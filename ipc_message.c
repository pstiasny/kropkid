#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "conf.h"
#include "ipc_message.h"

#define MESSAGE_TAIL(m) ((void*)(m) + sizeof(m->mt))

/**
 * Receives the incoming message and calls apropriate handler
 * handlers		array mapping message types to struct message_handler
 * rsock		socket connected to the incoming connection
 */
int ipc_receive_message(struct message_handler handlers[], int rsock) {
	int message_type;
	recv(rsock, &message_type, sizeof(message_type), 0);

	struct message *message_data =
		malloc(handlers[message_type].message_size);
	if (message_data == 0) {
		perror("ipc_receive_message: malloc");
		return -1;
	}

	if (recv(
			rsock, MESSAGE_TAIL(message_data),
			handlers[message_type].message_size - sizeof(message_type),
			0) == -1) {
		perror("ipc_receive_message: recv");
		return -1;
	}
	message_data->mt = message_type;

	handlers[message_type].handler_func(message_data, rsock);

	free(message_data);
	return 0;
}

/**
 * Accepts incoming connections and handles messages
 */
int ipc_accept_message(struct message_handler handlers[], int listener_socket) {
	struct sockaddr_un remote;
	socklen_t desclen = sizeof(remote);
	int rsock = accept(
			listener_socket,
			(struct sockaddr*)&remote,
			&desclen);
	if (rsock == -1) {
		perror("ipc_accept_message: accept");
		return -1;
	}

	int r = ipc_receive_message(handlers, rsock);

	close(rsock);
	return r;
}

/**
 * Opens a Unix socket for listening to messages.
 * MGR_SOCKET must be defined in conf.h.
 */
int ipc_start_listener() {
	int sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock == -1)
		return -1;

	struct sockaddr_un local;
	local.sun_family = AF_UNIX;
	strcpy(local.sun_path, MGR_SOCKET);

	int bres = bind(
			sock,
			(struct sockaddr*)&local,
			strlen(local.sun_path) + sizeof(local.sun_family));
	if (bres == -1) {
		return -1;
	}

	if (listen(sock, 10) == -1)
		return -1;

	return sock;
}

/**
 * Returns a socket connected to the host
 * MGR_SOCKET must be defined in conf.h.
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

/**
 * Send a message without waiting for response
 */
void notify(pid_t pid, int message_type) {
	/* TODO: better error handling */
	DBG(3, "Sending notification from pid %d\n", pid);
	int sock = get_send_socket();
	if (sock == -1) perror("client: get_send_socket");
	
	struct message m;
	m.mt = message_type;
	m.pid = pid;

	if (send(sock, &m, sizeof(m), 0) == -1)
		perror("client: send\n");
}

/**
 * Send a message and wait for response
 * pid				sender's PID
 * message_type		type of message to send as defined by host's handlers
 * response_buffer	buffer for the response
 * response_size	bytes to receive. if 0, wait for the message to be processed
 */
int query(pid_t pid, int message_type, void *response_buffer, size_t response_size) {
	int sock = get_send_socket();
	if (sock == -1) {
		perror("client: get_send_socket");
		return -1;
	}
	
	struct message mq;
	mq.mt = message_type;
	mq.pid = pid;

	if (send(sock, &mq, sizeof(mq), 0) == -1) {
		perror("client: send");
		return -1;
	}

	if (recv(sock, response_buffer, response_size, 0) != response_size) {
		perror("client: recv");
		return -1;
	}

	close(sock);
	return 0;
}

