#include "conf.h"
#include "game_manager.h"
#include "ipc_message.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/shm.h>
#include <signal.h>

struct game *idle_games[MAX_GAMES];
int idle_game_count;

/**
 * Returns ID of the game connected to the given PID,
 * -1 if not found.
 */
int get_game_by_pid(pid_t pid) {
	int i = -1;
	for (i = 0; i < idle_game_count; i++)
		if (idle_games[i]->sessions[0] == pid || 
				idle_games[i]->sessions[1] == pid)
			return i;
	return -1;
}

/**
 * Returns a socket connected to the game manager
 */
int get_send_socket() {
	struct sockaddr_un remote;
	int sock = socket(AF_UNIX, SOCK_STREAM, 0);
	remote.sun_family = AF_UNIX;
	strcpy(remote.sun_path, MGR_SOCKET);

	if (connect(sock,
			(struct sockaddr*)&remote,
			strlen(remote.sun_path) + sizeof(remote.sun_family)) == -1) {
		close(sock);
		return -1;
	}

	return sock;
}

void handle_idle_message(struct message *im, int socket) {
	int i;
	DBG(3, "Received idle notification from pid %d\n", im->pid);
	if (idle_game_count >= MAX_GAMES) {
		DBG(1, "Too many idle sessions, I'm dying.\n");
		for (i = 0; i < MAX_GAMES; i++) {
			DBG(2, "Session %d: %d\n", i, idle_games[i]->sessions[0]);
			struct game *g = idle_games[i];
			int shm_id = g->game_shm;

			if (g->sessions[0] != 0)
				kill(g->sessions[0], SIGTERM);
			if (g->sessions[1] != 0)
				kill(g->sessions[1], SIGTERM);
			
			shmdt(idle_games[i]);
			shmctl(shm_id, IPC_RMID, 0);
		}
		exit(1);
	}

	/* TEMPORARY! */
	/* Attach to an idle session */
	for (i = 0; i < idle_game_count; i++)
		if (idle_games[i]->sessions[1] == 0) {
			idle_games[i]->sessions[1] = im->pid;
			return;
		} else if (idle_games[i]->sessions[0] == 0) {
			idle_games[i]->sessions[0] = im->pid;
			return;
		}

	/* Allocate a shared game structure */
	int shmid = shmget(IPC_PRIVATE,
			sizeof(struct game),
			IPC_CREAT | 0600);
	if (shmid == -1) { perror("session manager: shmid"); }
	else {
		struct game *g = (struct game*)shmat(shmid, 0, 0);
		g->game_shm = shmid;

		g->sessions[0] = im->pid;
		g->sessions[1] = 0;

		memset(g->map, 0, MAP_WIDTH*MAP_HEIGHT*sizeof(char));

		DBG(3, "Created map SHM with id %d\n", shmid);
		idle_games[idle_game_count++] = g;
	}
}

void handle_session_quit_message(struct message *qm, int socket) {
	DBG(3, "Session %d quitting\n", qm->pid);
	int gid = get_game_by_pid(qm->pid);
	struct game *g = idle_games[gid];
	if (g->sessions[0] == qm->pid)
		g->sessions[0] = 0;
	else if (g->sessions[1] == qm->pid)
		g->sessions[1] = 0;
	
	if (g->sessions[0] == 0 && g->sessions[1] == 0) {
		int shm_id = g->game_shm;
		DBG(3, "Destroying game #%d, SHM #%d\n", gid, g->game_shm);
		idle_games[gid] = idle_games[--idle_game_count];
		shmdt(g);
		shmctl(shm_id, IPC_RMID, 0);
	}
}

void handle_move_message(struct message *mm, int socket) {
	DBG(3, "Move message recvd\n");
	int game_id = get_game_by_pid(mm->pid);
	if (game_id != -1) {
		struct game *g = idle_games[game_id];
		if (mm->pid == g->sessions[0] &&
				g->sessions[1] != 0)
			kill(g->sessions[1], SIGUSR1);
		else if (mm->pid == g->sessions[1])
			kill(g->sessions[0], SIGUSR1);
		else
			DBG(2, "Noone to poke\n");
	}
}

void handle_map_shm_query(struct message *mq, int socket) {
	DBG(3, "Received map SHM query from pid %d\n", mq->pid);
	int i = get_game_by_pid(mq->pid);
	if (i != -1) {
		if (send(socket, &(idle_games[i]->game_shm), sizeof(int), 0) == -1)
			printf("session manager: send failed\n");
	}
}

