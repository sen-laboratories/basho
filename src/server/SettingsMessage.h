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
 * settings protocol of SEN (docs/spec-settings-and-message-template.md in the sento repository, section 2 to 6): a field per setting, a group `programs` with a field for every program
 * (the name of the field is its signature, the value the level of its limit, `default` if it has none), and path keys for changes (`programs.application/x-vnd\.some_app`, the dots of
 * a name escaped with a backslash). Basho does not need SEN for this: the format is a message, and anyone can read it.
 */
namespace basho {

/** A program that has asked for the location: its signature and its name (the file name, for the form). */
struct SeenProgram {
	std::string	signature;
	std::string	name;
};

/** The name of the group of programs in the settings message. */
extern const char* const kProgramsField;
/** The value of a program that has no limit of its own. */
extern const char* const kProgramDefault;

/** Fills the three messages. `seen` are the programs that have asked (they are listed with the rules, so that a limit can be set for a program that has not got one yet). */
void	DescribeSettings(const Settings& settings, const std::vector<SeenProgram>& seen, BMessage* values, BMessage* schema, BMessage* defaults);

/**
 * Applies changes: a flat message of fields under their path keys (`max_precision`, `programs.<signature>`), or the whole settings message (after Revert and Defaults: the group `programs`
 * as a message). Valid changes are applied to `settings`, the names of those that are not go to `refused`. Returns B_OK if all were valid, else B_BAD_VALUE.
 */
status_t	ApplySettingsChanges(Settings* settings, const BMessage& changes, std::vector<std::string>* refused);

/** The path key of a program's field (`programs.` and the signature with its dots escaped), and the parts of a path key (a segment is read up to a dot that is not escaped). */
std::string					ProgramPathKey(const std::string& signature);
std::vector<std::string>	SplitPathKey(const std::string& key);

}	// namespace basho

#endif
