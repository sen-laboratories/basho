/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

// The table of the programs in the settings message, through the protocol of the settings window (needs the running server; tests/vm/server.sh builds and runs it):
// the description (array `programs`, schema `kind = table`), a cell changed by its row number, a wrong change refused, the whole message after Revert and Defaults.

#include <location/LocationClient.h>

#include <Application.h>
#include <Message.h>

#include <stdio.h>
#include <string.h>

#include <string>

static int sFailures = 0;
#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			sFailures++; \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		} \
	} while (0)

// (the program is this test itself: it has asked where the computer is, so it is listed also while it has no rule; the other one has a rule only)
static const char* kProgram = "application/x-vnd.basho-settings-table-test";
static const char* kOther = "application/x-vnd.basho-test.other";

// the row of a program in the settings, -1 if it has none; fills `row`
static int32
Find(LocationClient& client, const char* signature, BMessage* row)
{
	BMessage settings;
	if (client.GetSettings(&settings) != B_OK)
		return -1;
	BMessage each;
	for (int32 i = 0; settings.FindMessage("programs", i, &each) == B_OK; i++) {
		if (strcmp(each.GetString("program", ""), signature) == 0) {
			if (row != NULL)
				*row = each;
			return i;
		}
	}
	return -1;
}

static status_t
Set(LocationClient& client, const char* path, const char* text)
{
	BMessage changes;
	changes.AddString(path, text);
	return client.ApplySettings(changes);
}

static status_t
Allow(LocationClient& client, int32 row, bool on)
{
	char path[64];
	snprintf(path, sizeof(path), "programs_%02d.allowed", (int)row);
	BMessage changes;
	changes.AddBool(path, on);
	return client.ApplySettings(changes);
}

int main()
{
	BApplication app("application/x-vnd.basho-settings-table-test");
	LocationClient client;

	// two programs, so that the table has rows: this one asks (and is listed from then on), the other has a rule
	BMessage ignored;
	client.GetStatus(&ignored);
	double latitude, longitude;
	client.GetCurrentLocation(&latitude, &longitude);
	CHECK(client.SetProgramLimit(kProgram, "region") == B_OK);
	CHECK(client.SetProgramLimit(kOther, "country") == B_OK);

	BMessage settings, schema, defaults;
	CHECK(client.GetSettings(&settings, &schema, &defaults) == B_OK);
	BMessage table, name, program, allowed, level;
	CHECK(schema.FindMessage("programs", &table) == B_OK && strcmp(table.GetString("kind", ""), "table") == 0);
	CHECK(schema.FindMessage("programs.name", &name) == B_OK && name.GetBool("readonly", false));
	CHECK(schema.FindMessage("programs.program", &program) == B_OK && program.GetBool("hidden", false));
	CHECK(schema.FindMessage("programs.allowed", &allowed) == B_OK);
	CHECK(schema.FindMessage("programs.level", &level) == B_OK && strcmp(level.GetString("enabled_when", ""), "allowed=true") == 0);
	CHECK(level.HasString("choices") && strcmp(level.GetString("choices", 0, ""), "default") == 0);

	BMessage row;
	const int32 index = Find(client, kProgram, &row);
	CHECK(index >= 0);
	CHECK(strcmp(row.GetString("level", ""), "region") == 0 && row.GetBool("allowed", false));
	CHECK(row.HasString("name") && row.GetString("name", "")[0] != '\0');
	const int32 other = Find(client, kOther, NULL);
	CHECK(other >= 0 && other != index);
	CHECK(!defaults.IsEmpty());

	char path[64];
	snprintf(path, sizeof(path), "programs_%02d.level", (int)index);

	// a cell by its row number
	CHECK(Set(client, path, "city") == B_OK);
	CHECK(Find(client, kProgram, &row) == index && strcmp(row.GetString("level", ""), "city") == 0);
	CHECK(Set(client, path, "default") == B_OK);
	CHECK(Find(client, kProgram, &row) == index && strcmp(row.GetString("level", ""), "default") == 0 && row.GetBool("allowed", false));
	CHECK(Set(client, path, "20") == B_OK);
	CHECK(Find(client, kProgram, &row) == index && strcmp(row.GetString("level", ""), "20") == 0);

	// the box: refused, the level shows `default`, allowed again takes the program back as the others
	CHECK(Allow(client, index, false) == B_OK);
	CHECK(Find(client, kProgram, &row) == index && !row.GetBool("allowed", true) && strcmp(row.GetString("level", ""), "default") == 0);
	CHECK(client.SetProgramLimit(kProgram, "none") == B_OK);						// (the same through the other way)
	CHECK(Find(client, kProgram, &row) == index && !row.GetBool("allowed", true));
	CHECK(Allow(client, index, true) == B_OK);
	CHECK(Find(client, kProgram, &row) == index && row.GetBool("allowed", false) && strcmp(row.GetString("level", ""), "default") == 0);
	CHECK(Set(client, path, "country") == B_OK);

	// what is wrong is refused, the rest stays
	CHECK(Set(client, path, "nonsense") == B_BAD_VALUE);
	CHECK(Set(client, "programs_99.level", "city") == B_BAD_VALUE);
	CHECK(Set(client, "programs_00.colour", "red") == B_BAD_VALUE);
	{
		BMessage text;
		text.AddString("programs_00.allowed", "false");								// (a string is no bool)
		CHECK(client.ApplySettings(text) == B_BAD_VALUE);
	}
	CHECK(Find(client, kProgram, &row) == index && strcmp(row.GetString("level", ""), "country") == 0);

	// the whole message (Revert, Defaults): a row says what its program may know, a program that is not in it has no limit any more
	client.GetSettings(&settings);
	BMessage changed;
	{
		BMessage each;
		for (int32 i = 0; settings.FindMessage("programs", i, &each) == B_OK; i++) {
			if (i == index) {
				each.ReplaceBool("allowed", 0, false);
			}
			if (i != other)
				changed.AddMessage("programs", &each);
		}
		// (a whole message has the settings that tell it from a change of a cell)
		changed.AddString("network_lookup", settings.GetString("network_lookup", "never"));
		changed.AddString("max_precision", settings.GetString("max_precision", "city"));
	}
	CHECK(client.ApplySettings(changed) == B_OK);
	CHECK(Find(client, kProgram, &row) >= 0 && !row.GetBool("allowed", true));
	CHECK(Find(client, kOther, NULL) < 0);										// (left out: no limit any more, and it never asked: no row)

	// clean up
	client.SetProgramLimit(kProgram, "default");

	printf(sFailures == 0 ? "settings table ok\n" : "settings table: %d failure(s)\n", sFailures);
	return sFailures == 0 ? 0 : 1;
}
