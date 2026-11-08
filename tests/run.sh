#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Builds and runs the tests of src/core (no Haiku, no network). The nlohmann/json headers are found through JSON_INCLUDE (the folder that contains "nlohmann"; on Haiku the package
# nlohmann_json puts them into the system headers).
set -e
cd "$(dirname "$0")/.."
out=${TMPDIR:-/tmp}/basho-core-test
${CXX:-c++} -std=c++20 -Wall -Wextra -Isrc/core ${JSON_INCLUDE:+-I$JSON_INCLUDE} -DFIXTURES="\"$PWD/tests/fixtures\"" -o "$out" \
	tests/CoreTest.cpp src/core/Fix.cpp src/core/Granularity.cpp src/core/Providers.cpp src/core/Tracker.cpp src/core/Settings.cpp
"$out"
