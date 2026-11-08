/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include "Fix.h"

#include <string>

namespace basho {

/**
 * How fine the location that the server hands out may be. The server always keeps the finest fix that it has (what the lookup gave: a city); what a program is told is that fix made
 * coarser, to the level of the setting `max_precision` or the level that the program asks for, whichever is coarser. A program can ask for less than it is allowed (a weather applet
 * wants a city, a program that picks the language wants a country), never for more.
 *
 *   city      the fix as it is (name of the city, position, accuracy of the lookup)
 *   region    no city; the position is only known to 100 km; the region and the country stay
 *   country   no city, no region, no position: the country and the time zone
 *   <km>      a radius: the position is only known to this many km (1 to 1000); the city name is kept up to 25 km, the region up to 300 km
 *   none      nothing: the program is told that it may not know (only as the limit of a program, see Settings.h)
 *
 * The position is made coarser by moving it to the middle of a cell of a fixed grid (not by noise): every position in a cell gives the same answer, so a program cannot take the
 * mean of many answers and get the real position back, and a machine that moves a little does not make a stream of changes.
 */
struct Granularity {
	/** 0: as it is (city); infinity: no position (country); else the radius in km. */
	double	radiusKm = 0;
	/** Nothing at all (the program may not know where the machine is); coarser than everything else. */
	bool	none = false;

	bool	IsCity() const { return !none && radiusKm == 0; }
	bool	IsCountry() const;
	bool	IsNone() const { return none; }
	bool	operator==(const Granularity& other) const { return none == other.none && radiusKm == other.radiusKm; }

	static Granularity	City();
	static Granularity	Region();
	static Granularity	Country();
	static Granularity	Radius(double km);
	static Granularity	None();
};

/** "city", "region", "country", "none" or a number of km ("50"); false for anything else (the granularity is left as it was). */
bool			ParseGranularity(const std::string& text, Granularity* granularity);
/** The words that ParseGranularity reads: the name of a level, else the number. */
std::string		GranularityName(const Granularity& granularity);
/** The coarser of two. */
Granularity		Coarser(const Granularity& a, const Granularity& b);

/** The fix made coarser to the level (it is returned as it is for the city level). Coarsening a coarse fix again changes nothing. Not for the level "none": nothing is told then. */
Fix				Coarsen(const Fix& fix, const Granularity& granularity);
/** Whether two fixes tell a program the same (what is told, not when it was looked up). */
bool			SameAnswer(const Fix& a, const Fix& b);

}	// namespace basho
