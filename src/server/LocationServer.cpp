/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#include "LocationServer.h"

#include "HttpClient.h"
#include "LocationMessage.h"
#include "Providers.h"

#include <location/Location.h>

#include <Alert.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <Invoker.h>
#include <Message.h>
#include <MessageRunner.h>
#include <NetworkNotifications.h>
#include <Path.h>
#include <Roster.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <thread>

using basho::Fix;
using basho::LocationTracker;
using basho::Settings;

static const bigtime_t kSecond = 1000000;
static const bigtime_t kNetworkSettleTime = 8 * kSecond;	// (a network that has just come up is not ready: the address, the route, the resolver)


static std::string
SettingsPath()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK)
		return std::string();
	path.Append("location_server");
	return path.Path();
}


static std::string
CachePath()
{
	BPath path;
	if (find_directory(B_USER_CACHE_DIRECTORY, &path, true) != B_OK)
		return std::string();
	path.Append("location_server");
	create_directory(path.Path(), 0755);
	path.Append("last_fix");
	return path.Path();
}


static std::string
SeenPath()
{
	BPath path;
	if (find_directory(B_USER_CACHE_DIRECTORY, &path, true) != B_OK)
		return std::string();
	path.Append("location_server");
	create_directory(path.Path(), 0755);
	path.Append("seen_programs");
	return path.Path();
}


static std::string
ReadFile(const std::string& path)
{
	std::ifstream in(path);
	std::stringstream text;
	text << in.rdbuf();
	return text.str();
}


LocationServer::LocationServer(bool allowNetworkThisRun)
	:
	BApplication(LOCATION_SERVER_SIGNATURE),
	fOverrideActive(false),
	fSessionAllow(allowNetworkThisRun),
	fAsked(false),
	fAskPending(false),
	fLookingUp(false),
	fLastLookupStart(0),
	fLastLookupTime(0),
	fFailures(0),
	fTimer(NULL),
	fScheduled(NULL)
{
}


LocationServer::~LocationServer()
{
	delete fTimer;
	delete fScheduled;
}


void
LocationServer::ReadyToRun()
{
	_LoadSettings();
	_LoadCache();
	_LoadSeen();

	BMessage tick(kTimer);
	fTimer = new BMessageRunner(BMessenger(this), &tick, (bigtime_t)fSettings.intervalMinutes * 60 * kSecond);

	// a change of the network (a cable, a Wi-Fi that is joined) is the best time to look again: the place may have changed with it
	start_watching_network(B_WATCH_NETWORK_INTERFACE_CHANGES | B_WATCH_NETWORK_WLAN_CHANGES, BMessenger(this));

	// (nothing is looked up now: the server is also started to show its settings, and a lookup is for a program that asks. The first request, a subscriber or a question, makes one)
}


bool
LocationServer::QuitRequested()
{
	stop_watching_network(BMessenger(this));
	return true;
}


void
LocationServer::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case LOCATION_SUBSCRIBE:		_Subscribe(message); break;
		case LOCATION_UNSUBSCRIBE:		_Unsubscribe(message); break;
		case LOCATION_GET_CURRENT:		_GetCurrent(message); break;
		case LOCATION_FORCE_REFRESH:	_ForceRefresh(message); break;
		case LOCATION_SET_OVERRIDE:		_SetOverride(message); break;
		case LOCATION_CLEAR_OVERRIDE:	_ClearOverride(message); break;
		case LOCATION_GET_STATUS:		_GetStatus(message); break;
		case LOCATION_SETTINGS:			_SettingsRequest(message); break;
		case LOCATION_SETTINGS_SET:		_SettingsSet(message); break;
		case LOCATION_SETTINGS_CLOSE:	break;

		case kLookupDone:
			_LookupDone(message);
			break;
		case kTimer:
			if (_WantsFreshFix())
				_StartLookup("timer");
			break;
		case kScheduled:
			if (_WantsFreshFix())
				_StartLookup("scheduled");
			break;
		case B_NETWORK_MONITOR:
		{
			int32 opcode = message->GetInt32("opcode", 0);
			// (the interface changed, a link came up, a network was joined: not every message of it, the last one counts)
			if (_WantsFreshFix() && (opcode == B_NETWORK_INTERFACE_ADDED || opcode == B_NETWORK_INTERFACE_CHANGED || opcode == B_NETWORK_DEVICE_LINK_CHANGED
					|| opcode == B_NETWORK_WLAN_JOINED))
				_ScheduleLookup(kNetworkSettleTime);
			break;
		}
		case kConsentAnswer:
		{
			fAskPending = false;
			int32 which = message->GetInt32("which", 0);
			if (which == 2) {
				fSettings.networkLookup = Settings::kAllow;
				_SaveSettings();
				_StartLookup("consent");
			} else {
				if (which == 1) {
					fSettings.networkLookup = Settings::kNever;
					_SaveSettings();
				}
				fLastError = "the lookup was not allowed";
				_AnswerWaiters();
			}
			break;
		}
		default:
			BApplication::MessageReceived(message);
			break;
	}
}


