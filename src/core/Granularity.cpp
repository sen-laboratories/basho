/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#include "Granularity.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace basho {

static const double kRegionKm = 100;
static const double kKeepCityUpToKm = 25;
static const double kKeepRegionUpToKm = 300;
static const double kMaxRadiusKm = 1000;
static const double kKmPerDegree = 111.32;
static const double kPi = 3.14159265358979323846;

bool
Granularity::IsCountry() const
{
	return !none && std::isinf(radiusKm);
}

Granularity
Granularity::None()
{
	Granularity g;
	g.none = true;
	g.radiusKm = std::numeric_limits<double>::infinity();
	return g;
}

Granularity
Granularity::City()
{
	return Granularity();
}

Granularity
Granularity::Region()
{
	Granularity g;
	g.radiusKm = kRegionKm;
	return g;
}

Granularity
Granularity::Country()
{
	Granularity g;
	g.radiusKm = std::numeric_limits<double>::infinity();
	return g;
}

Granularity
Granularity::Radius(double km)
{
	Granularity g;
	g.radiusKm = km <= 0 ? 0 : std::min(km, kMaxRadiusKm);
	return g;
}

bool
ParseGranularity(const std::string& text, Granularity* granularity)
{
	std::string word;
	for (char c : text) {
		if (c != ' ' && c != '\t')
			word += (char)std::tolower((unsigned char)c);
	}
	if (word == "city" || word == "exact" || word == "full") {
		*granularity = Granularity::City();
		return true;
	}
	if (word == "region" || word == "state") {
		*granularity = Granularity::Region();
		return true;
	}
	if (word == "none" || word == "off" || word == "deny") {
		*granularity = Granularity::None();
		return true;
	}
	if (word == "country") {
		*granularity = Granularity::Country();
		return true;
	}
	// a number of km, "50" or "50km"
	if (word.size() > 2 && word.compare(word.size() - 2, 2, "km") == 0)
		word.resize(word.size() - 2);
	char* end = NULL;
	double km = std::strtod(word.c_str(), &end);
	if (word.empty() || end == word.c_str() || *end != '\0' || !(km >= 0))
		return false;
	*granularity = Granularity::Radius(km);
	return true;
}

std::string
GranularityName(const Granularity& granularity)
{
	if (granularity.IsNone())
		return "none";
	if (granularity.IsCity())
		return "city";
	if (granularity.IsCountry())
		return "country";
	if (granularity.radiusKm == kRegionKm)
		return "region";
	std::ostringstream out;
	out << granularity.radiusKm;
	return out.str();
}

Granularity
Coarser(const Granularity& a, const Granularity& b)
{
	if (a.none)
		return a;
	if (b.none)
		return b;
	return a.radiusKm >= b.radiusKm ? a : b;
}

Fix
Coarsen(const Fix& fix, const Granularity& granularity)
{
	if (granularity.IsCity() || granularity.IsNone() || !fix.hasPosition)
		return fix;

	Fix coarse = fix;
	if (granularity.IsCountry()) {
		coarse.latitude = coarse.longitude = 0;
		coarse.accuracy = 0;
		coarse.hasPosition = false;
		coarse.city.clear();
		coarse.region.clear();
		return coarse;
	}

	const double radius = granularity.radiusKm;
	// The grid: cells of `radius` km on a side in latitude; in longitude the cell is as wide in km at the latitude of the cell's row, so that it is a degree count that depends on the
	// row and not on the point (every position of a cell gives the same answer).
	double latitudeStep = radius / kKmPerDegree;
	double row = std::floor((fix.latitude + 90.0) / latitudeStep);
	double centreLatitude = std::min(89.999, std::max(-89.999, (row + 0.5) * latitudeStep - 90.0));
	double longitudeStep = std::min(360.0, latitudeStep / std::max(0.01, std::cos(centreLatitude * kPi / 180.0)));
	double column = std::floor((fix.longitude + 180.0) / longitudeStep);
	double centreLongitude = (column + 0.5) * longitudeStep - 180.0;
	// (a cell on the date line or at a pole can reach over the edge: the middle is brought back)
	if (centreLongitude > 180.0)
		centreLongitude -= 360.0;
	coarse.latitude = centreLatitude;
	coarse.longitude = centreLongitude;
	coarse.accuracy = std::max(coarse.accuracy, (float)(radius * 1000.0));
	if (radius > kKeepCityUpToKm)
		coarse.city.clear();
	if (radius > kKeepRegionUpToKm)
		coarse.region.clear();
	return coarse;
}

bool
SameAnswer(const Fix& a, const Fix& b)
{
	if (a.hasPosition != b.hasPosition || a.country != b.country || a.region != b.region || a.city != b.city || a.timezone != b.timezone || a.manual != b.manual)
		return false;
	if (!a.hasPosition)
		return true;
	return std::fabs(a.latitude - b.latitude) < 1e-9 && std::fabs(a.longitude - b.longitude) < 1e-9 && a.accuracy == b.accuracy;
}

}	// namespace basho
