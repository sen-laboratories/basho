/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
// Tests of the portable core (src/core): the parsers of the providers with the answers in tests/fixtures, the fallback from one provider to the next, the tracker that decides what is
// a change, the settings and the text form of a fix. No network, no Haiku. Run with tests/run.sh.
#include "Fix.h"
#include "Granularity.h"
#include "Providers.h"
#include "Settings.h"
#include "Tracker.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace basho;

static int sFailed = 0, sRun = 0;
#define CHECK(condition) do { sRun++; if (!(condition)) { sFailed++; std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #condition "\n"; } } while (0)
#define CHECK_NEAR(a, b, eps) CHECK(std::fabs((a) - (b)) <= (eps))

static std::string
Fixture(const char* name)
{
	std::ifstream in(std::string(FIXTURES) + "/" + name);
	std::stringstream text;
	text << in.rdbuf();
	return text.str();
}

static Fix
At(double latitude, double longitude, const char* country, const char* city = "", float accuracy = 25000)
{
	Fix f;
	f.latitude = latitude;
	f.longitude = longitude;
	f.country = country;
	f.city = city;
	f.accuracy = accuracy;
	return f;
}

static void
TestDistance()
{
	// Vienna - Berlin is about 523 km, Vienna - Budapest about 215 km
	CHECK_NEAR(DistanceKm(48.2082, 16.3738, 52.5200, 13.4050), 523, 5);
	CHECK_NEAR(DistanceKm(48.2082, 16.3738, 47.4979, 19.0402), 215, 5);
	CHECK_NEAR(DistanceKm(10, 10, 10, 10), 0, 0.0001);
	// across the date line
	CHECK_NEAR(DistanceKm(0.5, 179.9, 0.5, -179.9), 22.2, 0.5);
}

static void
TestValid()
{
	CHECK(At(48.2, 16.3, "AT").Valid());
	CHECK(At(48.2, 16.3, "").Valid());
	CHECK(!At(0, 0, "AT").Valid());			// null island
	CHECK(!At(91, 16.3, "AT").Valid());
	CHECK(!At(48.2, 181, "AT").Valid());
	CHECK(!At(48.2, 16.3, "Austria").Valid());
	CHECK(!At(48.2, 16.3, "at").Valid());
}

static void
TestParsers()
{
	Fix f;
	CHECK(ParseIpwhois(Fixture("ipwho.json"), &f));
	CHECK_NEAR(f.latitude, 48.2085, 0.001);
	CHECK(f.city == "Vienna" && f.country == "AT" && f.region == "Vienna");
	CHECK(f.timezone == "Europe/Vienna");
	CHECK_NEAR(f.accuracy, kDefaultAccuracyMetres, 1);
	CHECK(!ParseIpwhois(Fixture("ipwho-failure.json"), &f));
	CHECK(!ParseIpwhois("not json at all", &f));
	CHECK(!ParseIpwhois(Fixture("null-island.json"), &f));

	f = Fix();
	CHECK(ParseGeojs(Fixture("geojs.json"), &f));
	CHECK_NEAR(f.latitude, 48.2049, 0.0001);	// (a string in the answer)
	CHECK_NEAR(f.longitude, 16.3662, 0.0001);
	CHECK_NEAR(f.accuracy, 5000, 1);			// (kilometres in the answer)
	CHECK(f.country == "AT" && f.timezone == "Europe/Vienna");

	f = Fix();
	CHECK(ParseIpinfo(Fixture("ipinfo.json"), &f));
	CHECK_NEAR(f.latitude, 48.2085, 0.0001);
	CHECK_NEAR(f.longitude, 16.3721, 0.0001);
	CHECK(f.country == "AT" && f.city == "Vienna");
	CHECK(!ParseIpinfo(Fixture("ipinfo-bogon.json"), &f));

	f = Fix();
	CHECK(ParseIpApi(Fixture("ip-api.json"), &f));
	CHECK(f.region == "Vienna" && f.country == "AT");
	CHECK(!ParseIpApi("{\"status\":\"fail\"}", &f));
}

