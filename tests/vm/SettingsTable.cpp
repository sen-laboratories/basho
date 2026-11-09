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
#include <vector>

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
	if (client.GetSettings(&settings, NULL, NULL, "access") != B_OK)
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
	CHECK(client.GetSettings(&settings, &schema, &defaults, "access") == B_OK);
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
	client.GetSettings(&settings, NULL, NULL, "access");
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
		// (the whole page of the programs is the table and nothing else)
	}
	CHECK(client.ApplySettings(changed) == B_OK);
	CHECK(Find(client, kProgram, &row) >= 0 && !row.GetBool("allowed", true));
	CHECK(Find(client, kOther, NULL) < 0);										// (left out: no limit any more, and it never asked: no row)

	// the lookup services: a table with a box for each, in a fixed order; one stays on; the button and the text of the place are in the description
	{
		BMessage now, schema2, ignoredDefaults;
		CHECK(client.GetSettings(&now, &schema2, &ignoredDefaults) == B_OK);
		// the pages: the first has no programs, the page `access` has only them (the table is in its schema also while it has no rows)
		CHECK(!now.HasMessage("programs") && !schema2.HasMessage("programs"));
		BMessage accessValues, accessSchema, lookupField;
		CHECK(client.GetSettings(&accessValues, &accessSchema, NULL, "access") == B_OK);
		CHECK(accessSchema.HasMessage("programs") && accessSchema.HasMessage("programs.level") && !accessValues.HasString("network_lookup") && !accessSchema.HasMessage("services"));
		CHECK(schema2.FindMessage("network_lookup", &lookupField) == B_OK && strcmp(lookupField.GetString("label", ""), "Allow accessing my location?") == 0);
		BMessage infoColumn, nameColumn;
		CHECK(schema2.FindMessage("services.info", &infoColumn) == B_OK && infoColumn.GetBool("readonly", false));
		CHECK(schema2.FindMessage("services.name", &nameColumn) == B_OK && strcmp(nameColumn.GetString("busy_field", ""), "busy") == 0);
		BMessage firstService;
		CHECK(now.FindMessage("services", 0, &firstService) == B_OK && strlen(firstService.GetString("info", "")) > 0 && firstService.HasBool("busy"));
		BMessage services, use, serviceName, button, where;
		CHECK(schema2.FindMessage("services", &services) == B_OK && strcmp(services.GetString("kind", ""), "table") == 0);
		CHECK(schema2.FindMessage("services.enabled", &use) == B_OK);
		CHECK(schema2.FindMessage("services.name", &serviceName) == B_OK && serviceName.GetBool("readonly", false));
		CHECK(schema2.FindMessage("show_location", &button) == B_OK && strcmp(button.GetString("kind", ""), "button") == 0);
		CHECK(schema2.FindMessage("where", &where) == B_OK && where.GetBool("readonly", false));
		CHECK(now.HasString("where") && !now.GetBool("show_location", true));
		int32 rows = 0, on = 0;
		BMessage each;
		for (; now.FindMessage("services", rows, &each) == B_OK; rows++) {
			CHECK(each.HasString("name") && each.HasString("id"));
			on += each.GetBool("enabled", false) ? 1 : 0;
		}
		CHECK(rows >= 3 && on >= 1);

		// switching off every one is refused (the last that is on stays); switching one off and on again works
		BMessage first;
		now.FindMessage("services", 0, &first);
		const bool wasOn = first.GetBool("enabled", false);
		BMessage flip;
		flip.AddBool("services_00.enabled", !wasOn);
		const status_t flipped = client.ApplySettings(flip);
		client.GetSettings(&now);
		now.FindMessage("services", 0, &first);
		CHECK(flipped == B_OK && first.GetBool("enabled", false) == !wasOn);
		BMessage back;
		back.AddBool("services_00.enabled", wasOn);
		CHECK(client.ApplySettings(back) == B_OK);

		BMessage allOff;
		for (int32 i = 0; i < rows; i++) {
			char key[64];
			snprintf(key, sizeof(key), "services_%02d.enabled", (int)i);
			allOff.AddBool(key, false);
		}
		CHECK(client.ApplySettings(allOff) == B_BAD_VALUE);
		client.GetSettings(&now);
		on = 0;
		for (int32 i = 0; now.FindMessage("services", i, &each) == B_OK; i++)
			on += each.GetBool("enabled", false) ? 1 : 0;
		CHECK(on >= 1);
		CHECK(Set(client, "services_99.enabled", "x") == B_BAD_VALUE);

		// a service is moved (the form sends the whole message with the rows in the new order): the order is kept, and a toggle by the row of the new order hits the right service
		client.GetSettings(&now);
		std::vector<BMessage> order;
		for (int32 i = 0; now.FindMessage("services", i, &each) == B_OK; i++)
			order.push_back(each);
		BMessage reordered(now);
		reordered.RemoveName("services");
		for (size_t i = 1; i < order.size(); i++)
			reordered.AddMessage("services", &order[i]);
		reordered.AddMessage("services", &order[0]);							// (the first goes to the end)
		CHECK(client.ApplySettings(reordered) == B_OK);
		client.GetSettings(&now);
		CHECK(now.FindMessage("services", (int32)order.size() - 1, &each) == B_OK && strcmp(each.GetString("id", ""), order[0].GetString("id", "")) == 0);
		CHECK(now.FindMessage("services", 0, &each) == B_OK && strcmp(each.GetString("id", ""), order[1].GetString("id", "")) == 0);
		// (row 0 is now the second service: a cell of it changes that one)
		const bool secondWasOn = each.GetBool("enabled", false);
		BMessage second;
		second.AddBool("services_00.enabled", !secondWasOn);
		CHECK(client.ApplySettings(second) == B_OK);
		client.GetSettings(&now);
		CHECK(now.FindMessage("services", 0, &each) == B_OK && each.GetBool("enabled", false) == !secondWasOn);
		BMessage secondBack;
		secondBack.AddBool("services_00.enabled", secondWasOn);
		client.ApplySettings(secondBack);
		// (and back to the order it was: the first row to the end again, three times for four services)
		for (size_t i = 1; i < order.size(); i++) {
			client.GetSettings(&now);
			std::vector<BMessage> current;
			for (int32 k = 0; now.FindMessage("services", k, &each) == B_OK; k++)
				current.push_back(each);
			BMessage rotated(now);
			rotated.RemoveName("services");
			for (size_t k = 1; k < current.size(); k++)
				rotated.AddMessage("services", &current[k]);
			rotated.AddMessage("services", &current[0]);
			client.ApplySettings(rotated);
		}
		client.GetSettings(&now);
		CHECK(now.FindMessage("services", 0, &each) == B_OK && strcmp(each.GetString("id", ""), order[0].GetString("id", "")) == 0);
	}

	// clean up
	client.SetProgramLimit(kProgram, "default");

	printf(sFailures == 0 ? "settings table ok\n" : "settings table: %d failure(s)\n", sFailures);
	return sFailures == 0 ? 0 : 1;
}
