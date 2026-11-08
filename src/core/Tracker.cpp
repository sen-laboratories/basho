/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#include "Tracker.h"

#include <algorithm>

namespace basho {

double
LocationTracker::ThresholdKm(const Fix& a, const Fix& b) const
{
	return std::max(fConfig.minMoveKm, 0.5 * std::max(a.accuracy, b.accuracy) / 1000.0);
}

void
LocationTracker::Set(const Fix& fix)
{
	fCurrent = fix;
	fHasCurrent = true;
	fPendingCount = 0;
}

void
LocationTracker::Reset()
{
	fHasCurrent = false;
	fPendingCount = 0;
}

LocationTracker::Result
LocationTracker::Update(const Fix& fix, bool currentIsStale)
{
	if (!fHasCurrent || currentIsStale || fCurrent.manual) {
		Set(fix);
		return kFirst;
	}

	double distance = DistanceKm(fCurrent, fix);
	bool otherCountry = !fCurrent.country.empty() && !fix.country.empty() && fCurrent.country != fix.country;
	if (!otherCountry && distance <= ThresholdKm(fCurrent, fix)) {
		// the same place: the announced fix stays as it is (its name, its position), only its age is renewed
		fCurrent.timestamp = fix.timestamp;
		fPendingCount = 0;
		return kSame;
	}

	// somewhere else: do the lookups in a row agree on it?
	if (fPendingCount > 0 && DistanceKm(fPending, fix) <= ThresholdKm(fPending, fix) && fPending.country == fix.country)
		fPendingCount++;
	else
		fPendingCount = 1;
	fPending = fix;

	if (fPendingCount >= fConfig.confirmations) {
		Set(fix);
		return kMoved;
	}
	return kPending;
}

}	// namespace basho
