#include "rules.h"
#include <stdio.h>
#include "conf.h"
#include <assert.h>

#define MAP_AT(m, y, x) m[y * MAP_WIDTH + x]

/**
 * Check if there is a path to the edge of the map.
 * Does not clean up flags after returning.  The function is implemented
 * using DFS.
 * Returns 1 if edge is reached, 0 otherwise.
 */
int seek_exit(char *map, int y, int x, char own_player, char visited_from) {
	char v = MAP_AT(map, y, x);
	if (v & VISITED) return 0;
	MAP_AT(map, y, x) |= VISITED | visited_from;

	if (((v & PLAYER) == own_player) && !(v & DISABLED)) return 0;
	if (y == 0 || y == (MAP_HEIGHT - 1) || x == 0 || x == (MAP_WIDTH - 1))
		return 1;

	if (
			seek_exit(map, y, x - 1, own_player, FROM_RIGHT) ||
			seek_exit(map, y, x + 1, own_player, FROM_LEFT) ||
			seek_exit(map, y - 1, x, own_player, FROM_BOT) ||
			seek_exit(map, y + 1, x, own_player, FROM_UP)) {
		/*MAP_AT(map, y, x) |= (1 << 7);*/
		return 1;
	} else
		return 0;
}

/**
 * Clear the visitied flag to the root of the path
 */
void backtrace_clear_visited(char *map, int y, int x) {
	char v = MAP_AT(map, y, x);
	if (!(v & VISITED))
		return;

	MAP_AT(map, y, x) &= ~VISITED;
	switch (v & (3 << 5)) {
		case FROM_UP:
			backtrace_clear_visited(map, y - 1, x); break;
		case FROM_BOT:
			backtrace_clear_visited(map, y + 1, x); break;
		case FROM_LEFT:
			backtrace_clear_visited(map, y, x - 1); break;
		case FROM_RIGHT:
			backtrace_clear_visited(map, y, x + 1); break;
	}
}

/**
 * Mark the given field and its surroudings, up to fields occupied by own_player
 * as disabled.
 * This function is implemented using DFS.  The area must not be open.
 */
void mark_area_disabled(char *map, int y, int x, char own_player) {
	assert(x >= 0);
	assert(y >= 0);
	assert(x < MAP_WIDTH);
	assert(y < MAP_HEIGHT);
	char v = MAP_AT(map, y, x);
	if ((v & VISITED) || 
			((v & PLAYER) == own_player) && !(v & DISABLED))
		return;
	/* TODO: Test this insted of clear_flags when a closed area is found
	if (v & VISITED) 
		backtrace_clear_visited(map, y, x);*/
	MAP_AT(map, y, x) |= VISITED;

	mark_area_disabled(map, y - 1, x, own_player);
	mark_area_disabled(map, y + 1, x, own_player);
	mark_area_disabled(map, y, x - 1, own_player);
	mark_area_disabled(map, y, x + 1, own_player);
}

/**
 * Clear working flags and mark visited fields as disabled
 */
void flag_visited_as_disabled(char *map) {
	int cur;
	for (cur = 0; cur < MAP_HEIGHT * MAP_WIDTH; cur++)
		if (map[cur] & VISITED)
			map[cur] = (map[cur] & PLAYER) | DISABLED;
		else
			map[cur] = map[cur] & (PLAYER | DISABLED);
}

/**
 * Clear working flags on the map
 */
void clear_flags(char *map) {
	int cur;
	for (cur = 0; cur < MAP_HEIGHT * MAP_WIDTH; cur++)
		map[cur] = map[cur] & (PLAYER | DISABLED);
}

void process_map(char *map, int start_y, int start_x) {
	char player = MAP_AT(map, start_y, start_x);
	DBG(3, "Processing map from %d, %d, player %d\n",
			start_x, start_y, player);

	if (start_x > 0)
		if (!seek_exit(map, start_y, start_x - 1, player, FROM_RIGHT)) {
			clear_flags(map);
			mark_area_disabled(map, start_y, start_x - 1, player);
			flag_visited_as_disabled(map);
		} else 
			clear_flags(map);

	if (start_x < MAP_WIDTH - 1)
		if (!seek_exit(map, start_y, start_x + 1, player, FROM_LEFT)) {
			clear_flags(map);
			mark_area_disabled(map, start_y, start_x + 1, player);
			flag_visited_as_disabled(map);
		} else 
			clear_flags(map);

	if (start_y > 0)
		if (!seek_exit(map, start_y - 1, start_x, player, FROM_BOT)) {
			clear_flags(map);
			mark_area_disabled(map, start_y - 1, start_x, player);
			flag_visited_as_disabled(map);
		} else 
			clear_flags(map);

	if (start_y < MAP_HEIGHT - 1)
		if (!seek_exit(map, start_y + 1, start_x, player, FROM_UP)) {
			clear_flags(map);
			mark_area_disabled(map, start_y + 1, start_x, player);
			flag_visited_as_disabled(map);
		} else 
			clear_flags(map);
}

