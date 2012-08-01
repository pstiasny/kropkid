#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

#include "conf.h"

struct game {
	/* PIDs of participating telnet sessions */
	pid_t sessions[2];

	/* SHM id of this struct */
	int game_shm;

	/* game map */
	char map[MAP_WIDTH * MAP_HEIGHT];
};

enum MESSAGE_TYPE {
	MSG_IDLE,
	MSG_MOVE,
	MSG_MAP_SHM_QUERY,
	MSG_SESSION_QUIT
};

int run_manager();
void notify_idle_session(pid_t pid);
int get_map_shm(pid_t pid);