static void
TestProviderList()
{
	CHECK(FindProvider("ipwho.is") != NULL);
	CHECK(FindProvider("nope") == NULL);
	std::vector<std::string> defaults = DefaultProviderIds();
	CHECK(!defaults.empty());
	for (const std::string& id : defaults)
		CHECK(FindProvider(id)->secure);		// (nothing is asked over plain http unless the setting says so)
	CHECK(!FindProvider("ip-api")->secure);
}

static void
TestLookupFallback()
{
	std::vector<std::string> asked;
	HttpGet get = [&](const std::string& url, std::string* body, std::string* error) {
		asked.push_back(url);
		if (url.find("ipwho.is") != std::string::npos) {
			*error = "timed out";
			return false;
		}
		if (url.find("geojs") != std::string::npos) {
			*body = "<html>captive portal</html>";
			return true;
		}
		*body = Fixture("ipinfo.json");
		return true;
	};
	LookupResult r = Lookup({"ipwho.is", "geojs", "ipinfo"}, get, 1000);
	CHECK(r.ok);
	CHECK(asked.size() == 3);
	CHECK(r.fix.provider == "ipinfo" && r.fix.timestamp == 1000);
	CHECK(r.tried.size() == 3 && r.error.empty());

	// the first that answers is the last that is asked
	asked.clear();
	HttpGet good = [&](const std::string& url, std::string* body, std::string*) { asked.push_back(url); *body = Fixture("geojs.json"); return true; };
	r = Lookup({"geojs", "ipinfo"}, good, 5);
	CHECK(r.ok && asked.size() == 1 && r.fix.provider == "geojs");

	// all fail: the reasons are told
	HttpGet bad = [](const std::string&, std::string*, std::string* error) { *error = "no route"; return false; };
	r = Lookup({"ipwho.is", "bogus", "geojs"}, bad, 5);
	CHECK(!r.ok);
	CHECK(r.error.find("ipwho.is: no route") != std::string::npos);
	CHECK(r.error.find("bogus: unknown provider") != std::string::npos);
	CHECK(r.tried.size() == 2);

	r = Lookup({}, bad, 5);
	CHECK(!r.ok && !r.error.empty());

	// the progress: each service that is asked is named before it is asked (the settings show a spinner at it), a service that is not known is not
	std::vector<std::string> progress;
	asked.clear();
	r = Lookup({"ipwho.is", "bogus", "geojs", "ipinfo"}, bad, 5, [&](const std::string& id) { progress.push_back(id); });
	CHECK((progress == std::vector<std::string>{"ipwho.is", "geojs", "ipinfo"}));
	// every provider has a short description for the list of the services, and its name does not have to say whether it is secure
	for (const Provider& provider : AllProviders()) {
		CHECK(provider.info != NULL && std::string(provider.info).size() > 8);
		CHECK(std::string(provider.name).find("http") == std::string::npos);
	}
}

