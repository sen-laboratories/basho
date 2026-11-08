/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#include "LocationServer.h"

#include <string.h>

int
main(int argc, char** argv)
{
	// --allow-network: the lookup is allowed for this run (for tests and scripts), nothing is written to the settings
	bool allow = false;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--allow-network") == 0)
			allow = true;
	}
	LocationServer server(allow);
	server.Run();
	return 0;
}
