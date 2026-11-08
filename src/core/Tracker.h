/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include "Fix.h"

namespace basho {

/**
 * Decides when a new fix is a change that the subscribers should hear about. The position of an IP address jumps around: two services place the same address in two districts,
 * the same service places it a few kilometres away the next day, a mobile network or a VPN puts a machine in another city for one lookup. A subscriber that switches a context at
 * every such jump is worse than none, so:
 *   - a fix within the distance `minMoveKm` (or half the accuracy, if that is larger) of the one that was announced is the same place: nothing is announced;
 *   - a fix farther away, or in another country, is announced only when `confirmations` lookups in a row agree on it (a lookup that places the machine somewhere else again starts the count anew);
 *   - the announced fix is not replaced by a nearby one, so the name of the city does not flip between "Wien" and "Vienna";
 *   - a manual fix (an override) and the first fix are accepted at once, and so is any fix if the announced one is stale (the machine has been off).
 */
class LocationTracker {
public:
	struct Config {
		double	minMoveKm = 25;
		int		confirmations = 2;
	};

	enum Result {
		kSame,			///< the same place as before (nothing to announce)
		kFirst,			///< the first fix, or one that replaces a stale fix or a manual one: announce
		kMoved,			///< a confirmed move: announce
		kPending		///< far from the announced place, not yet confirmed: do not announce
	};

						LocationTracker() {}
						LocationTracker(const Config& config) : fConfig(config) {}

	void				SetConfig(const Config& config) { fConfig = config; }

	/** `currentIsStale`: the announced fix is old (a cache from before a shut down), so the new one is taken as it is. */
	Result				Update(const Fix& fix, bool currentIsStale = false);
	/** Takes the fix without asking (an override, a cache that was read at the start). */
	void				Set(const Fix& fix);
	void				Reset();

	bool				HasCurrent() const { return fHasCurrent; }
	const Fix&			Current() const { return fCurrent; }
	int					PendingCount() const { return fPendingCount; }

	/** The distance within which two fixes are the same place. */
	double				ThresholdKm(const Fix& a, const Fix& b) const;

private:
	Config		fConfig;
	Fix			fCurrent;
	bool		fHasCurrent = false;
	Fix			fPending;
	int			fPendingCount = 0;
};

}	// namespace basho
