/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#include "Settings.h"

#include "Providers.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace basho {

const char*
NetworkLookupName(Settings::NetworkLookup value)
{
	switch (value) {
		case Settings::kAllow: return "allow";
		case Settings::kNever: return "never";
		default: return "ask";
	}
}

const Settings::ProgramRule*
Settings::FindProgram(const std::string& signature) const
{
	for (const ProgramRule& rule : programs) {
		if (rule.signature == signature)
			return &rule;
	}
	return NULL;
}

Granularity
Settings::LimitFor(const std::string& signature) const
{
	const ProgramRule* rule = FindProgram(signature);
	return rule != NULL ? rule->limit : maxPrecision;
}

void
Settings::SetProgram(const std::string& signature, const Granularity& limit)
{
	for (ProgramRule& rule : programs) {
		if (rule.signature == signature) {
			rule.limit = limit;
			return;
		}
	}
	ProgramRule rule;
	rule.signature = signature;
	rule.limit = limit;
	programs.push_back(rule);
}

void
Settings::RemoveProgram(const std::string& signature)
{
	for (auto it = programs.begin(); it != programs.end(); ++it) {
		if (it->signature == signature) {
			programs.erase(it);
			return;
		}
	}
}

std::vector<std::string>
Settings::ProviderIds() const
{
	return providers.empty() ? DefaultProviderIds() : providers;
}

LocationTracker::Config
Settings::TrackerConfig() const
{
	LocationTracker::Config config;
	config.minMoveKm = minMoveKm;
	config.confirmations = confirmations;
	return config;
}

static int
ClampInt(const std::string& text, int fallback, int low, int high)
{
	char* end = NULL;
	long value = std::strtol(text.c_str(), &end, 10);
	if (end == text.c_str())
		return fallback;
	return (int)std::max<long>(low, std::min<long>(high, value));
}

static double
ClampDouble(const std::string& text, double fallback, double low, double high)
{
	char* end = NULL;
	double value = std::strtod(text.c_str(), &end);
	if (end == text.c_str())
		return fallback;
	return std::max(low, std::min(high, value));
}

Settings
Settings::Parse(const std::string& text)
{
	Settings settings;
	std::map<std::string, std::string> v = ParseKeyValues(text);

	if (v.count("network_lookup")) {
		const std::string& value = v["network_lookup"];
		if (value == "allow")
			settings.networkLookup = kAllow;
		else if (value == "never")
			settings.networkLookup = kNever;
		else
			settings.networkLookup = kAsk;
	}
	if (v.count("providers")) {
		std::istringstream in(v["providers"]);
		std::string id;
		while (in >> id) {
			// (unknown names are kept out: a typo must not leave the list empty and ask nobody)
			if (FindProvider(id) != NULL)
				settings.providers.push_back(id);
		}
	}
	if (v.count("interval_minutes"))
		settings.intervalMinutes = ClampInt(v["interval_minutes"], 30, 5, 1440);
	if (v.count("min_move_km"))
		settings.minMoveKm = ClampDouble(v["min_move_km"], 25, 1, 500);
	if (v.count("confirmations"))
		settings.confirmations = ClampInt(v["confirmations"], 2, 1, 5);
	// (a value that is not understood is a typo, and a typo must not give away more than was meant: it is the coarsest)
	if (v.count("max_precision") && !ParseGranularity(v["max_precision"], &settings.maxPrecision))
		settings.maxPrecision = Granularity::Country();
	// the rules of programs: a line `program <signature> <level>` for each (the same key comes more than once, so they are read line by line)
	{
		std::istringstream lines(text);
		std::string line;
		while (std::getline(lines, line)) {
			std::istringstream words(line);
			std::string key, signature, level;
			if (!(words >> key) || key != "program" || !(words >> signature))
				continue;
			std::getline(words, level);
			Granularity limit;
			// (a level that is not understood is the coarsest that a program can be given, as for max_precision)
			if (!ParseGranularity(level, &limit))
				limit = Granularity::Country();
			settings.SetProgram(signature, limit);
		}
	}
	if (v.count("min_refresh_seconds"))
		settings.minRefreshSeconds = ClampInt(v["min_refresh_seconds"], 60, 10, 3600);
	return settings;
}

std::string
Settings::ToText() const
{
	std::ostringstream out;
	out << "# Settings of the location server (Basho). One setting per line: the name, a blank, the value.\n"
		<< "\n"
		<< "# May the public address of this machine be sent to a lookup service to find out roughly where it is?  ask | allow | never\n"
		<< "network_lookup " << NetworkLookupName(networkLookup) << "\n"
		<< "\n"
		<< "# The services that are asked, in this order. Available:";
	for (const Provider& provider : AllProviders())
		out << " " << provider.id << (provider.secure ? "" : " (http only: the answer can be read on the way, so it is not asked unless named here)");
	out << "\n";
	out << "providers";
	for (const std::string& id : ProviderIds())
		out << " " << id;
	out << "\n"
		<< "\n"
		<< "# Minutes between two lookups (5 to 1440). A change of the network triggers one, too.\n"
		<< "interval_minutes " << intervalMinutes << "\n"
		<< "\n"
		<< "# A new fix within this distance (km) of the announced one is the same place; IP addresses are placed to a city, not better.\n"
		<< "min_move_km " << minMoveKm << "\n"
		<< "\n"
		<< "# Lookups in a row that must agree on a new place before it is announced (1 to 5).\n"
		<< "confirmations " << confirmations << "\n"
		<< "\n"
		<< "# The finest location that programs that ask the server are told: city (what the lookup gave), region, country (no position at all) or a radius in km (1 to 1000).\n"
		<< "# A program can ask for less than this, never for more. The server itself keeps the finest fix it has.\n"
		<< "max_precision " << GranularityName(maxPrecision) << "\n"
		<< "\n"
		<< "# The limit of one program (its signature, then the level: city, region, country, none or a radius in km). It replaces max_precision for that program: a program can be\n"
		<< "# allowed more than the others, or less, or nothing at all. Programs that are not listed here get max_precision. One line for each program.\n";
	for (const ProgramRule& rule : programs)
		out << "program " << rule.signature << " " << GranularityName(rule.limit) << "\n";
	out << "\n"
		<< "# The shortest time (seconds) between two lookups, however often a program asks to refresh.\n"
		<< "min_refresh_seconds " << minRefreshSeconds << "\n";
	return out.str();
}

}	// namespace basho
