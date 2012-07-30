#include <stdlib.h>
#include <sys/socket.h>

#include "ipc_message.h"

int ipc_receive_message(struct message_handler handlers[], int rsock) {
	int message_type;
	recv(rsock, &message_type, sizeof(message_type), 0);

	struct message *message_data = malloc(handlers[message_type].message_size);
	recv(
			rsock, message_data,
			handlers[message_type].message_size - sizeof(message_type), 0);
	message_data->mt = message_type;

	handlers[message_type].handler_func(message_data, rsock);

	free(message_data);

	return 0;
}