static void
TestTracker()
{
	LocationTracker::Config config;
	config.minMoveKm = 25;
	config.confirmations = 2;

	{	// the first fix is announced at once
		LocationTracker t(config);
		CHECK(!t.HasCurrent());
		CHECK(t.Update(At(48.2, 16.37, "AT", "Vienna")) == LocationTracker::kFirst);
		CHECK(t.HasCurrent() && t.Current().city == "Vienna");
	}
	{	// jitter within the city: the same place, and the announced name stays
		LocationTracker t(config);
		t.Update(At(48.2085, 16.3721, "AT", "Vienna"));
		CHECK(t.Update(At(48.2049, 16.3662, "AT", "Wien")) == LocationTracker::kSame);
		CHECK(t.Update(At(48.30, 16.45, "AT", "Gerasdorf")) == LocationTracker::kSame);	// ~12 km
		CHECK(t.Current().city == "Vienna");
		CHECK_NEAR(t.Current().latitude, 48.2085, 0.0001);
	}
	{	// a move is announced when two lookups in a row agree
		LocationTracker t(config);
		t.Update(At(48.2085, 16.3721, "AT", "Vienna"));
		CHECK(t.Update(At(52.52, 13.405, "DE", "Berlin")) == LocationTracker::kPending);
		CHECK(t.Current().city == "Vienna");
		CHECK(t.Update(At(52.51, 13.40, "DE", "Berlin")) == LocationTracker::kMoved);
		CHECK(t.Current().city == "Berlin");
	}
	{	// one lookup that says somewhere else (a VPN, a mobile network) and then the old place again: no announcement
		LocationTracker t(config);
		t.Update(At(48.2085, 16.3721, "AT", "Vienna"));
		CHECK(t.Update(At(50.11, 8.68, "DE", "Frankfurt")) == LocationTracker::kPending);
		CHECK(t.Update(At(48.21, 16.37, "AT", "Vienna")) == LocationTracker::kSame);
		CHECK(t.PendingCount() == 0);
		CHECK(t.Update(At(50.11, 8.68, "DE", "Frankfurt")) == LocationTracker::kPending);	// (the count starts anew)
	}
	{	// two different places in a row are no confirmation
		LocationTracker t(config);
		t.Update(At(48.2085, 16.3721, "AT", "Vienna"));
		CHECK(t.Update(At(50.11, 8.68, "DE", "Frankfurt")) == LocationTracker::kPending);
		CHECK(t.Update(At(52.52, 13.405, "DE", "Berlin")) == LocationTracker::kPending);
		CHECK(t.Current().city == "Vienna");
	}
	{	// another country counts even if it is near (a border town)
		LocationTracker t(config);
		t.Update(At(48.20, 16.97, "AT", "Hainburg", 1000));
		CHECK(t.Update(At(48.15, 17.11, "SK", "Bratislava", 1000)) == LocationTracker::kPending);
		CHECK(t.Update(At(48.15, 17.11, "SK", "Bratislava", 1000)) == LocationTracker::kMoved);
	}
	{	// the first lookup after a stale cache is taken as it is; so is one after a manual fix
		LocationTracker t(config);
		t.Update(At(48.2085, 16.3721, "AT", "Vienna"));
		CHECK(t.Update(At(52.52, 13.405, "DE", "Berlin"), true) == LocationTracker::kFirst);
		Fix manual = At(35.68, 139.69, "JP", "Tokyo");
		manual.manual = true;
		t.Set(manual);
		CHECK(t.Update(At(48.2085, 16.3721, "AT", "Vienna")) == LocationTracker::kFirst);
	}
	{	// a larger accuracy makes the threshold larger: a fix that is 40 km off but 100 km "accurate" is the same place
		LocationTracker t(config);
		t.Update(At(48.2085, 16.3721, "AT", "Vienna", 100000));
		CHECK(t.Update(At(48.55, 16.55, "AT", "Somewhere", 100000)) == LocationTracker::kSame);
		CHECK_NEAR(t.ThresholdKm(At(0, 0, "", "", 100000), At(0, 0, "", "", 1000)), 50, 0.001);
	}
	{	// one confirmation: a move is announced at once
		LocationTracker::Config one = config;
		one.confirmations = 1;
		LocationTracker t(one);
		t.Update(At(48.2085, 16.3721, "AT", "Vienna"));
		CHECK(t.Update(At(52.52, 13.405, "DE", "Berlin")) == LocationTracker::kMoved);
	}
}

