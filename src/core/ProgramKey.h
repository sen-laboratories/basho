/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <cstring>
#include <string>

namespace basho {

/**
 * How a program is named in a field of the settings message. The form of Shoji does not find a field whose name has a dot in it (a dot separates the names of nested messages in a path key),
 * and a signature has dots, so a dot is written as %2E in the name (and a backslash as %5C, a percent sign as %25). The fields are `program:<coded signature>` (the level of the limit) and
 * `programname:<coded signature>` (the name for people). Header only: the server, the settings view and the tool use it.
 */
inline const char*	ProgramPrefix() { return "program:"; }
inline const char*	ProgramNamePrefix() { return "programname:"; }

inline std::string
EncodeProgramKey(const std::string& signature)
{
	std::string encoded;
	for (char c : signature) {
		if (c == '.')
			encoded += "%2E";
		else if (c == '\\')
			encoded += "%5C";
		else if (c == '%')
			encoded += "%25";
		else
			encoded += c;
	}
	return encoded;
}

/** The signature of a coded name (what comes after the prefix); a backslash escape of the form `\.` (a path key that was escaped by the spec) is read, too. */
inline std::string
DecodeProgramKey(const std::string& encoded, size_t from = 0)
{
	std::string signature;
	for (size_t i = from; i < encoded.size(); i++) {
		if (encoded[i] == '%' && i + 2 < encoded.size() + 1 && i + 2 <= encoded.size() - 1) {
			const std::string code = encoded.substr(i + 1, 2);
			if (code == "2E" || code == "2e") { signature += '.'; i += 2; continue; }
			if (code == "5C" || code == "5c") { signature += '\\'; i += 2; continue; }
			if (code == "25") { signature += '%'; i += 2; continue; }
		}
		if (encoded[i] == '\\' && i + 1 < encoded.size()) {
			signature += encoded[++i];
			continue;
		}
		signature += encoded[i];
	}
	return signature;
}

/** The signature of a field name `program:<coded>`, "" if the name is none of a program (`programname:` is not). */
inline std::string
SignatureOfProgramField(const std::string& name)
{
	const size_t prefix = strlen(ProgramPrefix());
	if (name.compare(0, prefix, ProgramPrefix()) != 0)
		return std::string();
	return DecodeProgramKey(name, prefix);
}

/** The signature of a field name `programname:<coded>`, "" if it is none. */
inline std::string
SignatureOfNameField(const std::string& name)
{
	const size_t prefix = strlen(ProgramNamePrefix());
	if (name.compare(0, prefix, ProgramNamePrefix()) != 0)
		return std::string();
	return DecodeProgramKey(name, prefix);
}

}	// namespace basho
