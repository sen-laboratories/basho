/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include "Fix.h"
#include "Granularity.h"
#include "Settings.h"
#include "SettingsMessage.h"
#include "Tracker.h"

#include <Application.h>
#include <Messenger.h>

#include <string>
#include <vector>

class BMessage;
class BMessageRunner;

/**
 * The location server: finds out roughly where the machine is (an IP lookup at a public service, the first time only after the user has agreed), keeps the fix in memory, and tells
 * the subscribers when it changes. See include/location/Location.h for the protocol and src/core/Tracker.h for what counts as a change.
 */
class LocationServer : public BApplication {
public:
							LocationServer(bool allowNetworkThisRun);
	virtual					~LocationServer();

	virtual void			ReadyToRun();
	virtual bool			QuitRequested();
	virtual void			MessageReceived(BMessage* message);

private:
	enum {
		kLookupDone		= 'LKdn',
		kTimer			= 'LKtm',
		kScheduled		= 'LKsc',
		kConsentAnswer	= 'LKca'
	};

	// requests
	void					_Subscribe(BMessage* message);
	void					_Unsubscribe(BMessage* message);
	void					_GetCurrent(BMessage* message);
	void					_ForceRefresh(BMessage* message);
	void					_SetOverride(BMessage* message);
	void					_ClearOverride(BMessage* message);
	void					_GetStatus(BMessage* message);
	void					_SettingsRequest(BMessage* message);
	void					_SettingsSet(BMessage* message);
	void					_ReplySettings(BMessage* to, status_t result, const std::vector<std::string>& refused);
	std::string				_PlaceText() const;
	void					_SettingsChanged(const basho::Settings& before);

	// the lookup
	bool					_NetworkAllowed() const;
	void					_AskConsent();
	status_t				_StartLookup(const char* why);
	void					_ScheduleLookup(bigtime_t delay);
	void					_LookupDone(BMessage* message);
	void					_AnswerWaiters();
	bool					_WantsFreshFix() const;

	// the fix
	bool					_EffectiveFix(basho::Fix* fix) const;
	bool					_IsStale(const basho::Fix& fix) const;
	void					_Announce();
	/** The program that sent the message (what the roster knows of the team): its signature, which the limits are kept by, and its name. */
	void					_ProgramOf(const BMessage* message, std::string* signature, std::string* name);
	void					_NoteProgram(const std::string& signature, const std::string& name);
	void					_LoadSeen();
	void					_SaveSeen();
	/** The fix made coarser to what this program may be told: the coarser of its limit (the setting for it, else the general one) and what it asked for. `effective` is none if it may not know. */
	basho::Fix				_Told(const basho::Fix& fix, const basho::Granularity& requested, const std::string& signature, basho::Granularity* effective) const;
	bool					_RequestedGranularity(const BMessage* message, basho::Granularity* requested) const;
	void					_ReplyFix(BMessage* to);
	void					_ReplyError(BMessage* to, status_t status, const char* error);

	// files
	void					_LoadSettings();
	void					_SaveSettings();
	void					_LoadCache();
	void					_SaveCache();

	basho::Settings			fSettings;
	basho::LocationTracker	fTracker;
	basho::Fix				fOverride;
	bool					fOverrideActive;

	struct Subscriber {
		BMessenger			target;
		std::string			signature;		///< of the program that subscribed: its limit is looked up at every change, so a change of the settings counts at once
		basho::Granularity	requested;		///< what it asked for (coarser than the setting, or the same)
		bool				hasSent = false;
		basho::Fix			lastSent;		///< what it was told last: a change that does not change this is not sent
	};
	std::vector<Subscriber>	fSubscribers;
	std::vector<basho::SeenProgram>	fSeen;		///< the programs that have asked, for the list in the settings
	std::vector<std::string>		fSettingsRows;	///< the programs of the table in the settings as they were last described: a change of a cell names its row by the number in this list
	std::vector<BMessage>	fWaiters;		///< GET_CURRENT with force, and the settings window's "Show location now" (LOCATION_SETTINGS_SET): answered when the lookup is done

	bool					fSessionAllow;	///< --allow-network: agreed for this run only
	bool					fAsked;			///< the window was shown (and answered "Not now") in this run
	bool					fAskPending;
	bool					fLookingUp;
	bigtime_t				fLastLookupStart;
	int64					fLastLookupTime;	///< seconds since the epoch of the last lookup that gave a fix
	std::string				fLastError;
	int						fFailures;

	BMessageRunner*			fTimer;
	BMessageRunner*			fScheduled;
};
