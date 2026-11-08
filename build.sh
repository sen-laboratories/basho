#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Builds bin/location_server (the server), bin/location (the tool) and lib/liblocation.a (the client class) on Haiku. JOBS sets the number of parallel compiles (default 4).
# SEN's socket client (the header <sen/SenSocketHttp.h> of the package sen_core's development files, see HttpClient.cpp) is used if it is installed; Basho does not need SEN otherwise.
set -e
cd "$(dirname "$0")"
JOBS=${JOBS:-4}
OUT=${OUT:-objects}
mkdir -p bin lib "$OUT"
CXX=${CXX:-g++}
HEADERS=$(finddir B_SYSTEM_DEVELOP_DIRECTORY)/headers
INC="-Iinclude -Isrc/core -Isrc/server -isystem $(finddir B_USER_NONPACKAGED_DEVELOP_DIRECTORY)/headers -isystem $HEADERS/private/netservices2 -isystem $HEADERS/private/netservices -isystem $HEADERS/private/shared"
CXXFLAGS="-std=c++20 -Wall -Wextra -Wno-multichar -O2 $INC"

compile() {	# source -> object (parallel, up to $JOBS); an object that is not there afterwards is an error
	obj="$OUT/$(basename "${1%.cpp}").o"
	if [ ! -f "$obj" ] || [ -n "$(find "$1" include src -newer "$obj" -type f 2>/dev/null | head -n1)" ]; then
		echo "  $1"
		rm -f "$obj"
		$CXX $CXXFLAGS -c "$1" -o "$obj" 2>&1 | head -n 40 &
		running=$((running + 1))
		if [ $running -ge $JOBS ]; then wait; running=0; fi
	fi
}
running=0
for f in src/core/*.cpp src/server/*.cpp src/client/*.cpp src/tools/*.cpp; do compile "$f"; done
wait
for f in src/core/*.cpp src/server/*.cpp src/client/*.cpp src/tools/*.cpp; do
	[ -f "$OUT/$(basename "${f%.cpp}").o" ] || { echo "build failed: $f" >&2; exit 1; }
done

CORE="$OUT/Fix.o $OUT/Granularity.o $OUT/Providers.o $OUT/Tracker.o $OUT/Settings.o"
ar rcs lib/liblocation.a "$OUT/LocationClient.o"

$CXX -o bin/location_server "$OUT/main.o" "$OUT/LocationServer.o" "$OUT/LocationMessage.o" "$OUT/HttpClient.o" "$OUT/SettingsMessage.o" $CORE -lbe -lbnetapi -lnetservices2 -lshared -lroot -lstdc++
$CXX -o bin/location "$OUT/location.o" lib/liblocation.a -lbe -lroot -lstdc++

rc -o "$OUT/location.rsrc" src/tools/location.rdef
xres -o bin/location "$OUT/location.rsrc"
mimeset -f bin/location

# the resources of the server: the signature (how the roster finds and starts it) and the version
rc -o "$OUT/location_server.rsrc" src/server/location_server.rdef
xres -o bin/location_server "$OUT/location_server.rsrc"
mimeset -f bin/location_server
echo "built bin/location_server bin/location lib/liblocation.a"