// #pragma mark - requests


void
LocationServer::_Subscribe(BMessage* message)
{
	BMessenger target;
	if (message->FindMessenger("target", &target) != B_OK)
		target = message->ReturnAddress();
	if (!target.IsValid()) {
		if (message->IsSourceWaiting())
			_ReplyError(message, B_BAD_VALUE, "no target to send the changes to");
		return;
	}
	basho::Granularity requested;
	if (!_RequestedGranularity(message, &requested)) {
		if (message->IsSourceWaiting())
			_ReplyError(message, B_BAD_VALUE, "granularity is city, region, country or a number of km");
		return;
	}
	std::string signature, name;
	_ProgramOf(message, &signature, &name);
	_NoteProgram(signature, name);
	if (basho::Coarser(fSettings.LimitFor(signature), requested).IsNone()) {
		if (message->IsSourceWaiting())
			_ReplyError(message, B_NOT_ALLOWED, "the user does not allow this program to know where the computer is");
		return;
	}

	auto it = std::find_if(fSubscribers.begin(), fSubscribers.end(), [&](const Subscriber& s) { return s.target == target; });
	if (it == fSubscribers.end()) {
		Subscriber subscriber;
		subscriber.target = target;
		fSubscribers.push_back(subscriber);
		it = fSubscribers.end() - 1;
	}
	it->requested = requested;
	it->signature = signature;
	it->hasSent = false;

	if (message->IsSourceWaiting()) {
		BMessage ok(B_REPLY);
		ok.AddInt32("status", B_OK);
		message->SendReply(&ok);
	}

	// the fix that there is, now: a subscriber does not wait for the next lookup
	Fix fix;
	if (_EffectiveFix(&fix)) {
		basho::Granularity effective;
		Fix told = _Told(fix, requested, signature, &effective);
		BMessage current(LOCATION_CHANGED);
		FixToMessage(told, _IsStale(fix), basho::GranularityName(effective).c_str(), &current);
		if (target.SendMessage(&current) == B_OK) {
			it->hasSent = true;
			it->lastSent = told;
		}
	}
	// (and one that there is none of: the first subscriber is a reason to look)
	if (!fOverrideActive && !fTracker.HasCurrent())
		_StartLookup("subscriber");
}


void
LocationServer::_Unsubscribe(BMessage* message)
{
	BMessenger target;
	if (message->FindMessenger("target", &target) != B_OK)
		target = message->ReturnAddress();
	fSubscribers.erase(std::remove_if(fSubscribers.begin(), fSubscribers.end(), [&](const Subscriber& s) { return s.target == target; }), fSubscribers.end());
	if (message->IsSourceWaiting()) {
		BMessage ok(B_REPLY);
		ok.AddInt32("status", B_OK);
		message->SendReply(&ok);
	}
}


void
LocationServer::_GetCurrent(BMessage* message)
{
	bool force = message->GetBool("force", false);
	if (!fOverrideActive && (force || !fTracker.HasCurrent())) {
		// a look is asked for, or there is nothing yet: the answer comes when the lookup is done (a client has a time out of its own); if it cannot be started the answer is what there is
		if (_StartLookup("client") == B_OK || fLookingUp) {
			fWaiters.push_back(*message);
			return;
		}
	} else if (!fOverrideActive && fTracker.HasCurrent() && _IsStale(fTracker.Current())) {
		// (a fix from before the machine was off: it is answered, and made new meanwhile)
		_StartLookup("stale");
	}
	_ReplyFix(message);
}