static void
TestSettings()
{
	Settings defaults = Settings::Parse("");
	CHECK(defaults.networkLookup == Settings::kAsk);
	CHECK(defaults.intervalMinutes == 30 && defaults.confirmations == 2 && defaults.minRefreshSeconds == 60);
	CHECK(defaults.ProviderIds() == DefaultProviderIds());

	Settings s = Settings::Parse("# a comment\nnetwork_lookup allow\nproviders geojs bogus ip-api\ninterval_minutes 1\nmin_move_km 9999\nconfirmations x\n\n");
	CHECK(s.networkLookup == Settings::kAllow);
	CHECK((s.providers == std::vector<std::string>{"geojs", "ip-api"}));	// (a name that does not exist is left out)
	CHECK(s.intervalMinutes == 5);			// (clamped)
	CHECK_NEAR(s.minMoveKm, 500, 0.001);
	CHECK(s.confirmations == 2);			// (not a number: the default)

	// what is written can be read
	Settings back = Settings::Parse(s.ToText());
	CHECK(back.networkLookup == Settings::kAllow && back.providers == s.providers && back.intervalMinutes == 5);
	CHECK(Settings::Parse("network_lookup never").networkLookup == Settings::kNever);
	CHECK(Settings::Parse("network_lookup whatever").networkLookup == Settings::kAsk);
	// only unknown names: the default list (an empty list would ask nobody)
	CHECK(Settings::Parse("providers bogus").ProviderIds() == DefaultProviderIds());
	{
		// a service that is off keeps its place in the list (`-name`), and the list has every provider, in this order
		Settings order = Settings::Parse("providers geojs -ipwho.is ipinfo\n");
		CHECK((order.providers == std::vector<std::string>{"geojs", "ipinfo"}));
		const std::vector<std::string> all = order.ProviderOrder();
		CHECK(all.size() == AllProviders().size() && all[0] == "geojs" && all[1] == "ipwho.is" && all[2] == "ipinfo");
		CHECK(Settings::Parse(order.ToText()).ProviderOrder() == all);
		CHECK(Settings::Parse(order.ToText()).providers == order.providers);
		CHECK(order.ToText().find("providers geojs -ipwho.is ipinfo") != std::string::npos);
		CHECK(Settings().ProviderOrder().size() == AllProviders().size());
	}

	// max_precision: the default is the finest; a value that is not understood is the coarsest
	CHECK(Settings::Parse("").maxPrecision.IsCity());
	CHECK(Settings::Parse("max_precision region").maxPrecision == Granularity::Region());
	CHECK(Settings::Parse("max_precision 40").maxPrecision.radiusKm == 40);
	CHECK(Settings::Parse("max_precision contry").maxPrecision.IsCountry());
	CHECK(Settings::Parse(Settings::Parse("max_precision country").ToText()).maxPrecision.IsCountry());
	CHECK(Settings::Parse(Settings::Parse("max_precision 40").ToText()).maxPrecision.radiusKm == 40);
}

static void
TestPrograms()
{
	Settings s = Settings::Parse("max_precision region\n"
		"program application/x-vnd.sen-labs.sen_server city\n"
		"program application/x-vnd.spy none\n"
		"program   application/x-vnd.weather   country  \n"
		"program application/x-vnd.typo contry\n"
		"program\n"
		"programmer x y\n");
	CHECK(s.programs.size() == 4);
	CHECK(s.LimitFor("application/x-vnd.sen-labs.sen_server").IsCity());			// (more than the others)
	CHECK(s.LimitFor("application/x-vnd.spy").IsNone());							// (nothing)
	CHECK(s.LimitFor("application/x-vnd.weather").IsCountry());
	CHECK(s.LimitFor("application/x-vnd.typo").IsCountry());						// (a level that is not understood: the coarsest that can be given)
	CHECK(s.LimitFor("application/x-vnd.somebody-else") == Granularity::Region());	// (no rule: max_precision)
	CHECK(s.FindProgram("application/x-vnd.nobody") == NULL);

	// what is written can be read, in the same order
	Settings back = Settings::Parse(s.ToText());
	CHECK(back.programs.size() == 4 && back.programs[0].signature == "application/x-vnd.sen-labs.sen_server" && back.programs[1].limit.IsNone());
	CHECK(back.LimitFor("application/x-vnd.sen-labs.sen_server").IsCity() && back.maxPrecision == Granularity::Region());

	s.SetProgram("application/x-vnd.spy", Granularity::Radius(30));
	CHECK(s.LimitFor("application/x-vnd.spy").radiusKm == 30 && s.programs.size() == 4);
	s.SetProgram("application/x-vnd.new", Granularity::City());
	CHECK(s.programs.size() == 5);
	s.RemoveProgram("application/x-vnd.spy");
	CHECK(s.programs.size() == 4 && s.LimitFor("application/x-vnd.spy") == Granularity::Region());
	s.RemoveProgram("application/x-vnd.not-there");
	CHECK(s.programs.size() == 4);
	CHECK(Settings::Parse("").programs.empty());
}

