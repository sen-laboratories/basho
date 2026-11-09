/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#include "SettingsMessage.h"

#include "ProgramKey.h"
#include "Providers.h"

#include <Message.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>

namespace basho {

const char* const kProgramPrefix = "program:";
const char* const kProgramDefault = "default";

// the names of the attributes of a field in the schema (sento: SenSettingsForm.h)
static const char* const kLabel = "label";
static const char* const kHelp = "help";
static const char* const kKind = "kind";
static const char* const kChoices = "choices";
static const char* const kChoiceLabels = "choice_labels";
static const char* const kMin = "min";
static const char* const kMax = "max";
static const char* const kStep = "step";
static const char* const kOrder = "order";
static const char* const kList = "list";
static const char* const kVersion = "version";
static const char* const kReadOnly = "readonly";
static const char* const kHidden = "hidden";
static const char* const kEnabledWhen = "enabled_when";
static const char* const kShowLocation = "show_location";
static const char* const kWhere = "where";

// the table of the programs: an array of messages, a row for each program (`kind = table` in the schema); the path key of a cell is `programs_03.level`
static const char* const kPrograms = "programs";
// the table of the lookup services: a row for each service that Basho knows, in a fixed order (a row stays where it is while it is changed): `services_01.enabled`
static const char* const kServices = "services";


std::string
ProgramFieldName(const std::string& signature)
{
	return std::string(kProgramPrefix) + EncodeProgramKey(signature);
}


std::string
ProgramPathKey(const std::string& signature)
{
	// (the same: nothing in the name needs an escape)
	return ProgramFieldName(signature);
}


std::string
SignatureOfField(const std::string& name)
{
	return SignatureOfProgramField(name);
}


// the levels that the form offers (values and texts); a level that is set in the file and is not among them is added
struct Level {
	const char* value;
	const char* text;
};

static const Level kLevels[] = {
	{"city", "City (as looked up)"},
	{"10", "10 km"},
	{"25", "25 km"},
	{"50", "50 km"},
	{"region", "Region (100 km)"},
	{"200", "200 km"},
	{"500", "500 km"},
	{"country", "Country only"},
};


static void
AddLevels(BMessage* attributes, bool forProgram, const std::string& current, const char* defaultText)
{
	if (forProgram) {
		attributes->AddString(kChoices, kProgramDefault);
		attributes->AddString(kChoiceLabels, defaultText);
	}
	bool found = current == "city" || current == kProgramDefault;
	for (const Level& level : kLevels) {
		attributes->AddString(kChoices, level.value);
		attributes->AddString(kChoiceLabels, level.text);
		if (current == level.value)
			found = true;
	}
	// (a radius that was written into the file by hand: the form must be able to show it)
	if (!found && current != "none") {
		attributes->AddString(kChoices, current.c_str());
		attributes->AddString(kChoiceLabels, (current + " km").c_str());
	}
}


static void
AddNumberField(BMessage* schema, const char* key, const char* label, const char* help, double min, double max, int32 order)
{
	BMessage attributes;
	attributes.AddString(kLabel, label);
	attributes.AddString(kHelp, help);
	attributes.AddString(kKind, "spinner");
	attributes.AddDouble(kMin, min);
	attributes.AddDouble(kMax, max);
	attributes.AddDouble(kStep, 1);
	attributes.AddInt32(kOrder, order);
	schema->AddMessage(key, &attributes);
}


static std::string
NameOf(const std::string& signature)
{
	size_t at = signature.find_last_of("/.-");
	return at == std::string::npos || at + 1 >= signature.size() ? signature : signature.substr(at + 1);
}


// The programs of the table: those that have a rule, and those that have asked, in the order of their names (an order that does not depend on the rules, so that a row stays
// where it is while it is changed; the change of a cell names its row by the number it had in the last description).
static std::vector<std::pair<std::string, std::string>>
ListPrograms(const Settings& settings, const std::vector<SeenProgram>& seen)
{
	std::vector<std::pair<std::string, std::string>> listed;		// signature, name
	for (const Settings::ProgramRule& rule : settings.programs)
		listed.push_back({rule.signature, NameOf(rule.signature)});
	for (const SeenProgram& program : seen) {
		auto same = [&](const std::pair<std::string, std::string>& p) { return p.first == program.signature; };
		auto found = std::find_if(listed.begin(), listed.end(), same);
		if (found == listed.end())
			listed.push_back({program.signature, program.name.empty() ? NameOf(program.signature) : program.name});
		else if (!program.name.empty())
			found->second = program.name;
	}
	auto lower = [](std::string text) {
		for (char& c : text)
			c = (char)tolower((unsigned char)c);
		return text;
	};
	std::stable_sort(listed.begin(), listed.end(), [&lower](const std::pair<std::string, std::string>& a, const std::pair<std::string, std::string>& b) {
		const std::string x = lower(a.second), y = lower(b.second);
		return x != y ? x < y : a.first < b.first;
	});
	return listed;
}


// One row: the name to show, the signature (hidden), whether the program may know anything, and its level (`default`: as the others)
static void
AddProgramRow(BMessage* values, const Settings& settings, const std::string& signature, const std::string& name)
{
	const Settings::ProgramRule* rule = settings.FindProgram(signature);
	const bool allowed = rule == NULL || !rule->limit.IsNone();
	BMessage row;
	row.AddString("name", name.c_str());
	row.AddString("program", signature.c_str());
	row.AddBool("allowed", allowed);
	row.AddString("level", rule != NULL && allowed ? GranularityName(rule->limit).c_str() : kProgramDefault);
	values->AddMessage(kPrograms, &row);
}


// The name of a service as the table shows it: a service that answers over http only says so (the answer can be read on the way)
static std::string
ServiceName(const Provider& provider)
{
	return provider.secure ? std::string(provider.name) : std::string(provider.name) + " (http only)";
}


static void
FillValues(const Settings& settings, const std::vector<SeenProgram>& seen, BMessage* values, std::vector<std::string>* rows, const std::string& place)
{
	values->AddString("network_lookup", NetworkLookupName(settings.networkLookup));
	values->AddBool(kShowLocation, false);		// (a button: never stored)
	values->AddString(kWhere, place.c_str());
	values->AddString("max_precision", GranularityName(settings.maxPrecision).c_str());
	const std::vector<std::string> enabled = settings.ProviderIds();
	for (const Provider& provider : AllProviders()) {
		BMessage row;
		row.AddString("name", ServiceName(provider).c_str());
		row.AddString("id", provider.id);
		row.AddBool("enabled", std::find(enabled.begin(), enabled.end(), provider.id) != enabled.end());
		values->AddMessage(kServices, &row);
	}
	values->AddInt32("interval_minutes", settings.intervalMinutes);
	values->AddInt32("min_move_km", (int32)std::lround(settings.minMoveKm));
	values->AddInt32("confirmations", settings.confirmations);
	values->AddInt32("min_refresh_seconds", settings.minRefreshSeconds);

	for (const auto& program : ListPrograms(settings, seen)) {
		AddProgramRow(values, settings, program.first, program.second);
		if (rows != NULL)
			rows->push_back(program.first);
	}
}


void
DescribeSettings(const Settings& settings, const std::vector<SeenProgram>& seen, BMessage* values, BMessage* schema, BMessage* defaults, std::vector<std::string>* rows,
	const std::string& place)
{
	BMessage scratch;
	FillValues(settings, seen, values != NULL ? values : &scratch, rows, place);

	// the schema: the form of the settings
	if (schema != NULL) {
		schema->AddInt32(kVersion, 1);

		BMessage lookup;
		lookup.AddString(kLabel, "Look up the place by my internet address");
		lookup.AddString(kHelp, "A lookup sends the public address of this computer to a service on the internet (ipwho.is, GeoJS, ipinfo.io). Ask: the window is shown the first time. Never: only a position that is set by hand is used.");
		lookup.AddString(kKind, "choice");
		for (const char* value : {"ask", "allow", "never"})
			lookup.AddString(kChoices, value);
		for (const char* text : {"Ask me", "Allow", "Never"})
			lookup.AddString(kChoiceLabels, text);
		lookup.AddInt32(kOrder, 0);
		schema->AddMessage("network_lookup", &lookup);

		// a button under it: where does the server think this computer is, now? (a lookup if it is allowed; what it found is shown below)
		BMessage show;
		show.AddString(kLabel, "Show Location now");
		show.AddString("shows", kWhere);
		show.AddString(kHelp, "Looks the place up now (if the lookup is allowed) and shows what the server found, with how and when, the same as it would tell a program that may know everything.");
		show.AddString(kKind, "button");
		show.AddInt32(kOrder, 1);
		schema->AddMessage(kShowLocation, &show);

		BMessage where;
		where.AddString(kLabel, "Place found");
		where.AddString(kHelp, "What the server knows now: the place, how it was found and when.");
		where.AddBool(kReadOnly, true);
		where.AddInt32(kOrder, 2);
		schema->AddMessage(kWhere, &where);

		BMessage precision;
		precision.AddString(kLabel, "Programs may know");
		precision.AddString(kHelp, "The finest location that programs are told, unless they have a limit of their own (the programs that have asked are listed below). A program can ask for less than this, never for more. The server keeps the finest fix it has.");
		precision.AddString(kKind, "choice");
		AddLevels(&precision, false, GranularityName(settings.maxPrecision), "");
		precision.AddInt32(kOrder, 3);
		schema->AddMessage("max_precision", &precision);

		// the programs that have asked: a table with a check box for each (may it know where I am at all) and the finest level it may know
		BMessage table;
		table.AddString(kLabel, "Programs that have asked where this computer is");
		table.AddString(kHelp, "Click a box to allow or refuse a program, click a level to choose how much it may know. A change counts at once, also for a program that is asking already.");
		table.AddString(kKind, "table");
		table.AddInt32(kOrder, 4);
		schema->AddMessage(kPrograms, &table);

		BMessage name;
		name.AddString(kLabel, "Program");
		name.AddBool(kReadOnly, true);
		name.AddInt32(kOrder, 0);
		schema->AddMessage("programs.name", &name);

		BMessage signature;
		signature.AddBool(kHidden, true);
		schema->AddMessage("programs.program", &signature);

		BMessage allowed;
		allowed.AddString(kLabel, "Allowed");
		allowed.AddString(kHelp, "Whether this program may know where the computer is at all.");
		allowed.AddInt32(kOrder, 1);
		schema->AddMessage("programs.allowed", &allowed);

		BMessage level;
		level.AddString(kLabel, "May know");
		level.AddString(kHelp, "The finest location this program is told. As the others: what is set above.");
		level.AddString(kKind, "choice");
		AddLevels(&level, true, "city", "As the others");
		level.AddString(kEnabledWhen, "allowed=true");
		level.AddInt32(kOrder, 2);
		schema->AddMessage("programs.level", &level);

		// the services that are asked: a box for each, in the order of the list; the first that answers wins
		BMessage services;
		services.AddString(kLabel, "Lookup services");
		services.AddString(kHelp, "The services that are asked, in this order, until one answers. At least one stays on. A service that answers over http only can be read on the way: it is off unless you switch it on.");
		services.AddString(kKind, "table");
		services.AddInt32(kOrder, 5);
		schema->AddMessage(kServices, &services);

		BMessage use;
		use.AddString(kLabel, "Use");
		use.AddInt32(kOrder, 0);
		schema->AddMessage("services.enabled", &use);

		BMessage serviceName;
		serviceName.AddString(kLabel, "Service");
		serviceName.AddBool(kReadOnly, true);
		serviceName.AddInt32(kOrder, 1);
		schema->AddMessage("services.name", &serviceName);

		BMessage serviceId;
		serviceId.AddBool(kHidden, true);
		schema->AddMessage("services.id", &serviceId);

		AddNumberField(schema, "interval_minutes", "Look again every (minutes)", "Also when the network changes.", 5, 1440, 6);
		AddNumberField(schema, "min_move_km", "The same place within (km)", "A new fix within this distance of the announced one is the same place.", 1, 500, 7);
		AddNumberField(schema, "confirmations", "Lookups that must agree on a move", "A move is announced when this many lookups in a row say the same.", 1, 5, 8);
		AddNumberField(schema, "min_refresh_seconds", "Lookups at most every (seconds)", "However often a program asks.", 10, 3600, 9);
	}

	// the defaults: what Defaults puts back (every program without a limit of its own)
	if (defaults != NULL) {
		Settings withoutRules;
		FillValues(withoutRules, seen, defaults, NULL, place);
		// (the programs that are only in the rules, not seen, are listed too, with no limit)
		for (const Settings::ProgramRule& rule : settings.programs) {
			bool listed = false;
			BMessage row;
			for (int32 i = 0; defaults->FindMessage(kPrograms, i, &row) == B_OK; i++)
				listed = listed || row.GetString("program", "") == rule.signature;
			if (!listed)
				AddProgramRow(defaults, withoutRules, rule.signature, NameOf(rule.signature));
		}
	}
}


// #pragma mark - changes


static bool
IntValue(const BMessage& changes, const char* name, int32* value)
{
	type_code type;
	if (changes.GetInfo(name, &type) != B_OK)
		return false;
	if (type == B_INT32_TYPE)
		return changes.FindInt32(name, value) == B_OK;
	if (type == B_INT64_TYPE) {
		int64 wide;
		if (changes.FindInt64(name, &wide) != B_OK)
			return false;
		*value = (int32)wide;
		return true;
	}
	if (type == B_DOUBLE_TYPE) {
		double wide;
		if (changes.FindDouble(name, &wide) != B_OK)
			return false;
		*value = (int32)std::lround(wide);
		return true;
	}
	return false;
}


// the level of a program: `default` takes the rule away
static bool
ApplyProgram(Settings* settings, const std::string& signature, const char* level)
{
	if (signature.empty() || level == NULL)
		return false;
	if (std::string(level) == kProgramDefault) {
		settings->RemoveProgram(signature);
		return true;
	}
	Granularity limit;
	if (!ParseGranularity(level, &limit))
		return false;
	settings->SetProgram(signature, limit);
	return true;
}


// The cell of the table that a path key names: `programs_03.level` (row 3, column `level`), or `programs.level` when there is a single row.
static bool
ParseCellKey(const std::string& name, const char* table, int32* row, std::string* column)
{
	const size_t length = strlen(table);
	if (name.compare(0, length, table) != 0)
		return false;
	size_t at = length;
	*row = 0;
	if (at < name.size() && name[at] == '_') {
		at++;
		const size_t digits = name.find('.', at);
		if (digits == std::string::npos || digits == at || digits - at > 9)
			return false;
		for (size_t i = at; i < digits; i++) {
			if (name[i] < '0' || name[i] > '9')
				return false;
		}
		*row = (int32)strtol(name.substr(at, digits - at).c_str(), NULL, 10);
		at = digits;
	}
	if (at >= name.size() || name[at] != '.' || at + 1 >= name.size())
		return false;
	*column = name.substr(at + 1);
	return true;
}


// a program may know nothing: its rule says `none`
static bool
IsRefused(const Settings* settings, const std::string& signature)
{
	const Settings::ProgramRule* rule = settings->FindProgram(signature);
	return rule != NULL && rule->limit.IsNone();
}


static bool
ApplyAllowed(Settings* settings, const std::string& signature, bool allowed)
{
	if (signature.empty())
		return false;
	if (!allowed) {
		Granularity none;
		ParseGranularity("none", &none);
		settings->SetProgram(signature, none);
	} else if (IsRefused(settings, signature)) {
		settings->RemoveProgram(signature);		// (it is as the others again)
	}
	return true;
}


status_t
ApplySettingsChanges(Settings* settings, const BMessage& changes, std::vector<std::string>* refused, const std::vector<std::string>& rows)
{
	bool allValid = true;
	auto refuse = [&](const std::string& name) {
		allValid = false;
		if (refused != NULL)
			refused->push_back(name);
	};

	std::set<std::string> sentPrograms;
	// the lookup services that are on: changed by a cell or by the whole table, and written once at the end (in the order of the list)
	std::set<std::string> services;
	for (const std::string& id : settings->ProviderIds())
		services.insert(id);
	bool servicesChanged = false;
	char* rawName;
	type_code type;
	int32 count;
	for (int32 i = 0; changes.GetInfo(B_ANY_TYPE, i, &rawName, &type, &count) == B_OK; i++) {
		const std::string name = rawName;

		if (name == "network_lookup") {
			const char* value = changes.GetString(rawName, "");
			if (std::string(value) == "ask")
				settings->networkLookup = Settings::kAsk;
			else if (std::string(value) == "allow")
				settings->networkLookup = Settings::kAllow;
			else if (std::string(value) == "never")
				settings->networkLookup = Settings::kNever;
			else
				refuse(name);
		} else if (name == "max_precision") {
			Granularity level;
			if (ParseGranularity(changes.GetString(rawName, ""), &level) && !level.IsNone())
				settings->maxPrecision = level;
			else
				refuse(name);
		} else if (name == "providers") {
			std::vector<std::string> ids;
			bool valid = true;
			const char* id;
			for (int32 v = 0; changes.FindString(rawName, v, &id) == B_OK; v++) {
				if (FindProvider(id) == NULL)
					valid = false;
				else
					ids.push_back(id);
			}
			if (valid)
				settings->providers = ids;
			else
				refuse(name);
		} else if (name == "interval_minutes" || name == "min_move_km" || name == "confirmations" || name == "min_refresh_seconds") {
			int32 value;
			int32 low = 1, high = 1;
			if (name == "interval_minutes") { low = 5; high = 1440; }
			else if (name == "min_move_km") { low = 1; high = 500; }
			else if (name == "confirmations") { low = 1; high = 5; }
			else { low = 10; high = 3600; }
			if (!IntValue(changes, rawName, &value) || value < low || value > high) {
				refuse(name);
			} else if (name == "interval_minutes") {
				settings->intervalMinutes = value;
			} else if (name == "min_move_km") {
				settings->minMoveKm = value;
			} else if (name == "confirmations") {
				settings->confirmations = value;
			} else {
				settings->minRefreshSeconds = value;
			}
		} else if (name == kShowLocation || name == kWhere) {
			// (a button and a text that the server makes: nothing to store; the server looks at the button before)
		} else if (name == "services" && type == B_MESSAGE_TYPE) {
			// the whole table (Revert, Defaults): the services that are on
			services.clear();
			servicesChanged = true;
			for (int32 v = 0; v < count; v++) {
				BMessage row;
				if (changes.FindMessage(rawName, v, &row) != B_OK)
					continue;
				const std::string id = row.GetString("id", "");
				if (FindProvider(id) == NULL)
					refuse(name);
				else if (row.GetBool("enabled", false))
					services.insert(id);
			}
		} else if (name.compare(0, strlen(kServices), kServices) == 0) {
			// a cell of the table (`services_01.enabled`): the row is the place in the list of the services
			int32 row = 0;
			std::string column;
			bool on = true;
			if (!ParseCellKey(name, kServices, &row, &column) || column != "enabled" || row < 0 || row >= (int32)AllProviders().size()
					|| changes.FindBool(rawName, &on) != B_OK) {
				refuse(name);
				continue;
			}
			const std::string id = AllProviders()[row].id;
			if (on)
				services.insert(id);
			else
				services.erase(id);
			servicesChanged = true;
		} else if (name == kPrograms && type == B_MESSAGE_TYPE) {
			// the whole table (Revert, Defaults): every row says what its program may know; a program that is not in it has no limit of its own any more
			for (int32 v = 0; v < count; v++) {
				BMessage row;
				if (changes.FindMessage(rawName, v, &row) != B_OK)
					continue;
				const std::string signature = row.GetString("program", "");
				sentPrograms.insert(signature);
				bool valid = !signature.empty();
				if (valid && !row.GetBool("allowed", true))
					valid = ApplyAllowed(settings, signature, false);
				else if (valid)
					valid = ApplyProgram(settings, signature, row.GetString("level", kProgramDefault));
				if (!valid)
					refuse(name);
			}
		} else if (name.compare(0, strlen(kPrograms), kPrograms) == 0) {
			// a cell of the table (`programs_03.level`): the row is the number that it had in the last description
			int32 row = 0;
			std::string column;
			if (!ParseCellKey(name, kPrograms, &row, &column) || row < 0 || row >= (int32)rows.size()) {
				refuse(name);
				continue;
			}
			const std::string& signature = rows[row];
			sentPrograms.insert(signature);
			bool valid = false;
			bool on = true;
			if (column == "allowed" && changes.FindBool(rawName, &on) == B_OK)
				valid = ApplyAllowed(settings, signature, on);
			else if (column == "level")
				valid = ApplyProgram(settings, signature, changes.GetString(rawName, ""));
			if (!valid)
				refuse(name);
		} else if (!SignatureOfField(name).empty()) {
			const std::string signature = SignatureOfField(name);
			sentPrograms.insert(signature);
			if (!ApplyProgram(settings, signature, changes.GetString(rawName, "")))
				refuse(name);
		}
		// (a field that is none of these is not a setting of this server: left alone)
	}
	if (servicesChanged) {
		std::vector<std::string> ordered;
		for (const Provider& provider : AllProviders()) {
			if (services.count(provider.id) != 0)
				ordered.push_back(provider.id);
		}
		if (ordered.empty())
			refuse("services (one has to stay on)");
		else
			settings->providers = ordered;
	}
	// the whole message (Revert, Defaults): a program that it does not name has no limit of its own any more
	if (changes.HasString("network_lookup") && changes.HasString("max_precision")) {
		std::vector<std::string> before;
		for (const Settings::ProgramRule& rule : settings->programs)
			before.push_back(rule.signature);
		for (const std::string& signature : before) {
			if (sentPrograms.count(signature) == 0)
				settings->RemoveProgram(signature);
		}
	}
	return allValid ? B_OK : B_BAD_VALUE;
}

}	// namespace basho
