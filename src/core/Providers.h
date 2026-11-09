/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include "Fix.h"

#include <functional>
#include <string>
#include <vector>

namespace basho {

/** Fetches a URL: true and the body, or false and a reason. The server passes libcurl, the tests pass a function that answers from a fixture. */
using HttpGet = std::function<bool(const std::string& url, std::string* body, std::string* error)>;

/**
 * An IP geolocation service that answers without a key. The server asks them in the order of the setting `providers` until one gives a usable fix. Every service sees the public address
 * of the machine, which is the whole point and the whole privacy cost: that is why the server asks the user before the first lookup.
 */
struct Provider {
	const char*	id;			///< the name in the settings
	const char*	name;		///< the name that the consent dialog shows
	const char*	url;
	bool		secure;		///< https: the answer cannot be read or changed on the way
	bool		(*parse)(const std::string& body, Fix* fix);	///< fills the fix from the answer; false if it says no or is no answer
	const char*	info;		///< a short description for the list of the services in the settings: how it is reached, what is special
};

const std::vector<Provider>&	AllProviders();
const Provider*					FindProvider(const std::string& id);
/** The ids that are asked when the setting does not say otherwise: the ones that answer over https. */
std::vector<std::string>		DefaultProviderIds();

// the parsers (public for the tests)
bool	ParseIpwhois(const std::string& body, Fix* fix);
bool	ParseGeojs(const std::string& body, Fix* fix);
bool	ParseIpinfo(const std::string& body, Fix* fix);
bool	ParseIpApi(const std::string& body, Fix* fix);

/** The radius that is assumed for a provider that does not say how sure it is: IP addresses are placed to a city, the real place is often 10 to 50 km off. */
const float kDefaultAccuracyMetres = 25000.0f;

struct LookupResult {
	bool					ok = false;
	Fix						fix;
	std::string				error;		///< what went wrong at each provider that was asked, one line each
	std::vector<std::string>	tried;	///< the ids that were asked
};

/** Tells which provider is asked now (called before each one, from the thread that does the lookup). */
using ProviderProgress = std::function<void(const std::string& id)>;

/** Asks the providers one after the other until one gives a valid fix; the fix has the provider and `now` (seconds since the epoch). */
LookupResult	Lookup(const std::vector<std::string>& providerIds, const HttpGet& get, int64_t now, const ProviderProgress& progress = ProviderProgress());

}	// namespace basho