static void
TestFixText()
{
	Fix f = At(48.2085, 16.3721, "AT", "Vienna", 5000);
	f.region = "Vienna";
	f.timezone = "Europe/Vienna";
	f.provider = "geojs";
	f.timestamp = 1731085920;
	Fix back;
	CHECK(FixFromText(FixToText(f), &back));
	CHECK_NEAR(back.latitude, f.latitude, 1e-6);
	CHECK_NEAR(back.longitude, f.longitude, 1e-6);
	CHECK(back.city == "Vienna" && back.country == "AT" && back.provider == "geojs" && back.timestamp == 1731085920);
	CHECK_NEAR(back.accuracy, 5000, 0.5);
	CHECK(!back.manual);
	CHECK(!FixFromText("latitude 0\nlongitude 0\n", &back));
	CHECK(!FixFromText("city Vienna\n", &back));
	CHECK(!FixFromText("", &back));

	auto values = ParseKeyValues("  # c\nkey   value with blanks  \nempty\nkey2\tx\nkey2 y\n");
	CHECK(values["key"] == "value with blanks" && values.count("empty") && values["empty"] == "" && values["key2"] == "y");
}

static void
TestGranularity()
{
	Granularity g;
	CHECK(ParseGranularity("city", &g) && g.IsCity());
	CHECK(ParseGranularity(" Region ", &g) && g == Granularity::Region());
	CHECK(ParseGranularity("country", &g) && g.IsCountry());
	CHECK(ParseGranularity("50", &g) && g.radiusKm == 50);
	CHECK(ParseGranularity("50km", &g) && g.radiusKm == 50);
	CHECK(ParseGranularity("99999", &g) && g.radiusKm == 1000);	// (clamped)
	CHECK(ParseGranularity("0", &g) && g.IsCity());
	CHECK(!ParseGranularity("", &g) && !ParseGranularity("contry", &g) && !ParseGranularity("-5", &g) && !ParseGranularity("5 miles", &g));
	CHECK(GranularityName(Granularity::City()) == "city" && GranularityName(Granularity::Region()) == "region" && GranularityName(Granularity::Country()) == "country");
	CHECK(GranularityName(Granularity::Radius(50)) == "50");
	CHECK(Coarser(Granularity::City(), Granularity::Region()) == Granularity::Region());
	CHECK(Coarser(Granularity::Country(), Granularity::Radius(50)).IsCountry());
	CHECK(Coarser(Granularity::Radius(50), Granularity::Radius(20)).radiusKm == 50);
	// none: nothing at all, coarser than the country
	CHECK(ParseGranularity("none", &g) && g.IsNone() && !g.IsCountry() && !g.IsCity());
	CHECK(ParseGranularity("deny", &g) && g.IsNone());
	CHECK(GranularityName(Granularity::None()) == "none");
	CHECK(Coarser(Granularity::None(), Granularity::Country()).IsNone() && Coarser(Granularity::City(), Granularity::None()).IsNone());
	CHECK(!(Granularity::None() == Granularity::Country()));

	Fix vienna = At(48.2085, 16.3721, "AT", "Vienna", 5000);
	vienna.region = "Vienna";
	vienna.timezone = "Europe/Vienna";

	// the city level is the fix as it is
	CHECK(SameAnswer(Coarsen(vienna, Granularity::City()), vienna));

	// a radius: the position is only known to that radius, and what is moved is within it
	Fix r50 = Coarsen(vienna, Granularity::Radius(50));
	CHECK(r50.hasPosition && r50.Valid());
	CHECK(DistanceKm(vienna, r50) <= 50 * 0.75);
	CHECK(r50.accuracy >= 50000);
	CHECK(r50.city.empty() && r50.region == "Vienna" && r50.country == "AT" && r50.timezone == "Europe/Vienna");
	// (a small radius keeps the name of the city)
	Fix r10 = Coarsen(vienna, Granularity::Radius(10));
	CHECK(r10.city == "Vienna" && DistanceKm(vienna, r10) <= 10 * 0.75 && r10.accuracy >= 10000);
	// (a large one takes the region away, too)
	Fix r500 = Coarsen(vienna, Granularity::Radius(500));
	CHECK(r500.region.empty() && r500.country == "AT" && DistanceKm(vienna, r500) <= 500 * 0.75);

	// stable: every position in a cell gives the same answer, and so does the answer again (no noise, no averaging)
	Fix near = At(48.2200, 16.4000, "AT", "Wien", 25000);
	near.region = "Vienna";
	near.timezone = "Europe/Vienna";
	CHECK(SameAnswer(Coarsen(near, Granularity::Radius(50)), r50));		// (the same cell: the name of the city is gone at this level, so "Wien" and "Vienna" do not matter)
	Fix again = Coarsen(r50, Granularity::Radius(50));
	CHECK(SameAnswer(again, r50));
	// two far apart are not the same answer
	CHECK(!SameAnswer(Coarsen(At(52.52, 13.405, "DE", "Berlin"), Granularity::Radius(50)), r50));
	// the answers of many positions near each other take only a few values (a program cannot average to the real one)
	std::map<std::string, int> values;
	for (int i = 0; i < 40; i++) {
		Fix f = At(48.20 + i * 0.002, 16.36 + i * 0.002, "AT", "Vienna", 25000);
		Fix c = Coarsen(f, Granularity::Radius(50));
		values[FixToText(c)]++;
	}
	CHECK(values.size() <= 2);

	// the middle of a cell is a valid position in every part of the world, including at the date line and near the poles
	for (const Fix& f : {At(-33.87, 151.21, "AU"), At(64.1, -21.9, "IS"), At(0.5, 179.99, "FJ"), At(0.5, -179.99, "KI"), At(78.2, 15.6, "NO"), At(-77.8, 166.7, "AQ")}) {
		Fix c = Coarsen(f, Granularity::Radius(100));
		CHECK(c.Valid());
		CHECK(DistanceKm(f, c) <= 100);
		CHECK(c.longitude >= -180 && c.longitude <= 180);
	}

	// the region level
	Fix region = Coarsen(vienna, Granularity::Region());
	CHECK(region.city.empty() && region.region == "Vienna" && region.hasPosition && region.accuracy >= 100000);

	// the country level: no position at all
	Fix country = Coarsen(vienna, Granularity::Country());
	CHECK(!country.hasPosition && country.Valid());
	CHECK(country.country == "AT" && country.timezone == "Europe/Vienna");
	CHECK(country.city.empty() && country.region.empty() && country.latitude == 0 && country.longitude == 0);
	CHECK(SameAnswer(Coarsen(country, Granularity::Country()), country));
	CHECK(SameAnswer(Coarsen(country, Granularity::City()), country));		// (nothing that was taken away comes back)
	// a move inside the country is no change of the answer; another country is
	CHECK(SameAnswer(Coarsen(At(47.07, 15.44, "AT", "Graz"), Granularity::Country()), country) == false);	// (the time zone is empty here: only the country is the same)
	Fix graz = At(47.07, 15.44, "AT", "Graz");
	graz.timezone = "Europe/Vienna";
	CHECK(SameAnswer(Coarsen(graz, Granularity::Country()), country));
	CHECK(!SameAnswer(Coarsen(At(52.52, 13.405, "DE", "Berlin"), Granularity::Country()), country));
}

int
main()
{
	TestDistance();
	TestValid();
	TestParsers();
	TestProviderList();
	TestLookupFallback();
	TestTracker();
	TestSettings();
	TestFixText();
	TestGranularity();
	TestPrograms();
	std::cout << (sRun - sFailed) << " of " << sRun << " checks ok\n";
	return sFailed == 0 ? 0 : 1;
}
