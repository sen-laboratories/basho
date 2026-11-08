/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#include "Fix.h"

#include <cmath>
#include <cstdlib>
#include <sstream>

namespace basho {

static const double kPi = 3.14159265358979323846;
static const double kEarthRadiusKm = 6371.0088;

bool
Fix::Valid() const
{
	if (!hasPosition)
		return country.size() == 2;
	if (!(latitude >= -90 && latitude <= 90) || !(longitude >= -180 && longitude <= 180))
		return false;
	if (latitude == 0 && longitude == 0)
		return false;
	if (!(accuracy >= 0))
		return false;
	if (!country.empty()) {
		if (country.size() != 2)
			return false;
		for (char c : country) {
			if (c < 'A' || c > 'Z')
				return false;
		}
	}
	return true;
}

double
DistanceKm(double latitude1, double longitude1, double latitude2, double longitude2)
{
	double p1 = latitude1 * kPi / 180, p2 = latitude2 * kPi / 180;
	double dp = p2 - p1, dl = (longitude2 - longitude1) * kPi / 180;
	double a = std::sin(dp / 2) * std::sin(dp / 2) + std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
	return 2 * kEarthRadiusKm * std::asin(std::min(1.0, std::sqrt(a)));
}

double
DistanceKm(const Fix& a, const Fix& b)
{
	return DistanceKm(a.latitude, a.longitude, b.latitude, b.longitude);
}

std::map<std::string, std::string>
ParseKeyValues(const std::string& text)
{
	std::map<std::string, std::string> values;
	std::istringstream in(text);
	std::string line;
	while (std::getline(in, line)) {
		size_t start = line.find_first_not_of(" \t\r");
		if (start == std::string::npos || line[start] == '#')
			continue;
		size_t end = line.find_last_not_of(" \t\r");
		line = line.substr(start, end - start + 1);
		size_t split = line.find_first_of(" \t");
		if (split == std::string::npos) {
			values[line] = "";
			continue;
		}
		size_t valueStart = line.find_first_not_of(" \t", split);
		values[line.substr(0, split)] = valueStart == std::string::npos ? "" : line.substr(valueStart);
	}
	return values;
}

std::string
FixToText(const Fix& fix)
{
	std::ostringstream out;
	out.precision(7);
	out << "latitude " << fix.latitude << "\n"
		<< "longitude " << fix.longitude << "\n"
		<< "accuracy " << fix.accuracy << "\n"
		<< "city " << fix.city << "\n"
		<< "region " << fix.region << "\n"
		<< "country " << fix.country << "\n"
		<< "timezone " << fix.timezone << "\n"
		<< "provider " << fix.provider << "\n"
		<< "timestamp " << fix.timestamp << "\n"
		<< "manual " << (fix.manual ? 1 : 0) << "\n";
	return out.str();
}

bool
FixFromText(const std::string& text, Fix* fix)
{
	std::map<std::string, std::string> v = ParseKeyValues(text);
	if (!v.count("latitude") || !v.count("longitude"))
		return false;
	Fix f;
	f.latitude = std::strtod(v["latitude"].c_str(), NULL);
	f.longitude = std::strtod(v["longitude"].c_str(), NULL);
	f.accuracy = (float)std::strtod(v["accuracy"].c_str(), NULL);
	f.city = v["city"];
	f.region = v["region"];
	f.country = v["country"];
	f.timezone = v["timezone"];
	f.provider = v["provider"];
	f.timestamp = std::strtoll(v["timestamp"].c_str(), NULL, 10);
	f.manual = v["manual"] == "1";
	if (!f.Valid())
		return false;
	*fix = f;
	return true;
}

}	// namespace basho
