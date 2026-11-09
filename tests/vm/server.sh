#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# The server on a Haiku, through the tool `location` (needs the network for the lookup; the first part checks what is in the answer, not where the machine is):
#   - the lookup (allowed for this run with --allow-network, nothing is written to the settings) gives a fix with a valid position, a provider and a country;
#   - a subscriber gets the fix at once and every change; an override is announced, wins over the lookup, and clearing it goes back;
#   - a position by hand that is invalid is refused; the status says what the server is doing;
#   - without the allowance and with `network_lookup never` there is no lookup and the exit status says so.
# Run from the folder of the build (./build.sh first). A copy of your settings is kept and put back.
HERE=$(cd "$(dirname "$0")/../.." && pwd)
BIN=$HERE/bin
T=${TMPDIR:-/tmp}/basho-test.$$
SETTINGS=$(finddir B_USER_SETTINGS_DIRECTORY)/location_server
CACHE=$(finddir B_USER_CACHE_DIRECTORY)/location_server
mkdir -p "$T"; failed=0
check() { desc=$1; shift; if "$@" >/dev/null 2>&1; then echo "ok   $desc"; else echo "FAIL $desc"; failed=1; fi; }
has() { grep -q "$1" "$2"; }

[ -f "$SETTINGS" ] && mv "$SETTINGS" "$T/settings.saved"
[ -d "$CACHE" ] && mv "$CACHE" "$T/cache.saved"
# ("running" does not start the server, as every other command of the tool does)
quit_server() { "$BIN/location" quit >/dev/null 2>&1; for i in 1 2 3 4 5 6 7 8 9 10; do "$BIN/location" running >/dev/null 2>&1 || break; sleep 1; done; }
quit_server

# --- not allowed: no lookup, a window would ask (not shown here: the answer is "no lookup yet")
printf 'network_lookup never\n' > "$SETTINGS"
"$BIN/location_server" > "$T/server0.log" 2>&1 &
sleep 3
"$BIN/location" get > "$T/never.txt" 2>&1; code=$?
check "network_lookup never: exit status 2 (not allowed)" test $code -eq 2
"$BIN/location" set 52.52 13.405 Berlin DE
"$BIN/location" get > "$T/manual-only.txt" 2>&1
check "...but a position by hand works" has "Berlin" "$T/manual-only.txt"
"$BIN/location" clear
quit_server
rm -f "$SETTINGS"; rm -rf "$CACHE"

# --- how fine the location may be (offline: a position by hand): the setting max_precision is a limit, a program can ask for less, never for more
with_settings() {	# the text of the settings file; the server is started with it, a position is set by hand
	quit_server
	printf "$1" > "$SETTINGS"
	"$BIN/location_server" > "$T/granularity-server.log" 2>&1 &
	sleep 3
	"$BIN/location" set 48.2085 16.3721 Vienna AT >/dev/null 2>&1
}
field() { sed -n "s/^$2 *//p" "$1" | head -n1; }

with_settings 'network_lookup never\n'
"$BIN/location" get > "$T/g-city.txt" 2>&1
check "default: the city, the position and granularity city" sh -c "grep -q '^city *Vienna' '$T/g-city.txt' && grep -q '^latitude' '$T/g-city.txt' && grep -q '^granularity city' '$T/g-city.txt'"
"$BIN/location" get --granularity region > "$T/g-region.txt" 2>&1
check "asked for the region: no city, the position only to 100 km" sh -c "grep -q '^granularity region' '$T/g-region.txt' && ! grep -q '^city *Vienna' '$T/g-region.txt' && grep -q '^latitude' '$T/g-region.txt' && grep -q '^accuracy *1[0-9][0-9][0-9][0-9][0-9] m' '$T/g-region.txt'"
"$BIN/location" get --granularity country > "$T/g-country.txt" 2>&1
check "asked for the country: no position at all, the country stays" sh -c "grep -q '^position *none' '$T/g-country.txt' && ! grep -q '^latitude' '$T/g-country.txt' && grep -q '^country *AT' '$T/g-country.txt' && grep -q '^granularity country' '$T/g-country.txt'"
"$BIN/location" get --granularity 50 > "$T/g-50.txt" 2>&1
check "asked for 50 km: the position is moved by at most that, no city" sh -c "grep -q '^granularity 50' '$T/g-50.txt' && ! grep -q '^city *Vienna' '$T/g-50.txt' && grep -q '^accuracy *50000 m' '$T/g-50.txt'"
"$BIN/location" get --granularity 50 > "$T/g-50b.txt" 2>&1
check "the same question gives the same answer (a grid, not noise)" cmp -s "$T/g-50.txt" "$T/g-50b.txt"
check "a granularity that is none is refused" sh -c "! '$BIN/location' get --granularity nonsense"

