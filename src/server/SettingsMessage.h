/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#ifndef _BASHO_SETTINGS_MESSAGE_H_
#define _BASHO_SETTINGS_MESSAGE_H_

#include "Settings.h"

#include <SupportDefs.h>

#include <map>
#include <string>
#include <vector>

class BMessage;

/**
 * The settings as a message, for a program that shows them (the settings window of SEN, Shoji): the values, a schema (labels, choices, ranges) and the defaults, in the format of the
 * settings protocol of SEN (docs/spec-settings-and-message-template.md in the sento repository, section 2 to 6): a field per setting, and a field `program:<signature>` for every program
 * that has a limit or has asked (the value is the level of its limit, `default` if it has none), shown as a row of the form with the name of the program as its label. The path key of a
 * field is its name with the dots escaped by a backslash (`program:application/x-vnd\.some_app`), and changes come under path keys. Basho does not need SEN for this: the format is a
 * message, and anyone can read it.
 */
namespace basho {

/** A program that has asked for the location: its signature and its name (the file name, for the form). */
struct SeenProgram {
	std::string	signature;
	std::string	name;
};

/** What the name of the field of a program starts with (then its signature), and that of the field with its name for people (hidden in the generic form, the table of the settings view shows it). */
extern const char* const kProgramPrefix;
extern const char* const kProgramNamePrefix;
/** The value of a program that has no limit of its own. */
extern const char* const kProgramDefault;

/** Fills the three messages. `seen` are the programs that have asked (they are listed with the rules, so that a limit can be set for a program that has not got one yet). */
void	DescribeSettings(const Settings& settings, const std::vector<SeenProgram>& seen, BMessage* values, BMessage* schema, BMessage* defaults);

/**
 * Applies changes: a flat message of fields under their path keys (`max_precision`, `program:<signature>`), or the whole settings message (after Revert and Defaults). Valid changes are applied to `settings`, the names of those that are not go to `refused`. Returns B_OK if all were valid, else B_BAD_VALUE.
 */
status_t	ApplySettingsChanges(Settings* settings, const BMessage& changes, std::vector<std::string>* refused);

/** The name of a program's field, and its path key (the signature with its dots and backslashes escaped), and the signature of a field name or path key; "" if it is none of a program. */
std::string					ProgramFieldName(const std::string& signature);
std::string					ProgramNameFieldName(const std::string& signature);
std::string					ProgramPathKey(const std::string& signature);
std::string					SignatureOfField(const std::string& nameOrPathKey);

}	// namespace basho

#endif
