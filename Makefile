MGR_SOCKET_PATH="/var/run/kropkid_sock"

kropkid: main.c game_manager.o telnet_session.o conf.h
	gcc -Wall -g main.c game_manager.o telnet_session.o -lm -o kropkid -DMGR_SOCKET=\"$(MGR_SOCKET_PATH)\"

game_manager.o: game_manager.c game_manager.h conf.h
	gcc -Wall -g -c game_manager.c -o game_manager.o -DMGR_SOCKET=\"$(MGR_SOCKET_PATH)\"

telnet_session.o: telnet_session.c conf.h
	gcc -Wall -g -c telnet_session.c -o telnet_session.o -DMGR_SOCKET=\"$(MGR_SOCKET_PATH)\"

clean: 
	rm -f kropkid *.o

test: kropkid
	./kropkid

