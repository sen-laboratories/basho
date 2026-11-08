/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#include "LocationMessage.h"

#include <Message.h>

void
FixToMessage(const basho::Fix& fix, bool stale, const char* granularity, BMessage* message)
{
	if (fix.hasPosition) {
		message->AddDouble("latitude", fix.latitude);
		message->AddDouble("longitude", fix.longitude);
		message->AddFloat("accuracy", fix.accuracy);
	}
	message->AddString("city", fix.city.c_str());
	message->AddString("region", fix.region.c_str());
	message->AddString("country", fix.country.c_str());
	if (!fix.timezone.empty())
		message->AddString("timezone", fix.timezone.c_str());
	message->AddInt64("timestamp", fix.timestamp);
	message->AddString("provider", fix.provider.c_str());
	message->AddBool("stale", stale);
	message->AddBool("manual", fix.manual);
	message->AddString("granularity", granularity);
}

bool
MessageToFix(const BMessage* message, basho::Fix* fix)
{
	basho::Fix f;
	f.hasPosition = message->FindDouble("latitude", &f.latitude) == B_OK && message->FindDouble("longitude", &f.longitude) == B_OK;
	f.accuracy = message->GetFloat("accuracy", 0.0f);
	f.city = message->GetString("city", "");
	f.region = message->GetString("region", "");
	f.country = message->GetString("country", "");
	f.timezone = message->GetString("timezone", "");
	f.provider = message->GetString("provider", "");
	f.timestamp = message->GetInt64("timestamp", 0);
	f.manual = message->GetBool("manual", false);
	if (!f.Valid())
		return false;
	*fix = f;
	return true;
}
