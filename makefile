#James Mullan, CIS3207, Networked Spell Checker

all: server client

server: server.c
	gcc -o server server.c -pthread

client: client.c
	gcc -o client client.c -pthread
