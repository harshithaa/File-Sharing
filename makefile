all: uftp.h client.c server.c
	gcc client.c -o client -lpthread
	gcc server.c -o server -lpthread
