#include "conf.h"
#include "game_manager.h"
#include "ipc_message.h"

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

void poke_opponent() {
	if (own_pid == own_game->sessions[0] &&
			own_game->sessions[1] != 0)
		kill(own_game->sessions[1], SIGUSR1);
	else if (own_pid == own_game->sessions[1])
		kill(own_game->sessions[0], SIGUSR1);
	else
		DBG(2, "Noone to poke\n");
}

/**
 * Update the given field of the map.  Currently this also notifies the opponent
 * about the move.
 */
void map_set(int y, int x, char v) {
	if (map != 0)
		map[y * MAP_WIDTH + x] = v;

	poke_opponent();
}

/**
 * Requests the game's shared memory segment from the game manager.
 * Returns 0 on success, -1 on failure.
 */
int init_map() {
	int shmid = get_map_shm(own_pid);
	if (shmid == -1) {
		return -1;
	}
	own_game = (struct game*)shmat(shmid, 0, 0);
	map = own_game->map;
	if (map == (char*)-1) {
		perror("client: shmat");
		exit(1);
	}

	own_player_num = (own_game->sessions[0] == own_pid) ? 1 : 2;
	return 0;
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
	
	fprintf(out, "\e[%d;%dH", y + MAP_HEIGHT + 1, x);
	for (j = 0; j < MAP_WIDTH; j++)
		fputs("=", out);

	for (i = 0; i < MAP_HEIGHT; i++) {
		fprintf(out, "\e[%d;%dH", i + y + 1, x + 1);
		for (j = 0; j < MAP_WIDTH; j++) {
			/* for colourful background:
			if ((i+j)%2) fputs("\e[46m", out);
			else fputs("\e[47m", out); */
			if (map_get(i, j) == 0) fputs(" ", out);
			else if (map_get(i, j) == 1) fputs("\e[1;32mX", out);
			else if (map_get(i, j) == 2) fputs("\e[1;34mO", out);
		}
	}
	fputs("\e[0m", out);
}

void session_join(FILE* out, int sock) {
	char game_key[7];
	int i;
	fputs("\r\nEnter game key: ", out);
	fflush(out);
	for(i = 0; i < 6; i++) {
		if (recv(sock, game_key + i, 1, 0) != 1)
			return;
		fputc(game_key[i], out);
		fflush(out);
	}
	game_key[7] = 0;
	notify_join_game(own_pid, game_key);
}

void session_start(FILE* out, int sock) {
	fputs("kropkid\r\n"
			"<http://github.com/PawelStiasny/kropkid>\r\n\r\n"
			"[h]ost / [j]oin / [q]uit? ", out);
	fflush(out);
	char input = 0;
	while (1) {
		size_t status = recv(sock, &input, 1, 0);
		if (status != 1 || input == 'q') {
			fputs("\r\nGoodbye\r\n", out);
			fflush(out);
			exit(1);
		} else if (input == 'h') {
			/* host game */
			notify_idle_session(own_pid);
			waiting_for_opponent = 1;
			init_map();
			break;
		} else if (input == 'j') {
			/* join game */
			session_join(out, sock);
			waiting_for_opponent = 0;
			if (init_map() == -1) {
				fputs("\r\nNo games to join\r\n"
						"[h]ost / [j]oin / [q]uit? ", out);
				fflush(out);
			} else
				break;
		}
	}
}

void session_ingame(FILE* out, int sock) {
	int exit = 0, cur_y = MAP_HEIGHT / 2, cur_x = MAP_WIDTH / 2;
	int escape_status = 0; /* for reading escape sequences (arrow keys) */

	/* clear screen (ansi sequences) */
	fputs("\e[2J\e[H", out);

	print_map(out, MAP_TOP, MAP_LEFT);

	while (!exit) {
		fprintf(out,
				"\e[24;0H\e[0KGame #%s, You: %s\e[0m  q:Exit  <Space>:Move ",
				own_game->key,
				(own_player_num == 1) ? "\e[1;32mX" : "\e[1;34mO");

		if (waiting_for_opponent) {
			if (own_player_num == 1)
				fputs("\e[24;64H\e[0KWaiting for O...", out);
			else
				fputs("\e[24;64H\e[0KWaiting for X...", out);
		}
		fprintf(out, "\e[%d;%dH", cur_y + MAP_TOP + 1, cur_x + MAP_LEFT + 1);
		fflush(out);

		char input;
		size_t status = recv(sock, &input, 1, 0);
		if (status == 1) {
			switch(input) {
				case 'q':
					fprintf(out, "\e[0m\e[2J\e[H");
					exit = 1;
					break;
				case 'A':
					if (escape_status != 2) break;
				case 'k' :
					cur_y = max(0, cur_y - 1); break;
				case 'B':
					if (escape_status != 2) break;
				case 'j':
					cur_y = min(MAP_HEIGHT - 1, cur_y + 1); break;
				case 'D':
					if (escape_status != 2) break;
				case 'h' :
					cur_x = max(0, cur_x - 1); break;
				case 'C':
					if (escape_status != 2) break;
				case 'l':
					cur_x = min(MAP_WIDTH - 1, cur_x + 1); break;
				case ' ':
					if (!waiting_for_opponent && map_get(cur_y, cur_x) == 0) {
						map_set(cur_y, cur_x, own_player_num);
						waiting_for_opponent = 1;
						print_map(out, MAP_TOP, MAP_LEFT);
					}
					break;
				case 0x1b:
					if (escape_status == 0) escape_status = 1;
					break;
				case '[':
					if (escape_status == 1) escape_status = 2;
					break;
			}
			if ((escape_status == 1 && input != 0x1b) ||
					(escape_status == 2 && input != '['))
				escape_status = 0;
			fflush(out);
		} else if (status == -1 && errno != EINTR) {
			perror("client: recv");
			break;
		} else if (status != 1 && errno == EINTR) {
			if (map_updated) {
				if (own_game->state == GAME_ORPHANED) {
					fprintf(out, "\e[0m\e[2J\e[HThe other player has left\r\n");
					exit = 1;
				} else {
					print_map(out, MAP_TOP, MAP_LEFT);
					map_updated = 0;
					waiting_for_opponent = 0;
					fputs("\e[8;50H\e[0K", out);
				}
			}
		} else if (status == 0) {
			break;
		} else
			DBG(1, "???\n");
	}
	fflush(out);
	shmdt(map);
	notify(own_pid, MSG_SESSION_QUIT);
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
		perror("client: fdopen");
		exit(1);
	}

	/* set raw terminal, no echo (telnet protocol) */
	fputs("\xff\xfb\x01\xff\xfb\x03\xff\xfd\x0f3", out);

	struct sigaction usr1_sig_action;
	usr1_sig_action.sa_handler = handle_signal_poke;
	/* It is important to allow interruption of system calls, so recv waiting
	   for user input can be interrupted after opponents move. */
	usr1_sig_action.sa_flags = 0;
	sigemptyset(&usr1_sig_action.sa_mask);

	if (sigaction(SIGUSR1, &usr1_sig_action, 0) == -1) {
		perror("client: sigaction");
		exit(1);
	}

	while(1) {
		session_start(out, sock);
		session_ingame(out, sock);
	}

	fclose(out);
}

