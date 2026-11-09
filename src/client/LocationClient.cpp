/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#include <location/LocationClient.h>

#include <location/Location.h>

#include <Message.h>
#include <Roster.h>

LocationClient::LocationClient()
{
}


LocationClient::~LocationClient()
{
}


bool
LocationClient::IsRunning()
{
	return be_roster != NULL && be_roster->IsRunning(LOCATION_SERVER_SIGNATURE);
}


status_t
LocationClient::_Server(BMessenger* server)
{
	if (!fServer.IsValid()) {
		if (be_roster == NULL)
			return B_NO_INIT;	// (no BApplication in this program)
		if (!be_roster->IsRunning(LOCATION_SERVER_SIGNATURE)) {
			status_t result = be_roster->Launch(LOCATION_SERVER_SIGNATURE);
			if (result != B_OK && result != B_ALREADY_RUNNING)
				return result;
		}
		// (it takes a moment until the server has a looper that answers)
		BMessenger messenger(LOCATION_SERVER_SIGNATURE);
		for (int i = 0; i < 30 && !messenger.IsValid(); i++) {
			snooze(100000);
			messenger = BMessenger(LOCATION_SERVER_SIGNATURE);
		}
		if (!messenger.IsValid())
			return B_ERROR;
		fServer = messenger;
	}
	*server = fServer;
	return B_OK;
}


status_t
LocationClient::_Send(BMessage* message, BMessage* reply, bigtime_t timeout)
{
	BMessenger server;
	status_t result = _Server(&server);
	if (result != B_OK)
		return result;
	BMessage ignored;
	result = server.SendMessage(message, reply != NULL ? reply : &ignored, timeout, timeout);
	if (result == B_BAD_PORT_ID || result == B_BAD_VALUE)
		fServer = BMessenger();		// (the server was quit: the next call finds or starts it again)
	return result;
}


status_t
LocationClient::StartWatching(const BMessenger& target, const char* granularity)
{
	BMessage message(LOCATION_SUBSCRIBE);
	message.AddMessenger("target", target);
	if (granularity != NULL)
		message.AddString("granularity", granularity);
	BMessage reply;
	status_t result = _Send(&message, &reply, 5000000);
	return result != B_OK ? result : (status_t)reply.GetInt32("status", B_OK);
}


status_t
LocationClient::StopWatching(const BMessenger& target)
{
	BMessage message(LOCATION_UNSUBSCRIBE);
	message.AddMessenger("target", target);
	BMessage reply;
	status_t result = _Send(&message, &reply, 2000000);
	return result != B_OK ? result : (status_t)reply.GetInt32("status", B_OK);
}


status_t
LocationClient::GetCurrent(BMessage* fix, bigtime_t timeout, bool force, const char* granularity)
{
	BMessage request(LOCATION_GET_CURRENT);
	request.AddBool("force", force);
	if (granularity != NULL)
		request.AddString("granularity", granularity);
	BMessage reply;
	status_t result = _Send(&request, &reply, timeout);
	if (result != B_OK)
		return result;
	if (reply.what == LOCATION_ERROR)
		return (status_t)reply.GetInt32("status", B_ERROR);
	if (reply.what != LOCATION_CURRENT_REPLY)
		return B_BAD_DATA;
	*fix = reply;
	return B_OK;
}


status_t
LocationClient::GetCurrentLocation(double* latitude, double* longitude, BString* city, bigtime_t timeout, const char* granularity)
{
	BMessage fix;
	status_t result = GetCurrent(&fix, timeout, false, granularity);
	if (result != B_OK)
		return result;
	if (!fix.HasDouble("latitude"))
		return B_NOT_SUPPORTED;		// (the user allows the country only)
	if (fix.FindDouble("latitude", latitude) != B_OK || fix.FindDouble("longitude", longitude) != B_OK)
		return B_BAD_DATA;
	if (city != NULL)
		*city = fix.GetString("city", "");
	return B_OK;
}


status_t
LocationClient::SetManualLocation(double latitude, double longitude, const char* city, const char* country)
{
	BMessage message(LOCATION_SET_OVERRIDE);
	message.AddDouble("latitude", latitude);
	message.AddDouble("longitude", longitude);
	if (city != NULL)
		message.AddString("city", city);
	if (country != NULL)
		message.AddString("country", country);
	BMessage reply;
	status_t result = _Send(&message, &reply, 2000000);
	return result != B_OK ? result : (status_t)reply.GetInt32("status", B_OK);
}


status_t
LocationClient::ClearManualLocation()
{
	BMessage message(LOCATION_CLEAR_OVERRIDE);
	BMessage reply;
	status_t result = _Send(&message, &reply, 2000000);
	return result != B_OK ? result : (status_t)reply.GetInt32("status", B_OK);
}


status_t
LocationClient::Refresh()
{
	BMessage message(LOCATION_FORCE_REFRESH);
	BMessage reply;
	status_t result = _Send(&message, &reply, 2000000);
	return result != B_OK ? result : (status_t)reply.GetInt32("status", B_OK);
}


status_t
LocationClient::GetStatus(BMessage* status, bigtime_t timeout)
{
	BMessage message(LOCATION_GET_STATUS);
	return _Send(&message, status, timeout);
}


status_t
LocationClient::GetSettings(BMessage* settings, BMessage* schema, BMessage* defaults, const char* page)
{
	BMessage request(LOCATION_SETTINGS);
	if (page != NULL)
		request.AddString("page", page);
	BMessage reply;
	status_t result = _Send(&request, &reply, 3000000);
	if (result != B_OK)
		return result;
	status_t status = (status_t)reply.GetInt32("result", B_OK);
	if (status != B_OK)
		return status;
	if (settings != NULL)
		reply.FindMessage("settings", settings);
	if (schema != NULL)
		reply.FindMessage("schema", schema);
	if (defaults != NULL)
		reply.FindMessage("defaults", defaults);
	return B_OK;
}


status_t
LocationClient::ApplySettings(const BMessage& changes, BString* detail)
{
	BMessage request(LOCATION_SETTINGS_SET);
	request.AddMessage("settings", &changes);
	BMessage reply;
	status_t result = _Send(&request, &reply, 3000000);
	if (result != B_OK)
		return result;
	if (detail != NULL)
		*detail = reply.GetString("detail", "");
	return (status_t)reply.GetInt32("result", B_OK);
}


status_t
LocationClient::SetProgramLimit(const char* signature, const char* level, BString* detail)
{
	// (the name of a program's field: the dots of the signature are written as %2E, as the settings message does it; a backslash and a percent sign, too)
	BString key("program:");
	for (const char* c = signature; *c != '\0'; c++) {
		if (*c == '.')
			key << "%2E";
		else if (*c == '\\')
			key << "%5C";
		else if (*c == '%')
			key << "%25";
		else
			key << *c;
	}
	BMessage changes;
	changes.AddString(key.String(), level);
	return ApplySettings(changes, detail);
}
