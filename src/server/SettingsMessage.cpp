/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#include "SettingsMessage.h"

#include "Providers.h"

#include <Message.h>

#include <algorithm>
#include <cmath>

namespace basho {

const char* const kProgramsField = "programs";
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


std::string
ProgramPathKey(const std::string& signature)
{
	std::string key = std::string(kProgramsField) + ".";
	for (char c : signature) {
		if (c == '.' || c == '\\')
			key += '\\';
		key += c;
	}
	return key;
}


std::vector<std::string>
SplitPathKey(const std::string& key)
{
	std::vector<std::string> parts(1);
	for (size_t i = 0; i < key.size(); i++) {
		if (key[i] == '\\' && i + 1 < key.size()) {
			parts.back() += key[++i];
		} else if (key[i] == '.') {
			parts.emplace_back();
		} else {
			parts.back() += key[i];
		}
	}
	return parts;
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
	if (forProgram) {
		attributes->AddString(kChoices, "none");
		attributes->AddString(kChoiceLabels, "Nothing: it may not know where I am");
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


static void
FillValues(const Settings& settings, const std::vector<SeenProgram>& seen, BMessage* values, BMessage* schema)
{
	values->AddString("network_lookup", NetworkLookupName(settings.networkLookup));
	values->AddString("max_precision", GranularityName(settings.maxPrecision).c_str());
	for (const std::string& id : settings.ProviderIds())
		values->AddString("providers", id.c_str());
	values->AddInt32("interval_minutes", settings.intervalMinutes);
	values->AddInt32("min_move_km", (int32)std::lround(settings.minMoveKm));
	values->AddInt32("confirmations", settings.confirmations);
	values->AddInt32("min_refresh_seconds", settings.minRefreshSeconds);

	// the programs: those that have a rule, and those that have asked
	BMessage programs;
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
	int32 order = 0;
	for (const auto& program : listed) {
		const Settings::ProgramRule* rule = settings.FindProgram(program.first);
		const std::string level = rule != NULL ? GranularityName(rule->limit) : std::string(kProgramDefault);
		programs.AddString(program.first.c_str(), level.c_str());
		if (schema != NULL) {
			BMessage attributes;
			attributes.AddString(kLabel, program.second.c_str());
			attributes.AddString(kHelp, program.first.c_str());
			attributes.AddString(kKind, "choice");
			AddLevels(&attributes, true, level, ("As for the others (" + std::string(GranularityName(settings.maxPrecision)) + ")").c_str());
			attributes.AddInt32(kOrder, order++);
			schema->AddMessage(ProgramPathKey(program.first).c_str(), &attributes);
		}
	}
	values->AddMessage(kProgramsField, &programs);
}


void
DescribeSettings(const Settings& settings, const std::vector<SeenProgram>& seen, BMessage* values, BMessage* schema, BMessage* defaults)
{
	// the values (and the fields of the programs in the schema, which come from the same list)
	BMessage scratch;
	FillValues(settings, seen, values != NULL ? values : &scratch, schema);

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

		BMessage precision;
		precision.AddString(kLabel, "Programs may know");
		precision.AddString(kHelp, "The finest location that programs are told, unless they have a limit of their own (below). A program can ask for less than this, never for more. The server keeps the finest fix it has.");
		precision.AddString(kKind, "choice");
		AddLevels(&precision, false, GranularityName(settings.maxPrecision), "");
		precision.AddInt32(kOrder, 1);
		schema->AddMessage("max_precision", &precision);

		BMessage programs;
		programs.AddString(kLabel, "Programs");
		programs.AddString(kHelp, "The programs that have asked where this computer is, and what each may be told. A program that is not here yet shows up after its first request.");
		programs.AddInt32(kOrder, 2);
		schema->AddMessage(kProgramsField, &programs);

		BMessage providers;
		providers.AddString(kLabel, "Lookup services");
		providers.AddString(kHelp, "In this order, until one answers: ipwho.is, geojs, ipinfo (https), ip-api (http only).");
		providers.AddBool(kList, true);
		providers.AddInt32(kOrder, 3);
		schema->AddMessage("providers", &providers);

		AddNumberField(schema, "interval_minutes", "Look again every (minutes)", "Also when the network changes.", 5, 1440, 4);
		AddNumberField(schema, "min_move_km", "The same place within (km)", "A new fix within this distance of the announced one is the same place.", 1, 500, 5);
		AddNumberField(schema, "confirmations", "Lookups that must agree on a move", "A move is announced when this many lookups in a row say the same.", 1, 5, 6);
		AddNumberField(schema, "min_refresh_seconds", "Lookups at most every (seconds)", "However often a program asks.", 10, 3600, 7);
	}

	// the defaults: what Defaults puts back (every program without a limit of its own)
	if (defaults != NULL) {
		Settings withoutRules;
		FillValues(withoutRules, seen, defaults, NULL);
		// (the programs that are only in the rules, not seen, are listed too, with no limit)
		BMessage programs;
		defaults->FindMessage(kProgramsField, &programs);
		for (const Settings::ProgramRule& rule : settings.programs) {
			if (!programs.HasString(rule.signature.c_str()))
				programs.AddString(rule.signature.c_str(), kProgramDefault);
		}
		defaults->RemoveName(kProgramsField);
		defaults->AddMessage(kProgramsField, &programs);
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


status_t
ApplySettingsChanges(Settings* settings, const BMessage& changes, std::vector<std::string>* refused)
{
	bool allValid = true;
	auto refuse = [&](const std::string& name) {
		allValid = false;
		if (refused != NULL)
			refused->push_back(name);
	};

	char* rawName;
	type_code type;
	int32 count;
	for (int32 i = 0; changes.GetInfo(B_ANY_TYPE, i, &rawName, &type, &count) == B_OK; i++) {
		const std::string name = rawName;
		const std::vector<std::string> path = SplitPathKey(name);

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
		} else if (name == kProgramsField && type == B_MESSAGE_TYPE) {
			// the whole group (after Revert or Defaults): the rules are what it says
			BMessage group;
			changes.FindMessage(rawName, &group);
			std::vector<std::string> before;
			for (const Settings::ProgramRule& rule : settings->programs)
				before.push_back(rule.signature);
			char* programName;
			type_code programType;
			for (int32 p = 0; group.GetInfo(B_STRING_TYPE, p, &programName, &programType) == B_OK; p++) {
				if (!ApplyProgram(settings, programName, group.GetString(programName, "")))
					refuse(std::string(kProgramsField) + "." + programName);
			}
			// (a rule for a program that is not in the group any more is gone)
			for (const std::string& signature : before) {
				if (!group.HasString(signature.c_str()))
					settings->RemoveProgram(signature);
			}
		} else if (path.size() == 2 && path[0] == kProgramsField) {
			if (!ApplyProgram(settings, path[1], changes.GetString(rawName, "")))
				refuse(name);
		}
		// (a field that is none of these is not a setting of this server: left alone)
	}
	return allValid ? B_OK : B_BAD_VALUE;
}

}	// namespace basho
