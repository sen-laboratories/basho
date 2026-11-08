/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <cstdint>
#include <map>
#include <string>

/**
 * The portable part of Basho (no Haiku headers): a place on Earth as the server knows it, and what is done with it. Compiles on Linux and macOS for the tests.
 */
namespace basho {

/** A coarse location: where the machine is, roughly (a city, not a street). */
struct Fix {
	double		latitude = 0;		///< decimal degrees, WGS 84
	double		longitude = 0;
	float		accuracy = 0;		///< radius in metres, 0 if not known
	std::string	city;				///< as the provider wrote it (the spelling differs between providers: "Wien", "Vienna")
	std::string	region;
	std::string	country;			///< ISO 3166-1 alpha-2, upper case ("AT")
	std::string	timezone;			///< IANA name ("Europe/Vienna"), may be empty
	std::string	provider;			///< the id of the provider ("ipwho.is", "geojs", "manual", ...)
	int64_t		timestamp = 0;		///< seconds since the epoch: when the provider said so
	bool		manual = false;		///< set by hand (an override), not looked up
	bool		hasPosition = true;	///< false for a fix that was made coarser than any position (country level): only the country and the time zone are there

	/** A usable position: in range, not 0/0 ("null island": what a provider says when it does not know), a country that is empty or two letters. Without a position: a country. */
	bool		Valid() const;
};

/** Great-circle distance (haversine) between two points, in kilometres. */
double	DistanceKm(double latitude1, double longitude1, double latitude2, double longitude2);
double	DistanceKm(const Fix& a, const Fix& b);

/** The fix as `key value` lines (the cache of the last fix), and back. Unknown keys are ignored; false if the position is not valid. */
std::string	FixToText(const Fix& fix);
bool		FixFromText(const std::string& text, Fix* fix);

/** Lines `key value` (a key, white space, the rest of the line is the value); empty lines and lines that start with # are skipped. A key that comes twice keeps the last. */
std::map<std::string, std::string>	ParseKeyValues(const std::string& text);

}	// namespace basho
