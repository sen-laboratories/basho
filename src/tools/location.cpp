/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

/**
 * `location`: asks the location server (Basho) where this machine is, and for the tests and for people who want to try it.
 *
 *   location                        the fix that the server has (starts the server if it is not running)
 *   location get [--force] [--granularity G]   the same; --force looks up again first (if the rate limit allows)
 *   location watch [--granularity G]           prints every change until it is interrupted
 *                                   G is city, region, country or a number of km: a program asks for as little as it needs; what the user allows is the limit (max_precision)
 *   location set <lat> <lon> [city [country]]    a position by hand; it wins until it is cleared
 *   location clear                  back to what the lookup says
 *   location refresh               look up again
 *   location status                 the state of the server
 *   location config                  the settings, and the limits of the programs that have asked
 *   location config set <name> <value>   one setting (max_precision region, network_lookup allow, interval_minutes 15, ...)
 *   location grant <signature> <level>   what a program may know at most: city, region, country, none, a number of km, or default (the general limit)
 *   location running                exit status 0 if the server runs (it does not start it)
 *   location quit                   quits the server
 *
 * Exit status: 0 ok; 2 the user has not allowed the lookup; 3 no fix (yet); 1 anything else.
 */

#include <location/Location.h>
#include <location/LocationClient.h>

#include <Application.h>
#include <Message.h>
#include <Roster.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <string>
#include <vector>

static void
PrintFix(const BMessage& fix, const char* heading = NULL)
{
	time_t when = (time_t)fix.GetInt64("timestamp", 0);
	char text[64] = "";
	strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", localtime(&when));
	if (heading != NULL)
		printf("%s\n", heading);
	double latitude = 0, longitude = 0;
	if (fix.FindDouble("latitude", &latitude) == B_OK && fix.FindDouble("longitude", &longitude) == B_OK)
		printf("latitude   %.5f\nlongitude  %.5f\naccuracy   %.0f m\n", latitude, longitude, fix.GetFloat("accuracy", 0.0f));
	else
		printf("position   none (the user allows the country only)\n");
	printf("city       %s\nregion     %s\ncountry    %s\ntimezone   %s\nprovider   %s\ntime       %s\nstale      %s\nmanual     %s\ngranularity %s\n",
		fix.GetString("city", ""), fix.GetString("region", ""), fix.GetString("country", ""), fix.GetString("timezone", ""), fix.GetString("provider", ""), text,
		fix.GetBool("stale", false) ? "yes" : "no", fix.GetBool("manual", false) ? "yes" : "no", fix.GetString("granularity", ""));
	fflush(stdout);
}


static void
PrintSettings(const BMessage& settings)
{
	char* name;
	type_code type;
	int32 count;
	for (int32 i = 0; settings.GetInfo(B_ANY_TYPE, i, &name, &type, &count) == B_OK; i++) {
		if (type == B_MESSAGE_TYPE && std::string(name) == "programs") {
			// (the table of the programs: a line for each, with its signature and what it may know)
			for (int32 v = 0; v < count; v++) {
				BMessage row;
				if (settings.FindMessage(name, v, &row) != B_OK)
					continue;
				printf("program %-48s %s\n", row.GetString("program", ""), row.GetBool("allowed", true) ? row.GetString("level", "default") : "none");
			}
			continue;
		}
		if (type == B_MESSAGE_TYPE) {
			BMessage group;
			settings.FindMessage(name, &group);
			printf("%s\n", name);
			char* inner;
			type_code innerType;
			for (int32 j = 0; group.GetInfo(B_STRING_TYPE, j, &inner, &innerType) == B_OK; j++)
				printf("  %-48s %s\n", inner, group.GetString(inner, ""));
			continue;
		}
		printf("%-22s", name);
		for (int32 v = 0; v < count; v++) {
			const char* text;
			int32 number;
			if (settings.FindString(name, v, &text) == B_OK)
				printf(" %s", text);
			else if (settings.FindInt32(name, v, &number) == B_OK)
				printf(" %d", (int)number);
		}
		printf("\n");
	}
	fflush(stdout);
}


static int
ExitFor(status_t status)
{
	if (status == B_OK)
		return 0;
	if (status == B_NOT_ALLOWED)
		return 2;
	if (status == B_NO_INIT)
		return 3;
	return 1;
}


class LocationTool : public BApplication {
public:
	LocationTool(const std::vector<std::string>& args)
		:
		BApplication("application/x-vnd.basho-location_tool"),
		fArgs(args),
		fExit(0)
	{
	}

	int ExitCode() const { return fExit; }

	virtual void ReadyToRun()
	{
		fExit = _Run();
		if (fWatching)
			return;
		PostMessage(B_QUIT_REQUESTED);
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == LOCATION_CHANGED) {
			PrintFix(*message, "-- changed");
			return;
		}
		BApplication::MessageReceived(message);
	}

