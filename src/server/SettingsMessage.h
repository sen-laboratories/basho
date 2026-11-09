/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include "Settings.h"

#include <SupportDefs.h>

#include <map>
#include <string>
#include <vector>

class BMessage;

/**
 * The settings as a message, for a program that shows them (the settings window of SEN, Shoji): the values, a schema (labels, choices, ranges) and the defaults, in the format of the
 * settings protocol of SEN (docs/spec-settings-and-message-template.md in the sento repository, section 2 to 6): a field per setting, and the array of messages `programs` for the programs
 * that have a limit or have asked, shown as a table (`kind = table` in the schema): a row for each program with its `name`, its `program` (the signature, hidden), `allowed` (whether it may
 * know where the computer is at all) and `level` (what it may know, `default` if it has no limit of its own). The path key of a cell is `programs_03.level`: the row is the number it had in the last
 * description. A single field `program:<signature>` (the dots of the signature written as %2E) is accepted as a change, too: it is what the client class and the tool send. Basho does not
 * need SEN for this: the format is a message, and anyone can read it.
 */
namespace basho {

/** A program that has asked for the location: its signature and its name (the file name, for the form). */
struct SeenProgram {
	std::string	signature;
	std::string	name;
};

/** What the name of the field of a program starts with (then its signature, coded: ProgramKey.h). */
extern const char* const kProgramPrefix;
/** The value of a program that has no limit of its own. */
extern const char* const kProgramDefault;

/** Fills the three messages. `seen` are the programs that have asked (they are listed with the rules, so that a limit can be set for a program that has not got one yet). `rows` gets the signatures
 *  of the programs in the order of the rows of the table: the change of a cell names its row by the number in this list. */
void	DescribeSettings(const Settings& settings, const std::vector<SeenProgram>& seen, BMessage* values, BMessage* schema, BMessage* defaults, std::vector<std::string>* rows = NULL);

/**
 * Applies changes: a flat message of fields under their path keys (`max_precision`, `programs_03.level`, `program:<signature>`), or the whole settings message (after Revert and Defaults). Valid changes are applied to `settings`, the names of those that are not go to `refused`. Returns B_OK if all were valid, else B_BAD_VALUE.
 * `rows` are the signatures of the table as the last description gave them (a cell is named by its row number).
 */
status_t	ApplySettingsChanges(Settings* settings, const BMessage& changes, std::vector<std::string>* refused, const std::vector<std::string>& rows = std::vector<std::string>());

/** The name of a program's field, and its path key (the signature with its dots and backslashes escaped), and the signature of a field name or path key; "" if it is none of a program. */
std::string					ProgramFieldName(const std::string& signature);
std::string					ProgramPathKey(const std::string& signature);
std::string					SignatureOfField(const std::string& nameOrPathKey);

}	// namespace basho