# a subscriber that asked for the country is not told of a move inside the country
"$BIN/location" watch --granularity country > "$T/g-watch.txt" 2>&1 &
cwatcher=$!
"$BIN/location" watch > "$T/g-watch-city.txt" 2>&1 &
fwatcher=$!
sleep 3
"$BIN/location" set 47.0707 15.4395 Graz AT
sleep 2
"$BIN/location" set 52.52 13.405 Berlin DE
sleep 2
check "a move inside the country is no change for a program that asked for the country; another country is" sh -c "[ \$(grep -c '^-- changed' '$T/g-watch.txt') -eq 2 ]"
check "a program that asked for the city hears all of them" sh -c "[ \$(grep -c '^-- changed' '$T/g-watch-city.txt') -eq 3 ]"
check "and the country watcher is told no position" sh -c "! grep -q '^latitude' '$T/g-watch.txt'"
kill $cwatcher $fwatcher 2>/dev/null

with_settings 'network_lookup never\nmax_precision region\n'
"$BIN/location" get > "$T/c-region.txt" 2>&1
check "max_precision region: a program that asks for the city is told the region" sh -c "grep -q '^granularity region' '$T/c-region.txt' && ! grep -q '^city *Vienna' '$T/c-region.txt'"
"$BIN/location" get --granularity country > "$T/c-region-country.txt" 2>&1
check "...and one that asks for the country gets the country" sh -c "grep -q '^position *none' '$T/c-region-country.txt'"
"$BIN/location" status > "$T/c-status.txt" 2>&1
check "the status tells the limit" grep -q "max_precision   region" "$T/c-status.txt"

with_settings 'network_lookup never\nmax_precision country\n'
"$BIN/location" get --granularity city > "$T/c-country.txt" 2>&1
check "max_precision country: asking for the city is not enough" sh -c "grep -q '^position *none' '$T/c-country.txt' && grep -q '^country *AT' '$T/c-country.txt'"

with_settings 'network_lookup never\nmax_precision contry\n'
"$BIN/location" get > "$T/c-typo.txt" 2>&1
check "a max_precision that is not understood is the coarsest, not the finest" sh -c "grep -q '^position *none' '$T/c-typo.txt'"

# --- the limits of single programs (here the program is the tool itself): a grant replaces the general limit, live, and is kept
TOOL=application/x-vnd.basho-location_tool
with_settings 'network_lookup never\nmax_precision region\n'
"$BIN/location" get > "$T/p-general.txt" 2>&1
check "no rule: the general limit (region)" grep -q "^granularity region" "$T/p-general.txt"
"$BIN/location" grant $TOOL city
"$BIN/location" get > "$T/p-city.txt" 2>&1
check "a program can be allowed more than the others: city" sh -c "grep -q '^granularity city' '$T/p-city.txt' && grep -q '^city *Vienna' '$T/p-city.txt'"
"$BIN/location" grant $TOOL country
"$BIN/location" get > "$T/p-country.txt" 2>&1
check "...or less: country" sh -c "grep -q '^position *none' '$T/p-country.txt'"
check "the rule is in the settings file" grep -q "^program $TOOL country" "$SETTINGS"
"$BIN/location" grant $TOOL none
"$BIN/location" get > "$T/p-none.txt" 2>&1; code=$?
check "none: B_NOT_ALLOWED, exit status 2" test $code -eq 2
check "none: it cannot subscribe either" sh -c "! '$BIN/location' watch"
"$BIN/location" config > "$T/p-config.txt" 2>&1
check "the program is listed with its limit (found by the signature of its application)" grep -q "^program $TOOL  *none" "$T/p-config.txt"
"$BIN/location" grant $TOOL default
"$BIN/location" get > "$T/p-default.txt" 2>&1
check "default takes the rule away: the general limit again" grep -q "^granularity region" "$T/p-default.txt"
check "...and out of the settings file" sh -c "! grep -q '^program $TOOL' '$SETTINGS'"
check "a limit that is none is refused" sh -c "! '$BIN/location' grant $TOOL nonsense"

# a change of the limit reaches a program that is subscribed already
"$BIN/location" watch > "$T/p-watch.txt" 2>&1 &
pwatcher=$!
sleep 3
"$BIN/location" grant $TOOL country
sleep 2
"$BIN/location" grant $TOOL city
sleep 2
check "a subscriber is told at once when its limit changes (region, country, city)" sh -c "[ \$(grep -c '^-- changed' '$T/p-watch.txt') -ge 3 ] && grep -q '^granularity country' '$T/p-watch.txt' && grep -q '^granularity city' '$T/p-watch.txt'"
kill $pwatcher 2>/dev/null
"$BIN/location" grant $TOOL default

# the settings through the protocol: a value by its name, a wrong one refused
"$BIN/location" config set max_precision 40
"$BIN/location" get --granularity city > "$T/p-40.txt" 2>&1
check "config set max_precision 40 (a radius)" sh -c "grep -q '^granularity 40' '$T/p-40.txt'" 
check "a value that does not fit is refused" sh -c "! '$BIN/location' config set network_lookup sometimes"
check "...and so is a number out of its range" sh -c "! '$BIN/location' config set confirmations 99"
check "...and a setting that does not exist" sh -c "! '$BIN/location' config set no_such_setting 1"
"$BIN/location" config set interval_minutes 15
"$BIN/location" config > "$T/p-config2.txt" 2>&1
check "an integer setting is changed" grep -q "^interval_minutes *15" "$T/p-config2.txt"