/**
 * Clean up at exit
 */
void at_manager_exit(int sig) {
	int i;
	for (i = 0; i < idle_game_count; i++) {
		struct game *g = idle_games[i];
		DBG(2, "Active session #%d (%d, %d, shm %d)\n",
				i, g->sessions[0], g->sessions[1], g->game_shm);
		if (g->sessions[0] != 0)
			kill(g->sessions[0], SIGTERM);
		if (g->sessions[1] != 0)
			kill(g->sessions[1], SIGTERM);
		int shm_id = g->game_shm;
		shmdt(g);
		shmctl(shm_id, IPC_RMID, 0);
	}
	write(0, "Session manager cleaned up\n", 27);
	exit(0);
}

/**
 * Message handlers for use with ipc_receive_message.  Indices correspond to
 * enum MESSAGE_TYPE.
 */
struct message_handler msg_handlers[] =
{
	/* MSG_IDLE */
	{ sizeof(struct message), handle_idle_message },
	/* MSG_MOVE */
	{ sizeof(struct message), handle_move_message },
	/* MSG_MAP_SHM_QUERY */
	{ sizeof(struct message), handle_map_shm_query },
	/* MSG_SESSION_QUIT */
	{ sizeof(struct message), handle_session_quit_message }
};

/**
 * Start the game session manager process and return
 */
int run_manager() {
	int pid = fork();
	if (pid == 0) {
		memset(idle_games, 0, sizeof(idle_games));
		idle_game_count = 0;

		signal(SIGTERM, at_manager_exit);
		signal(SIGINT, at_manager_exit);

		int sock = socket(AF_UNIX, SOCK_STREAM, 0);
		if (sock == -1) exit(1);

		struct sockaddr_un local;
		local.sun_family = AF_UNIX;
		strcpy(local.sun_path, MGR_SOCKET);
		
		int bres = bind(
				sock,
				(struct sockaddr*)&local,
				strlen(local.sun_path) + sizeof(local.sun_family));
		if (bres == -1) {
			perror("session manager: bind");
			exit(1);
		}

		if (listen(sock, 10) == -1) exit(1);

		// TODO: Initialise socket before forking? (Error handling)

		DBG(2, "Session manager is running\n");
		for(;;) {
			struct sockaddr_un remote;
			socklen_t desclen = sizeof(remote);
			int rsock = accept(sock, (struct sockaddr*)&remote, &desclen);

			DBG(3, "Unix socket connection accepted\n");

			ipc_receive_message(msg_handlers, rsock);
			
			close(rsock);
		}
		exit(0);
	} else
		return pid;
}

/**
 * Call this in the client when a session is created or becomes idle
 */
void notify_idle_session(pid_t pid) {
	DBG(3, "Sending idle session notification from pid %d\n", pid);
	int sock = get_send_socket();
	if (sock == -1) perror("client: get_send_socket");
	
	struct message m;
	m.mt = MSG_IDLE;
	m.pid = pid;

	if (send(sock, &m, sizeof(m), 0) == -1)
		printf("Send failed\n");

	int dummy;
	recv(sock, &dummy, 1, 0);
	close(sock);
}

void notify(pid_t pid, enum MESSAGE_TYPE t) {
	DBG(3, "Sending notification from pid %d\n", pid);
	int sock = get_send_socket();
	if (sock == -1) perror("client: get_send_socket");
	
	struct message m;
	m.mt = t;
	m.pid = pid;

	if (send(sock, &m, sizeof(m), 0) == -1)
		printf("Send failed\n");
}

int get_map_shm(pid_t pid) {
	DBG(3, "Requesting map SHM from pid %d\n", pid);
	int sock = get_send_socket();
	if (sock == -1) perror("client: get_send_socket");
	
	struct message mq;
	mq.mt = MSG_MAP_SHM_QUERY;
	mq.pid = pid;

	if (send(sock, &mq, sizeof(mq), 0) == -1)
		printf("client: send failed\n");

	int map_shm = -1;
	if (recv(sock, &map_shm, sizeof(map_shm), 0) <= 0)
		printf("client: recv failed\n");

	close(sock);

	DBG(3, "Obtained map SHM id: %d\n", map_shm);
	return map_shm;
}

