/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#ifndef _LOCATION_CLIENT_H_
#define _LOCATION_CLIENT_H_

#include <Messenger.h>
#include <OS.h>
#include <String.h>

class BMessage;

/**
 * The convenience class for programs that want to know where the machine is. Needs a BApplication in the program (it uses the roster to find, and if need be to start, the server).
 *
 *   BLocationClient client;
 *   client.StartWatching(BMessenger(this));          // then LOCATION_CHANGED arrives in MessageReceived()
 *
 *   double latitude, longitude; BString city;
 *   if (client.GetCurrentLocation(&latitude, &longitude, &city) == B_OK) ...
 */
class BLocationClient {
public:
							BLocationClient();
							~BLocationClient();

	/** Whether the server runs. */
	static bool				IsRunning();

	/** The server is started if it is not running (the first call takes a moment). Gets the fix that the server has right away, then every change. */
	status_t				StartWatching(const BMessenger& target, const char* granularity = NULL);
	status_t				StopWatching(const BMessenger& target);

	/**
	 * The fix as a BMessage (the fields of Location.h). `force`: look up again first (if the server's rate limit allows). B_NO_INIT: there is no fix (yet); B_NOT_ALLOWED: the user said no.
	 * `granularity`: "city", "region", "country" or a number of km: ask only for as much as the program needs, the user may allow less than that (the message says what it got).
	 */
	status_t				GetCurrent(BMessage* fix, bigtime_t timeout = 2000000, bool force = false, const char* granularity = NULL);
	/** B_NOT_SUPPORTED if the user allows no position at all (the country level): ask GetCurrent() then, it has the country. */
	status_t				GetCurrentLocation(double* latitude, double* longitude, BString* city = NULL, bigtime_t timeout = 2000000, const char* granularity = NULL);

	/** For development and for people who do not want a lookup: a position that wins until it is cleared. */
	status_t				SetManualLocation(double latitude, double longitude, const char* city = NULL, const char* country = NULL);
	status_t				ClearManualLocation();
	status_t				Refresh();

	/** The state of the server (for tools). */
	status_t				GetStatus(BMessage* status, bigtime_t timeout = 2000000);

	/**
	 * The settings of the server as a message (and the schema that describes them, if you want it): what the settings window shows. The programs that have a limit or have asked are the
	 * messages of the field `programs` (a table in the form): `name`, `program` (the signature), `allowed` (bool) and `level` (`default`, `city`, `region`, `country` or a number of km).
	 */
	status_t				GetSettings(BMessage* settings, BMessage* schema = NULL, BMessage* defaults = NULL);
	/** Changes: fields under their path keys, as the settings window sends them (`max_precision`, `programs_03.level`; the row is the number in the last GetSettings()), or the field `program:<signature with its dots written as %2E>`. `detail` says what was refused. */
	status_t				ApplySettings(const BMessage& changes, BString* detail = NULL);
	/** What one program may know at most; `level` is `default` to take its limit away (it gets the general one again). */
	status_t				SetProgramLimit(const char* signature, const char* level, BString* detail = NULL);

private:
	status_t				_Server(BMessenger* server);
	status_t				_Send(BMessage* message, BMessage* reply, bigtime_t timeout);

	BMessenger				fServer;
};

#endif	// _LOCATION_CLIENT_H_