# the table of the programs in the settings message (the protocol of the settings window): rows, cells by their row number, the box, what is wrong, the whole message
g++ -std=c++20 -Wno-multichar -I"$HERE/include" -o "$T/settings-table" "$HERE/tests/vm/SettingsTable.cpp" "$HERE/lib/liblocation.a" -lbe -lroot -lstdc++ > "$T/settings-table-build.txt" 2>&1
check "the test of the table builds" test -x "$T/settings-table"
"$T/settings-table" > "$T/settings-table.txt" 2>&1
check "the table of the programs: rows, cells by row number, the box, refusals, the whole message" grep -q "^settings table ok" "$T/settings-table.txt"
cat "$T/settings-table.txt" | grep FAIL

# what was set is still there when the server is started again
quit_server
"$BIN/location_server" > "$T/p-restart.log" 2>&1 &
sleep 3
"$BIN/location" set 48.2085 16.3721 Vienna AT >/dev/null 2>&1
"$BIN/location" config > "$T/p-config3.txt" 2>&1
check "the settings and the list of programs are kept over a restart" sh -c "grep -q '^interval_minutes *15' '$T/p-config3.txt' && grep -q '$TOOL' '$T/p-config3.txt'"
quit_server
rm -f "$SETTINGS"

# --- allowed for this run (NO_NETWORK=1: not on a machine without the internet, such as the one of the CI)
if [ -n "$NO_NETWORK" ]; then
	rm -rf "$CACHE"
	[ -f "$T/settings.saved" ] && mv "$T/settings.saved" "$SETTINGS"
	[ -d "$T/cache.saved" ] && mv "$T/cache.saved" "$CACHE"
	[ $failed = 0 ] && echo "all ok (without the lookup)" || { echo FAILED; echo "logs in $T"; }
	exit $failed
fi
"$BIN/location_server" --allow-network > "$T/server.log" 2>&1 &
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
	sleep 2
	"$BIN/location" get > "$T/fix.txt" 2>&1 && break
done
check "the lookup gives a fix" has "^latitude" "$T/fix.txt"
lat=$(sed -n 's/^latitude *//p' "$T/fix.txt"); lon=$(sed -n 's/^longitude *//p' "$T/fix.txt")
check "latitude and longitude are in range" awk -v lat="$lat" -v lon="$lon" 'BEGIN { exit !(lat > -90 && lat < 90 && lon > -180 && lon < 180 && !(lat == 0 && lon == 0)) }'
check "it names a provider" sh -c "grep '^provider' '$T/fix.txt' | grep -v 'provider *\$'"
check "and a country of two letters" sh -c "grep -E '^country +[A-Z]{2}\$' '$T/fix.txt'"
check "the settings file was not written by the run (allowed for this run only)" sh -c "! grep -q 'network_lookup allow' '$SETTINGS'"
[ -f "$CACHE/last_fix" ] && echo "ok   the last fix is cached" || { echo "FAIL the last fix is cached"; failed=1; }

# --- a subscriber: the fix at once, then a change by an override
"$BIN/location" watch > "$T/watch.txt" 2>&1 &
watcher=$!
sleep 3
check "a subscriber gets the fix at once" has "^-- changed" "$T/watch.txt"
"$BIN/location" set 52.52 13.405 Berlin DE
sleep 2
check "an override is announced" has "Berlin" "$T/watch.txt"
"$BIN/location" get > "$T/over.txt" 2>&1
check "and wins over the lookup" has "manual     yes" "$T/over.txt"
check "an invalid position is refused" sh -c "! '$BIN/location' set 91 0"
"$BIN/location" clear
sleep 2
"$BIN/location" get > "$T/back.txt" 2>&1
check "clearing the override goes back to the lookup" has "manual     no" "$T/back.txt"
check "...and tells the subscriber" sh -c "[ \$(grep -c '^-- changed' '$T/watch.txt') -ge 3 ]"
"$BIN/location" status > "$T/status.txt" 2>&1
check "the status tells the subscriber" has "subscribers     1" "$T/status.txt"
kill $watcher 2>/dev/null

# --- rate limit: a second refresh within a minute is not another lookup
"$BIN/location" refresh >/dev/null 2>&1; "$BIN/location" refresh > "$T/refresh2.txt" 2>&1
check "a refresh that comes too soon is told so (the lookup is made a little later)" has "too near" "$T/refresh2.txt"

quit_server
rm -f "$SETTINGS"; rm -rf "$CACHE"
[ -f "$T/settings.saved" ] && mv "$T/settings.saved" "$SETTINGS"
[ -d "$T/cache.saved" ] && mv "$T/cache.saved" "$CACHE"
[ $failed = 0 ] && echo "all ok" || { echo FAILED; echo "logs in $T"; }
[ $failed = 0 ] && rm -rf "$T"
exit $failed
