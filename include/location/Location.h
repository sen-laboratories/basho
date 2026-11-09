/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <SupportDefs.h>

/**
 * The protocol of the location server (Basho): where this machine is, roughly, for every program that wants to know. The server is a background application, nothing in it depends on SEN.
 * Programs talk to it with BMessages, or with the LocationClient of LocationClient.h that wraps them.
 *
 * A fix (the payload of LOCATION_CHANGED and LOCATION_CURRENT_REPLY):
 *   latitude   B_DOUBLE_TYPE    decimal degrees, WGS 84 (missing at the level "country": then there is no position)
 *   longitude  B_DOUBLE_TYPE    (missing, too)
 *   accuracy   B_FLOAT_TYPE     radius in metres (an IP address is placed to a city: 5000 to 50000; missing, too)
 *   city       B_STRING_TYPE    as the lookup service spelled it, for display (not for comparing: "Wien", "Vienna")
 *   region     B_STRING_TYPE
 *   country    B_STRING_TYPE    ISO 3166-1 alpha-2, upper case
 *   timezone   B_STRING_TYPE    IANA name, may be missing
 *   timestamp  B_INT64_TYPE     seconds since the epoch: when the fix was looked up
 *   provider   B_STRING_TYPE    "ipwho.is", "geojs", "ipinfo", ... or "manual" for an override
 *   stale      B_BOOL_TYPE      the fix is older than two lookup intervals (the machine was off, or offline): good for a first guess, not for switching anything
 *   manual     B_BOOL_TYPE      an override is active
 *   granularity B_STRING_TYPE   how fine this fix is: "city" (as looked up), "region", "country" or a radius in km ("50"): the coarser of what the user allows (the setting max_precision
 *                               of the server) and what the program asked for. A fix at the level "region" has no city, one at "country" has no city, no region and no position.
 *
 * Who may know what: the settings say how fine the location may be for every program (`max_precision`), and for single programs, found by the signature of the application that
 * asks (`program <signature> <level>`; the settings window of SEN and `location grant` set them). A program that is not listed has the general limit; a program with the level `none`
 * gets B_NOT_ALLOWED. The signature is what the roster knows of the sender: this protects against programs that behave and against accidents, not against a program that is made
 * to lie (Haiku has no users and no sandbox to tell it from).
 *
 * How fine the location may be (a privacy setting of the user, not of the program): the server always has the finest fix that the lookup gave and tells each program only as much as the
 * setting `max_precision` allows. A program can ask for less than that, never for more: LOCATION_SUBSCRIBE and LOCATION_GET_CURRENT take an optional string field "granularity"
 * ("city", "region", "country" or a number of km), and the answer says which level it got. The position at a coarse level is the middle of a cell of a fixed grid, not noise: all positions
 * in a cell give the same answer. A subscriber is told of a change only if what it is told changes (for a program that asked for the country, a move inside the country is no change).
 */
#define LOCATION_SERVER_SIGNATURE	"application/x-vnd.basho-location_server"

enum {
	// requests
	LOCATION_SUBSCRIBE		= 'LCSB',	///< field "target" (BMessenger), else the sender; "granularity" (string, optional): gets LOCATION_CHANGED now (if there is a fix) and at every change of what it may be told
	LOCATION_UNSUBSCRIBE	= 'LCUB',	///< the same fields
	LOCATION_GET_CURRENT	= 'LCGC',	///< "granularity" (string, optional); bool "force": look up again (if the rate limit allows) and answer when that is done; answer: LOCATION_CURRENT_REPLY or LOCATION_ERROR
	LOCATION_FORCE_REFRESH	= 'LCFR',	///< look up again, no answer but B_REPLY (status B_OK, or B_BUSY if the last lookup was too near); the news come as LOCATION_CHANGED
	LOCATION_SET_OVERRIDE	= 'LCSO',	///< latitude, longitude (double); city, region, country, timezone (string), accuracy (float) optional: a manual fix that wins until it is cleared
	LOCATION_CLEAR_OVERRIDE	= 'LCCO',
	LOCATION_GET_STATUS		= 'LCST',	///< for tools: answer B_REPLY with the state of the server (see the fields in LocationServer.cpp)

	// the settings, in the protocol that the settings window of SEN speaks to its plugins (the codes have the same values; any host that speaks it, SEN's or your own, can show them):
	LOCATION_SETTINGS		= 'SEse',	///< optional "reply_to" (BMessenger), "serial" (int32); answer LOCATION_SETTINGS_REPLY (or a reply if the sender waits): "settings", "schema", "defaults" (messages), "result" (int32)
	LOCATION_SETTINGS_SET	= 'SEss',	///< "settings": a message of changed fields under their path keys (or the whole message); answer B_REPLY: "result", "detail" (what was refused), "settings" as they are now
	LOCATION_SETTINGS_CLOSE	= 'SEsc',	///< nothing to do for the server, which keeps running
	LOCATION_SETTINGS_REPLY	= 'SEsr',

	// notifications and answers
	LOCATION_CHANGED		= 'LCHG',	///< a fix, to every subscriber
	LOCATION_CURRENT_REPLY	= 'LCRP',	///< a fix, the answer to LOCATION_GET_CURRENT
	LOCATION_ERROR			= 'LCER'	///< "error" (string, in words), "status" (int32, a status_t: B_NOT_ALLOWED the user said no, B_NO_INIT no fix yet, B_ERROR the lookup failed)
};