private:
	int _Run()
	{
		const std::string command = fArgs.empty() ? "get" : fArgs[0];
		LocationClient client;

		// (options after the command: --force, --granularity G)
		bool force = false;
		const char* granularity = NULL;
		for (size_t i = 1; i < fArgs.size(); i++) {
			if (fArgs[i] == "--force")
				force = true;
			else if (fArgs[i] == "--granularity" && i + 1 < fArgs.size())
				granularity = fArgs[++i].c_str();
		}

		if (command == "get") {
			BMessage fix;
			status_t result = client.GetCurrent(&fix, force ? 40000000 : 5000000, force, granularity);
			if (result == B_OK)
				PrintFix(fix);
			else
				fprintf(stderr, "no location: %s\n", result == B_NOT_ALLOWED ? "the lookup is not allowed" : result == B_NO_INIT ? "there is none yet" : result == B_BAD_VALUE ? "granularity is city, region, country or a number of km" : strerror(result));
			return ExitFor(result);
		}
		if (command == "watch") {
			status_t result = client.StartWatching(BMessenger(this), granularity);
			if (result != B_OK) {
				fprintf(stderr, "cannot watch: %s\n", strerror(result));
				return 1;
			}
			fWatching = true;
			printf("watching, interrupt to stop\n");
			fflush(stdout);
			return 0;
		}
		if (command == "set") {
			if (fArgs.size() < 3) {
				fprintf(stderr, "usage: location set <latitude> <longitude> [city [country]]\n");
				return 1;
			}
			status_t result = client.SetManualLocation(atof(fArgs[1].c_str()), atof(fArgs[2].c_str()), fArgs.size() > 3 ? fArgs[3].c_str() : NULL,
				fArgs.size() > 4 ? fArgs[4].c_str() : NULL);
			if (result != B_OK)
				fprintf(stderr, "not set: %s\n", strerror(result));
			return result == B_OK ? 0 : 1;
		}
		if (command == "clear")
			return client.ClearManualLocation() == B_OK ? 0 : 1;
		if (command == "refresh") {
			status_t result = client.Refresh();
			if (result == B_BUSY)
				fprintf(stderr, "the last lookup was too near, it is made a little later\n");
			return result == B_OK || result == B_BUSY ? 0 : ExitFor(result);
		}
		if (command == "status") {
			BMessage status;
			status_t result = client.GetStatus(&status);
			if (result != B_OK) {
				fprintf(stderr, "no answer: %s\n", strerror(result));
				return 1;
			}
			printf("network_lookup  %s%s\nmax_precision   %s\nlooking_up      %s\nhas_fix         %s\noverride        %s\nsubscribers     %d\nwaiting         %d\ninterval        %d min\npending         %d\nlast_error      %s\nproviders      ",
				status.GetString("network_lookup", ""), status.GetBool("session_allow", false) ? " (allowed for this run)" : "", status.GetString("max_precision", ""),
				status.GetBool("looking_up", false) ? "yes" : "no", status.GetBool("has_fix", false) ? "yes" : "no", status.GetBool("override_active", false) ? "yes" : "no",
				(int)status.GetInt32("subscribers", 0), (int)status.GetInt32("waiting", 0), (int)status.GetInt32("interval_minutes", 0),
				(int)status.GetInt32("pending_confirmations", 0), status.GetString("last_error", ""));
			const char* provider;
			for (int32 i = 0; status.FindString("provider", i, &provider) == B_OK; i++)
				printf(" %s", provider);
			printf("\n");
			return 0;
		}
		if (command == "config" || command == "grant") {
			BMessage settings;
			if (command == "config" && fArgs.size() == 1) {
				status_t result = client.GetSettings(&settings);
				if (result != B_OK) {
					fprintf(stderr, "no settings: %s\n", strerror(result));
					return 1;
				}
				PrintSettings(settings);
				return 0;
			}
			BString detail;
			status_t result = B_BAD_VALUE;
			if (command == "grant" && fArgs.size() == 3) {
				result = client.SetProgramLimit(fArgs[1].c_str(), fArgs[2].c_str(), &detail);
			} else if (command == "config" && fArgs.size() == 4 && fArgs[1] == "set") {
				// (the type of the value is the type of the setting)
				result = client.GetSettings(&settings);
				type_code type = B_STRING_TYPE;
				if (result == B_OK && fArgs[2].compare(0, 9, "programs.") != 0 && settings.GetInfo(fArgs[2].c_str(), &type) != B_OK)
					result = B_NAME_NOT_FOUND;
				if (result == B_OK) {
					BMessage changes;
					if (type == B_INT32_TYPE)
						changes.AddInt32(fArgs[2].c_str(), atoi(fArgs[3].c_str()));
					else
						changes.AddString(fArgs[2].c_str(), fArgs[3].c_str());
					result = client.ApplySettings(changes, &detail);
				}
			} else {
				fprintf(stderr, "usage: location config [set <name> <value>] | location grant <signature> <level>\n");
				return 1;
			}
			if (result != B_OK)
				fprintf(stderr, "not changed: %s%s%s\n", strerror(result), detail.IsEmpty() ? "" : " - ", detail.String());
			return result == B_OK ? 0 : 1;
		}
		if (command == "running")
			return LocationClient::IsRunning() ? 0 : 1;
		if (command == "quit") {
			BMessenger server(LOCATION_SERVER_SIGNATURE);
			if (!server.IsValid())
				return 0;
			return server.SendMessage(B_QUIT_REQUESTED) == B_OK ? 0 : 1;
		}
		fprintf(stderr, "usage: location [get [--force] | watch | set <lat> <lon> [city [country]] | clear | refresh | status | config | grant | running | quit]\n");
		return 1;
	}

	std::vector<std::string>	fArgs;
	int							fExit;
	bool						fWatching = false;
};


int
main(int argc, char** argv)
{
	std::vector<std::string> args;
	for (int i = 1; i < argc; i++)
		args.push_back(argv[i]);
	LocationTool tool(args);
	tool.Run();
	return tool.ExitCode();
}
