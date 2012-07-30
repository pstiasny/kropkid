
#ifndef SRV_PORT
	#define SRV_PORT 23001
#endif

#ifndef MGR_SOCKET
	#define MGR_SOCKET "/var/run/kropkid_sock"
#endif

#define MAP_HEIGHT 18
#define MAP_WIDTH 35
#define MAP_LEFT 6

#define MAX_GAMES 1024

/*
 * 0 - No debug messages
 * 1 - Warnings
 * 2 - General state messages
 * 3 - Detailed event information
 */
#ifndef DEBUG_LEVEL
	#define DEBUG_LEVEL 2
#endif

#define DBG(l,...) { if ((l) <= DEBUG_LEVEL) printf(__VA_ARGS__); }
