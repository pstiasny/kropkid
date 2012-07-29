#include "conf.h"
#include "game_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

/* telnet_session.c */
void telnet_session(int sock);

pid_t manager_pid;

void at_listener_exit() {
	int manager_ret_val;
	kill(manager_pid, SIGTERM);
	waitpid(manager_pid, &manager_ret_val, 0);
	unlink(MGR_SOCKET);
	write(0, "Session manager terminated. Listener terminating.\n", 50);
	exit(0);
}

/**
 * The root process spawns the game manager process and listens for telnet
 * connections.
 */
int main() {
	struct stat usock_stat;
	if (stat(MGR_SOCKET, &usock_stat) != -1) {
		if (S_ISSOCK(usock_stat.st_mode))
			fputs("kropkid is already running\n", stderr);
		else
			fputs("socket file exists\n", stderr);
		return 1;
	}

	DBG(2, "Root PID: %d\n", getpid());

	signal(SIGINT, at_listener_exit);
	signal(SIGTERM, at_listener_exit);

	manager_pid = run_manager();
	if (manager_pid == -1) {
		perror("manager fork");
		return 1;
	}

	struct sockaddr_in sa, sr;
	socklen_t addrsize = sizeof(sr);
	memset(&sa, 0, sizeof(sa));
	int sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == -1) return 1;
	int yes = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
		perror("setsockopt"); return 1; }
	sa.sin_family = AF_INET;
	sa.sin_port = htons(SRV_PORT);
	sa.sin_addr.s_addr = INADDR_ANY;
	if (bind(sock, (struct sockaddr*)&sa, sizeof(sa)) == -1) { perror("bind"); return 1; }
	if (listen(sock, 5) == -1) return 1;
	// signal(SIGCHLD, SIG_IGN);
	for(;;) {
		int in_sock = accept(sock, (struct sockaddr*)&sr, &addrsize);
		if (in_sock == -1) { perror("accept"); return 1; }
		int pid = fork();
		if (pid == 0) {
			signal(SIGINT, SIG_DFL);
			signal(SIGTERM, SIG_DFL);
			telnet_session(in_sock);
			close(in_sock);
			return 0;
		} else 
			close(in_sock);
	}
	close(sock);

	at_listener_exit();
	return 0;
}
