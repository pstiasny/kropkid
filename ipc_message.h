
struct message {
	/* Message type */
	int mt;

	/* PID of the calling process */
	pid_t pid;
};

struct message_handler {
	size_t message_size;
	void (*handler_func) (struct message*, int);
};

/* host */
int ipc_receive_message(struct message_handler handlers[], int rsock);
int ipc_accept_message(struct message_handler handlers[], int listener_socket);
int ipc_start_listener();

/* client */
int get_send_socket();
void notify(pid_t pid, int message_type);
int query(pid_t pid, int message_type, void *response_buffer, size_t response_size);

