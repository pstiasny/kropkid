
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

int ipc_receive_message(struct message_handler handlers[], int rsock);

