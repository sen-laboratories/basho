/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#ifndef _LOCATION_MESSAGE_H_
#define _LOCATION_MESSAGE_H_

#include "Fix.h"

class BMessage;

/** The fix as the fields of Location.h (`stale`: see there). A fix without a position (country level) has no latitude, longitude and accuracy. `granularity` is the level that it was made coarser to. */
void	FixToMessage(const basho::Fix& fix, bool stale, const char* granularity, BMessage* message);
/** The fields back to a fix: with a position if the message has one, else (country level) with the country only. False if the message has neither a usable position nor a country. */
bool	MessageToFix(const BMessage* message, basho::Fix* fix);

#endif
