#include "conf.h"
#include "game_manager.h"

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/shm.h>
#include <sys/socket.h>

#define min(x, y) (((x) < (y)) ? (x) : (y))
#define max(x, y) (((x) > (y)) ? (x) : (y))

pid_t own_pid = 0;
char own_player_num = 0;

/**
 * Shared memory segment containing the game's map
 */
struct game *own_game = 0;
char *map = 0;

volatile sig_atomic_t map_updated = 0;
int waiting_for_opponent = 0;

/**
 * Return status of the given field on the map
 */
char map_get(int y, int x) {
	if (!map)
		return 0;
	else
		return map[y * MAP_WIDTH + x];
}

/**
 * Update the given field of the map.  Currently this also notifies the opponent
 * about the move.
 */
void map_set(int y, int x, char v) {
	if (map != 0)
		map[y * MAP_WIDTH + x] = v;

	/*notify(getpid(), MSG_MOVE);*/
	if (own_pid == own_game->sessions[0] &&
			own_game->sessions[1] != 0)
		kill(own_game->sessions[1], SIGUSR1);
	else if (own_pid == own_game->sessions[1])
		kill(own_game->sessions[0], SIGUSR1);
	else
		DBG(2, "Noone to poke\n");
}

/**
 * Currently, this function requests the game's shared memory segment from the
 * game manager.  This is a temporary solution until a real game lobby system is
 * created.
 */
void init_map() {
	int shmid = get_map_shm(own_pid);
	if (shmid == -1) {
		printf("Unable to establish a map, client dropping out (shmget)\n");
		exit(1);
	}
	own_game = (struct game*)shmat(shmid, 0, 0);
	map = own_game->map;
	if (map == (char*)-1) {
		printf("Unable to establish a map, client dropping out (shmat)\n");
		exit(1);
	}

	own_player_num = (own_game->sessions[0] == own_pid) ? 1 : 2;
	/*if (own_player_num == 2) waiting_for_opponent = 1;*/
}

/**
 * Handle SIGUSR1 meaning the other player has made a move
 */
void handle_signal_poke(int sig) {
	map_updated = 1;
}

/**
 * Outputs the full map state to the terminal
 * out		Output stream to the terminal
 * y, x		Position of map's upper left corner in terminal coordinates
 */
void print_map(FILE* out, int y, int x) {
	int i, j;
	
	fprintf(out, "\e[%d;%dH+", y, x);
	for (j = 0; j < MAP_WIDTH; j++)
		fputs("-", out);
	fputs("+", out);

	fprintf(out, "\e[%d;%dH+", y + MAP_HEIGHT + 1, x);
	for (j = 0; j < MAP_WIDTH; j++)
		fputs("-", out);
	fputs("+", out);

	for (i = 0; i < MAP_HEIGHT; i++) {
		fprintf(out, "\e[%d;%dH|", y + i + 1, x);
		fprintf(out, "\e[%d;%dH|", y + i + 1, x + MAP_WIDTH + 1);
	}

	for (i = 0; i < MAP_HEIGHT; i++) {
		fprintf(out, "\e[%d;%dH", i + y + 1, x + 1);
		for (j = 0; j < MAP_WIDTH; j++) {
			/* if ((i+j)%2) fputs("\e[46m", out);
			else fputs("\e[47m", out); */
			if (map_get(i, j) == 0) fputs(" ", out);
			else if (map_get(i, j) == 1) fputs("\e[1;32mX", out);
			else if (map_get(i, j) == 2) fputs("\e[1;34mO", out);
		}
	}
	fputs("\e[0m", out);
}

/**
 * Handles the telnet session thread.
 * sock		Socket connected to the telnet client
 */
void telnet_session(int sock) {
	own_pid = getpid();
	/* stdio is used on output TCP stream */
	FILE *out;
	out = fdopen(sock, "w");
	if ( out == 0) {
		perror("fdopen");
		exit(5);
	}
	
	struct sigaction usr1_sig_action;
	usr1_sig_action.sa_handler = handle_signal_poke;
	/* It is important to allow interruption of system calls, so recv waiting
	   for user input can be interrupted after opponents move. */
	usr1_sig_action.sa_flags = 0;
	sigemptyset(&usr1_sig_action.sa_mask);

	if (sigaction(SIGUSR1, &usr1_sig_action, 0) == -1) {
		perror("sigaction");
		exit(1);
	}
	
	/*
	if (!join_idle_session()) {
		wait for another player
	}
	*/
	notify_idle_session(own_pid);
	init_map();

	int exit = 0, cur_y = MAP_HEIGHT / 2, cur_x = MAP_WIDTH / 2;

	/* set raw terminal, no echo (telnet protocol) */
	fputs("\xff\xfb\x01\xff\xfb\x03\xff\xfd\x0f3", out);
	/* clear screen (ansi sequences) */
	fputs("\e[2J\e[H", out);

	print_map(out, 3, MAP_LEFT);

	while (!exit) {
		fputs("\e[3;50H\e[0KChoose your action: "
				"\e[4;50H q. Exit"
				"\e[5;50H h,j,k,l. Move cursor"
				"\e[6;50H <Space>. Mark field", out);
		fprintf(out, "\e[%d;%dH", cur_y + 4, cur_x + MAP_LEFT + 1);
		fflush(out);

		char input;
		size_t status = recv(sock, &input, 1, 0);
		if (status == 1) {
			switch(input) {
				case 'q':
					exit = 1; break;
				case 'k' :
					cur_y = max(0, cur_y - 1); break;
				case 'j':
					cur_y = min(MAP_HEIGHT - 1, cur_y + 1); break;
				case 'h' :
					cur_x = max(0, cur_x - 1); break;
				case 'l':
					cur_x = min(MAP_WIDTH - 1, cur_x + 1); break;
				case ' ':
					if (!waiting_for_opponent && map_get(cur_y, cur_x) == 0) {
						map_set(cur_y, cur_x, own_player_num);
						waiting_for_opponent = 1;
						print_map(out, 3, MAP_LEFT);
						if (own_player_num == 1)
							fputs("\e[8;50H\e[0KWaiting for player 2 (O) to move...", out);
						else
							fputs("\e[8;50H\e[0KWaiting for player 1 (X) to move...", out);
					}
					break;
			}
			fflush(out);
		} else if (status == -1 && errno != EINTR) {
			perror("recv");
			break;
		} else if (status != 1 && errno == EINTR) {
			if (map_updated) {
				print_map(out, 3, MAP_LEFT);
				map_updated = 0;
				waiting_for_opponent = 0;
				fputs("\e[8;50H\e[0K", out);
			}
		} else if (status == 0) {
			break;
		} else
			DBG(1, "???\n");
	}
	fprintf(out, "\e[0m\e[2J\e[HGoodbye!\n");
	fflush(out);

	fclose(out);

	notify(own_pid, MSG_SESSION_QUIT);
	shmdt(map);
}