void
LocationServer::_ForceRefresh(BMessage* message)
{
	status_t result = fOverrideActive ? B_OK : _StartLookup("refresh");
	if (message->IsSourceWaiting()) {
		BMessage reply(B_REPLY);
		reply.AddInt32("status", result);
		message->SendReply(&reply);
	}
}


void
LocationServer::_SetOverride(BMessage* message)
{
	Fix fix;
	if (!MessageToFix(message, &fix) || !fix.hasPosition) {
		if (message->IsSourceWaiting())
			_ReplyError(message, B_BAD_VALUE, "latitude and longitude are needed, in range");
		return;
	}
	fix.manual = true;
	fix.provider = "manual";
	fix.timestamp = time(NULL);
	fOverride = fix;
	fOverrideActive = true;
	if (message->IsSourceWaiting()) {
		BMessage ok(B_REPLY);
		ok.AddInt32("status", B_OK);
		message->SendReply(&ok);
	}
	_Announce();
}


void
LocationServer::_ClearOverride(BMessage* message)
{
	bool was = fOverrideActive;
	fOverrideActive = false;
	if (message->IsSourceWaiting()) {
		BMessage ok(B_REPLY);
		ok.AddInt32("status", B_OK);
		message->SendReply(&ok);
	}
	if (!was)
		return;
	if (fTracker.HasCurrent())
		_Announce();
	else
		_StartLookup("override cleared");
}


void
LocationServer::_GetStatus(BMessage* message)
{
	BMessage reply(B_REPLY);
	reply.AddInt32("status", B_OK);
	reply.AddString("network_lookup", basho::NetworkLookupName(fSettings.networkLookup));
	reply.AddString("max_precision", basho::GranularityName(fSettings.maxPrecision).c_str());
	reply.AddBool("session_allow", fSessionAllow);
	reply.AddBool("looking_up", fLookingUp);
	reply.AddBool("override_active", fOverrideActive);
	reply.AddBool("has_fix", fTracker.HasCurrent());
	reply.AddInt32("subscribers", (int32)fSubscribers.size());
	reply.AddInt32("waiting", (int32)fWaiters.size());
	reply.AddInt32("interval_minutes", fSettings.intervalMinutes);
	reply.AddInt32("pending_confirmations", fTracker.PendingCount());
	reply.AddInt64("last_lookup", fLastLookupTime);
	reply.AddString("last_error", fLastError.c_str());
	for (const std::string& id : fSettings.ProviderIds())
		reply.AddString("provider", id.c_str());
	message->SendReply(&reply);
}


// #pragma mark - the lookup


bool
LocationServer::_NetworkAllowed() const
{
	return fSessionAllow || fSettings.networkLookup == Settings::kAllow;
}


