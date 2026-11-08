/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#include "Providers.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdlib>

namespace basho {

using Json = nlohmann::json;

static Json
ParseJson(const std::string& body)
{
	return Json::parse(body, nullptr, false);
}

// a number that a service writes as a number or as a string ("48.2049")
static bool
NumberOf(const Json& object, const char* key, double* out)
{
	if (!object.is_object() || !object.contains(key))
		return false;
	const Json& value = object[key];
	if (value.is_number()) {
		*out = value.get<double>();
		return true;
	}
	if (value.is_string()) {
		const std::string text = value.get<std::string>();
		char* end = NULL;
		double number = std::strtod(text.c_str(), &end);
		if (end == text.c_str())
			return false;
		*out = number;
		return true;
	}
	return false;
}

static std::string
TextOf(const Json& object, const char* key)
{
	if (object.is_object() && object.contains(key) && object[key].is_string())
		return object[key].get<std::string>();
	return std::string();
}

static std::string
Upper(std::string text)
{
	for (char& c : text)
		c = (char)std::toupper((unsigned char)c);
	return text;
}

bool
ParseIpwhois(const std::string& body, Fix* fix)
{
	Json j = ParseJson(body);
	if (!j.is_object() || !j.value("success", false))
		return false;
	Fix f;
	if (!NumberOf(j, "latitude", &f.latitude) || !NumberOf(j, "longitude", &f.longitude))
		return false;
	f.city = TextOf(j, "city");
	f.region = TextOf(j, "region");
	f.country = Upper(TextOf(j, "country_code"));
	if (j.contains("timezone")) {
		// (an object with the name in "id")
		f.timezone = j["timezone"].is_object() ? TextOf(j["timezone"], "id") : TextOf(j, "timezone");
	}
	f.accuracy = kDefaultAccuracyMetres;
	if (!f.Valid())
		return false;
	*fix = f;
	return true;
}

bool
ParseGeojs(const std::string& body, Fix* fix)
{
	Json j = ParseJson(body);
	if (!j.is_object())
		return false;
	Fix f;
	if (!NumberOf(j, "latitude", &f.latitude) || !NumberOf(j, "longitude", &f.longitude))
		return false;
	f.city = TextOf(j, "city");
	f.region = TextOf(j, "region");
	f.country = Upper(TextOf(j, "country_code"));
	f.timezone = TextOf(j, "timezone");
	// ("accuracy" is a radius in kilometres)
	double km = 0;
	f.accuracy = NumberOf(j, "accuracy", &km) && km > 0 ? (float)(km * 1000.0) : kDefaultAccuracyMetres;
	if (!f.Valid())
		return false;
	*fix = f;
	return true;
}

bool
ParseIpinfo(const std::string& body, Fix* fix)
{
	Json j = ParseJson(body);
	if (!j.is_object() || j.value("bogon", false))
		return false;
	// ("loc" is "48.2085,16.3721")
	const std::string loc = TextOf(j, "loc");
	size_t comma = loc.find(',');
	if (comma == std::string::npos)
		return false;
	Fix f;
	f.latitude = std::strtod(loc.substr(0, comma).c_str(), NULL);
	f.longitude = std::strtod(loc.substr(comma + 1).c_str(), NULL);
	f.city = TextOf(j, "city");
	f.region = TextOf(j, "region");
	f.country = Upper(TextOf(j, "country"));
	f.timezone = TextOf(j, "timezone");
	f.accuracy = kDefaultAccuracyMetres;
	if (!f.Valid())
		return false;
	*fix = f;
	return true;
}

bool
ParseIpApi(const std::string& body, Fix* fix)
{
	Json j = ParseJson(body);
	if (!j.is_object() || TextOf(j, "status") != "success")
		return false;
	Fix f;
	if (!NumberOf(j, "lat", &f.latitude) || !NumberOf(j, "lon", &f.longitude))
		return false;
	f.city = TextOf(j, "city");
	f.region = TextOf(j, "regionName");
	f.country = Upper(TextOf(j, "countryCode"));
	f.timezone = TextOf(j, "timezone");
	f.accuracy = kDefaultAccuracyMetres;
	if (!f.Valid())
		return false;
	*fix = f;
	return true;
}

const std::vector<Provider>&
AllProviders()
{
	static const std::vector<Provider> providers = {
		{"ipwho.is", "ipwho.is", "https://ipwho.is/", true, ParseIpwhois},
		{"geojs", "GeoJS (geojs.io)", "https://get.geojs.io/v1/ip/geo.json", true, ParseGeojs},
		{"ipinfo", "ipinfo.io", "https://ipinfo.io/json", true, ParseIpinfo},
		// (http only on the free plan: the answer can be read and changed on the way, so it is asked only if the setting names it)
		{"ip-api", "ip-api.com (no https)", "http://ip-api.com/json/?fields=status,lat,lon,city,regionName,countryCode,timezone", false, ParseIpApi},
	};
	return providers;
}

const Provider*
FindProvider(const std::string& id)
{
	for (const Provider& provider : AllProviders()) {
		if (id == provider.id)
			return &provider;
	}
	return NULL;
}

std::vector<std::string>
DefaultProviderIds()
{
	std::vector<std::string> ids;
	for (const Provider& provider : AllProviders()) {
		if (provider.secure)
			ids.push_back(provider.id);
	}
	return ids;
}

LookupResult
Lookup(const std::vector<std::string>& providerIds, const HttpGet& get, int64_t now)
{
	LookupResult result;
	for (const std::string& id : providerIds) {
		const Provider* provider = FindProvider(id);
		if (provider == NULL) {
			result.error += id + ": unknown provider\n";
			continue;
		}
		result.tried.push_back(id);
		std::string body, error;
		if (!get(provider->url, &body, &error)) {
			result.error += id + ": " + error + "\n";
			continue;
		}
		Fix fix;
		if (!provider->parse(body, &fix)) {
			result.error += id + ": no usable location in the answer\n";
			continue;
		}
		fix.provider = provider->id;
		fix.timestamp = now;
		result.ok = true;
		result.fix = fix;
		result.error.clear();
		return result;
	}
	if (result.error.empty())
		result.error = "no provider to ask\n";
	return result;
}

}	// namespace basho
