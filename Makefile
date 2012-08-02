MGR_SOCKET_PATH="/var/run/kropkid_sock"

CFLAGS = -Wall -g -DMGR_SOCKET=\"$(MGR_SOCKET_PATH)\"

kropkid: main.c game_manager.o telnet_session.o conf.h
	gcc $(CFLAGS) main.c game_manager.o telnet_session.o ipc_message.o -lm -o kropkid

game_manager.o: game_manager.c game_manager.h ipc_message.o conf.h
	gcc $(CFLAGS) -c game_manager.c -o game_manager.o

telnet_session.o: telnet_session.c conf.h game_manager.o ipc_message.o
	gcc $(CFLAGS) -c telnet_session.c -o telnet_session.o

ipc_message.o: ipc_message.c ipc_message.h
	gcc $(CFLAGS) -c ipc_message.c -o ipc_message.o

clean: 
	rm -f kropkid *.o

test: kropkid
	./kropkid