void
LocationServer::_AskConsent()
{
	if (fAskPending || fAsked)
		return;
	fAsked = true;
	fAskPending = true;

	std::string names;
	for (const std::string& id : fSettings.ProviderIds()) {
		const basho::Provider* provider = basho::FindProvider(id);
		if (provider == NULL)
			continue;
		names += names.empty() ? provider->name : std::string(", ") + provider->name;
	}
	std::string text =
		"The location server would like to find out roughly where this computer is: the city, not the street.\n\n"
		"For that it asks a public lookup service which place your internet address belongs to (" + names + ", one after the other until one answers). "
		"The service sees your internet address, nothing else is sent, and the answer stays on this computer.\n\n"
		"Programs that want to know where you are (a context that changes with the city, a place lookup that prefers what is near you) can then ask this server. "
		"You can change this in ~/config/settings/location_server.";
	BAlert* alert = new BAlert("Location server", text.c_str(), "Not now", "Never", "Allow", B_WIDTH_FROM_WIDEST, B_OFFSET_SPACING, B_INFO_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	BMessage* answer = new BMessage(kConsentAnswer);
	alert->Go(new BInvoker(answer, BMessenger(this)));
}


status_t
LocationServer::_StartLookup(const char* why)
{
	if (fLookingUp)
		return B_BUSY;

	if (!_NetworkAllowed()) {
		if (fSettings.networkLookup == Settings::kAsk)
			_AskConsent();
		fLastError = fSettings.networkLookup == Settings::kNever ? "the lookup is switched off (network_lookup never)" : "the user has not allowed the lookup yet";
		return B_NOT_ALLOWED;
	}

	// however often a program asks, the services are asked at most once in min_refresh_seconds; what was asked for comes a little later
	bigtime_t sinceLast = system_time() - fLastLookupStart;
	bigtime_t minimum = (bigtime_t)fSettings.minRefreshSeconds * kSecond;
	if (fLastLookupStart != 0 && sinceLast < minimum) {
		_ScheduleLookup(minimum - sinceLast);
		return B_BUSY;
	}

	fLookingUp = true;
	fLastLookupStart = system_time();
	fprintf(stderr, "location_server: looking up (%s)\n", why);

	std::vector<std::string> ids = fSettings.ProviderIds();
	BMessenger self(this);
	std::thread([ids, self]() mutable {
		basho::LookupResult result = basho::Lookup(ids, [](const std::string& url, std::string* body, std::string* error) {
			return basho::HttpFetch(url, body, error);
		}, (int64_t)time(NULL));
		BMessage done(kLookupDone);
		done.AddBool("ok", result.ok);
		if (result.ok)
			FixToMessage(result.fix, false, "city", &done);
		done.AddString("error", result.error.c_str());
		self.SendMessage(&done);
	}).detach();
	return B_OK;
}


void
LocationServer::_ScheduleLookup(bigtime_t delay)
{
	// (one at a time: a newer wish replaces the older, so that a burst of network messages makes one lookup)
	delete fScheduled;
	BMessage message(kScheduled);
	fScheduled = new BMessageRunner(BMessenger(this), &message, std::max<bigtime_t>(delay, kSecond), 1);
}


void
LocationServer::_LookupDone(BMessage* message)
{
	fLookingUp = false;
	Fix fix;
	if (!message->GetBool("ok", false) || !MessageToFix(message, &fix) || !fix.hasPosition) {
		fLastError = message->GetString("error", "the lookup failed");
		fFailures++;
		fprintf(stderr, "location_server: the lookup failed:\n%s", fLastError.c_str());
		// (no fix at all: try again soon, then less often)
		if (!fTracker.HasCurrent() && !fOverrideActive)
			_ScheduleLookup(std::min<bigtime_t>(kSecond * 60 * (1 << std::min(fFailures - 1, 4)), (bigtime_t)fSettings.intervalMinutes * 60 * kSecond));
		_AnswerWaiters();
		return;
	}
	fFailures = 0;
	fLastError.clear();
	fLastLookupTime = fix.timestamp;

	bool currentIsStale = fTracker.HasCurrent() && _IsStale(fTracker.Current());
	LocationTracker::Result result = fTracker.Update(fix, currentIsStale);
	fprintf(stderr, "location_server: %s, %s by %s: %s\n", fix.city.c_str(), fix.country.c_str(), fix.provider.c_str(),
		result == LocationTracker::kSame ? "the same place" : result == LocationTracker::kPending ? "somewhere else, waiting for a second lookup" : "new place");

	// (a pending move is looked at again soon: the confirmation should not wait for the next interval)
	if (result == LocationTracker::kPending)
		_ScheduleLookup((bigtime_t)fSettings.minRefreshSeconds * kSecond);

	_SaveCache();
	if (result == LocationTracker::kFirst || result == LocationTracker::kMoved) {
		if (!fOverrideActive)
			_Announce();
	}
	_AnswerWaiters();
}


// Whether anybody cares for a fresh fix: a subscriber, or a program that waits for an answer. Otherwise the timer and a change of the network let the server be.
bool
LocationServer::_WantsFreshFix() const
{
	return !fSubscribers.empty() || !fWaiters.empty();
}


void
LocationServer::_AnswerWaiters()
{
	std::vector<BMessage> waiters;
	waiters.swap(fWaiters);
	for (BMessage& waiter : waiters) {
		// (a program that asked for the location, or the settings window that was asked to show it)
		if (waiter.what == LOCATION_SETTINGS_SET)
			_ReplySettings(&waiter, B_OK, std::vector<std::string>());
		else
			_ReplyFix(&waiter);
	}
}


// #pragma mark - the fix


bool
LocationServer::_EffectiveFix(Fix* fix) const
{
	if (fOverrideActive) {
		*fix = fOverride;
		return true;
	}
	if (fTracker.HasCurrent()) {
		*fix = fTracker.Current();
		return true;
	}
	return false;
}


bool
LocationServer::_IsStale(const Fix& fix) const
{
	if (fix.manual)
		return false;
	return time(NULL) - fix.timestamp > (int64)2 * fSettings.intervalMinutes * 60;
}


basho::Fix
LocationServer::_Told(const Fix& fix, const basho::Granularity& requested, const std::string& signature, basho::Granularity* effective) const
{
	*effective = basho::Coarser(fSettings.LimitFor(signature), requested);
	return effective->IsNone() ? fix : basho::Coarsen(fix, *effective);
}


bool
LocationServer::_RequestedGranularity(const BMessage* message, basho::Granularity* requested) const
{
	*requested = basho::Granularity::City();
	const char* text = NULL;
	if (message->FindString("granularity", &text) != B_OK || text == NULL)
		return true;
	return basho::ParseGranularity(text, requested);
}


void
LocationServer::_Announce()
{
	Fix fix;
	if (!_EffectiveFix(&fix))
		return;
	bool stale = _IsStale(fix);
	// (each subscriber is told what it may be told: a change that does not change that, a move inside the country for a program that asked for the country, is not sent;
	// one whose program has quit is dropped when it can no longer be reached)
	for (auto it = fSubscribers.begin(); it != fSubscribers.end();) {
		basho::Granularity effective;
		Fix told = _Told(fix, it->requested, it->signature, &effective);
		if (effective.IsNone() || (it->hasSent && basho::SameAnswer(it->lastSent, told))) {
			++it;
			continue;
		}
		BMessage changed(LOCATION_CHANGED);
		FixToMessage(told, stale, basho::GranularityName(effective).c_str(), &changed);
		if (!it->target.IsValid() || it->target.SendMessage(&changed, (BHandler*)NULL, 0) != B_OK) {
			it = fSubscribers.erase(it);
			continue;
		}
		it->hasSent = true;
		it->lastSent = told;
		++it;
	}
}


void
LocationServer::_ReplyFix(BMessage* to)
{
	basho::Granularity requested;
	if (!_RequestedGranularity(to, &requested)) {
		_ReplyError(to, B_BAD_VALUE, "granularity is city, region, country or a number of km");
		return;
	}
	std::string signature, name;
	_ProgramOf(to, &signature, &name);
	_NoteProgram(signature, name);
	if (basho::Coarser(fSettings.LimitFor(signature), requested).IsNone()) {
		_ReplyError(to, B_NOT_ALLOWED, "the user does not allow this program to know where the computer is");
		return;
	}
	Fix fix;
	if (!_EffectiveFix(&fix)) {
		if (fSettings.networkLookup == Settings::kNever)
			_ReplyError(to, B_NOT_ALLOWED, "the lookup is switched off and no position was set by hand");
		else if (!fLastError.empty() && !fLookingUp)
			_ReplyError(to, B_NO_INIT, fLastError.c_str());
		else
			_ReplyError(to, B_NO_INIT, "no location yet");
		return;
	}
	basho::Granularity effective;
	Fix told = _Told(fix, requested, signature, &effective);
	BMessage reply(LOCATION_CURRENT_REPLY);
	FixToMessage(told, _IsStale(fix), basho::GranularityName(effective).c_str(), &reply);
	to->SendReply(&reply);
}


void
LocationServer::_ReplyError(BMessage* to, status_t status, const char* error)
{
	BMessage reply(LOCATION_ERROR);
	reply.AddString("error", error);
	reply.AddInt32("status", status);
	to->SendReply(&reply);
}


// #pragma mark - the programs and the settings


void
LocationServer::_ProgramOf(const BMessage* message, std::string* signature, std::string* name)
{
	signature->clear();
	name->clear();
	team_id team = message->ReturnAddress().Team();
	app_info info;
	if (team >= 0 && be_roster != NULL && be_roster->GetRunningAppInfo(team, &info) == B_OK) {
		*signature = info.signature;
		*name = info.ref.name != NULL ? info.ref.name : "";
	}
}


void
LocationServer::_NoteProgram(const std::string& signature, const std::string& name)
{
	if (signature.empty())
		return;
	for (const basho::SeenProgram& seen : fSeen) {
		if (seen.signature == signature)
			return;
	}
	basho::SeenProgram program;
	program.signature = signature;
	program.name = name;
	fSeen.push_back(program);
	_SaveSeen();
}


void
LocationServer::_LoadSeen()
{
	std::istringstream lines(ReadFile(SeenPath()));
	std::string line;
	while (std::getline(lines, line)) {
		size_t tab = line.find('\t');
		if (tab == std::string::npos || tab == 0)
			continue;
		basho::SeenProgram program;
		program.signature = line.substr(0, tab);
		program.name = line.substr(tab + 1);
		fSeen.push_back(program);
	}
}


void
LocationServer::_SaveSeen()
{
	std::string path = SeenPath();
	if (path.empty())
		return;
	std::ofstream out(path, std::ios::trunc);
	for (const basho::SeenProgram& program : fSeen)
		out << program.signature << "\t" << program.name << "\n";
}


void
LocationServer::_SettingsRequest(BMessage* message)
{
	BMessage values, schema, defaults;
	fSettingsRows.clear();
	basho::DescribeSettings(fSettings, fSeen, &values, &schema, &defaults, &fSettingsRows, _PlaceText());

	BMessage reply(LOCATION_SETTINGS_REPLY);
	reply.AddInt32("result", B_OK);
	reply.AddInt32("protocol", 1);
	reply.AddString("SEN:plugin", LOCATION_SERVER_SIGNATURE);
	reply.AddMessage("settings", &values);
	reply.AddMessage("schema", &schema);
	reply.AddMessage("defaults", &defaults);
	int32 serial;
	if (message->FindInt32("serial", &serial) == B_OK)
		reply.AddInt32("serial", serial);

	BMessenger owner;
	if (message->IsSourceWaiting()) {
		message->SendReply(&reply);
	} else if (message->FindMessenger("reply_to", &owner) == B_OK && owner.IsValid()) {
		owner.SendMessage(&reply);
	} else if (message->ReturnAddress().IsValid()) {
		message->ReturnAddress().SendMessage(&reply);
	}
}


void
LocationServer::_SettingsSet(BMessage* message)
{
	BMessage changes;
	status_t result = message->FindMessage("settings", &changes);
	std::vector<std::string> refused;
	Settings before = fSettings;
	if (result == B_OK)
		result = basho::ApplySettingsChanges(&fSettings, changes, &refused, fSettingsRows);
	_SettingsChanged(before);

	// the button "Show location now": the place is looked up (if that is allowed and not done a moment ago) and the answer, with what was found, comes when that is done
	if (result == B_OK && changes.GetBool("show_location", false) && !fOverrideActive) {
		const status_t started = _StartLookup("settings");
		const bool consentPending = started == B_NOT_ALLOWED && fSettings.networkLookup == Settings::kAsk && fAskPending;
		if (started == B_OK || fLookingUp || consentPending) {
			fWaiters.push_back(*message);
			return;
		}
	}
	_ReplySettings(message, result, refused);
}


// The answer to a change of the settings (or to the button): the settings as they are now, with the place that the server knows. It is sent whether or not the sender waits for it:
// the settings window does not, and shows what comes.
void
LocationServer::_ReplySettings(BMessage* to, status_t result, const std::vector<std::string>& refused)
{
	BMessage values;
	fSettingsRows.clear();
	basho::DescribeSettings(fSettings, fSeen, &values, NULL, NULL, &fSettingsRows, _PlaceText());
	BMessage reply(B_REPLY);
	reply.AddInt32("result", result);
	if (!refused.empty()) {
		std::string detail = "Refused: ";
		for (size_t i = 0; i < refused.size(); i++)
			detail += (i > 0 ? ", " : "") + refused[i];
		reply.AddString("detail", detail.c_str());
	}
	reply.AddMessage("settings", &values);
	to->SendReply(&reply);
}


// What the settings window shows as the place that was found: the place with its position (the finest the server has: this is the user's own window, not a program that asked), how it was
// found and when; or why there is none.
std::string
LocationServer::_PlaceText() const
{
	Fix fix;
	if (!_EffectiveFix(&fix)) {
		if (fSettings.networkLookup == Settings::kNever)
			return "No place: the lookup is switched off.";
		if (fLookingUp)
			return "Looking it up...";
		if (!fLastError.empty()) {
			std::string firstLine = fLastError.substr(0, fLastError.find('\n'));
			return "No place yet: " + firstLine;
		}
		return "Not looked up yet.";
	}
	std::string text;
	for (const std::string* part : {&fix.city, &fix.region, &fix.country}) {
		if (part->empty() || (!text.empty() && text.size() >= part->size() && text.compare(text.size() - part->size(), part->size(), *part) == 0))
			continue;
		text += text.empty() ? *part : ", " + *part;
	}
	if (text.empty())
		text = "Somewhere";
	if (fix.hasPosition) {
		char position[64];
		snprintf(position, sizeof(position), " (%.2f, %.2f)", fix.latitude, fix.longitude);
		text += position;
	}
	text += fix.manual ? std::string(", set by hand") : ", by " + fix.provider;
	if (fix.timestamp > 0) {
		const time_t when = (time_t)fix.timestamp;
		char clock[32];
		struct tm local;
		localtime_r(&when, &local);
		strftime(clock, sizeof(clock), " at %H:%M", &local);
		text += clock;
	}
	return text;
}


// A change of the settings counts at once: it is stored, the timer and the tracker follow it, and every subscriber is told what it may be told now (a program that was
// given more or less than before hears of it, one that is not allowed any more hears nothing).
void
LocationServer::_SettingsChanged(const Settings& before)
{
	_SaveSettings();
	fTracker.SetConfig(fSettings.TrackerConfig());
	if (fSettings.intervalMinutes != before.intervalMinutes) {
		delete fTimer;
		BMessage tick(kTimer);
		fTimer = new BMessageRunner(BMessenger(this), &tick, (bigtime_t)fSettings.intervalMinutes * 60 * kSecond);
	}
	_Announce();
	// (the lookup was just allowed, and there is nothing yet)
	if (before.networkLookup != fSettings.networkLookup && fSettings.networkLookup == Settings::kAllow && !fTracker.HasCurrent() && !fOverrideActive)
		_StartLookup("allowed");
}


// #pragma mark - files


void
LocationServer::_LoadSettings()
{
	std::string path = SettingsPath();
	std::string text = path.empty() ? std::string() : ReadFile(path);
	fSettings = Settings::Parse(text);
	fTracker.SetConfig(fSettings.TrackerConfig());
	// (the first start writes the file, so that people see what can be set)
	if (!path.empty() && text.empty())
		_SaveSettings();
}


void
LocationServer::_SaveSettings()
{
	std::string path = SettingsPath();
	if (path.empty())
		return;
	std::ofstream out(path, std::ios::trunc);
	out << fSettings.ToText();
}


void
LocationServer::_LoadCache()
{
	std::string path = CachePath();
	Fix fix;
	if (!path.empty() && basho::FixFromText(ReadFile(path), &fix) && !fix.manual)
		fTracker.Set(fix);	// (it may be stale: _IsStale() says so, and the first lookup replaces it without waiting for a second)
}


void
LocationServer::_SaveCache()
{
	std::string path = CachePath();
	if (path.empty() || !fTracker.HasCurrent())
		return;
	std::ofstream out(path, std::ios::trunc);
	out << basho::FixToText(fTracker.Current());
}
