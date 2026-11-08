/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include "Granularity.h"
#include "Tracker.h"

#include <string>
#include <vector>

namespace basho {

/**
 * The settings of the server: lines `key value` in `~/config/settings/location_server` (the file is made with the defaults and a comment for each line at the first start).
 *
 *   network_lookup   ask | allow | never   whether the public address may be sent to a lookup service (default: ask, once, in a window)
 *   providers        ipwho.is geojs ...    the services, in the order they are asked (default: all that use https)
 *   interval_minutes 5..1440               how often the location is looked up again (default 30)
 *   min_move_km      1..500                a fix within this distance of the announced one is the same place (default 25)
 *   confirmations    1..5                  lookups in a row that must agree on a new place before it is announced (default 2)
 *   max_precision    city | region | country | <km>   the finest location that programs are told (see Granularity.h; default city = what the lookup gave)
 *   program <signature> <level>   the limit of one program (a line for each; level as for max_precision, or none): it replaces max_precision for that program, so a program can
 *                    be allowed more than the others or less. Programs that are not listed get max_precision.
 *   min_refresh_seconds 10..3600           the shortest time between two lookups, however often a client asks (default 60)
 */
struct Settings {
	enum NetworkLookup { kAsk, kAllow, kNever };

	NetworkLookup				networkLookup = kAsk;
	std::vector<std::string>	providers;			///< empty: the default (see DefaultProviderIds())
	int							intervalMinutes = 30;
	double						minMoveKm = 25;
	int							confirmations = 2;
	int							minRefreshSeconds = 60;
	Granularity					maxPrecision;		///< programs are never told a finer location than this

	/** The limit of one program (by its signature): instead of `maxPrecision`. */
	struct ProgramRule {
		std::string				signature;
		Granularity				limit;
	};
	std::vector<ProgramRule>	programs;			///< in the order of the file

	/** What a program (by signature) may be told at most: its rule, else `maxPrecision`. */
	Granularity					LimitFor(const std::string& signature) const;
	const ProgramRule*			FindProgram(const std::string& signature) const;
	/** Sets the rule of a program, or takes it away (`remove`): it is then limited by `maxPrecision` again. */
	void						SetProgram(const std::string& signature, const Granularity& limit);
	void						RemoveProgram(const std::string& signature);

	/** The providers to ask: the setting, else the default. */
	std::vector<std::string>	ProviderIds() const;
	LocationTracker::Config		TrackerConfig() const;

	static Settings				Parse(const std::string& text);
	std::string					ToText() const;
};

const char*	NetworkLookupName(Settings::NetworkLookup value);

}	// namespace basho
