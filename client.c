/*
    Command line arguements:
    argv[0] filename
    argv[1] server_ipaddress
    argv[2] portno
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h> //defines hostent structure

void error(const char* msg){
    perror(msg);
    exit(1);
}

int main(int argc, char *argv[])
{
    int sockfd, portNo, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    char buffer[256];
    if(argc < 3){
        fprintf(stderr, "usage %s hostname port\n", argv[0]);
        exit(420); //Change this before you hand it in
    }

    portNo = atoi(argv[2]); //atoi converts string to integer
    sockfd = socket(AF_INET, SOCK_STREAM, 0); //Create socket
    if(sockfd < 0){ //Socket error checking
        error("Error opening socket");
    }

    server = gethostbyname(argv[1]); //Getting reference to the server based on ip address (argv[1])
    if(server == NULL){
        fprintf(stderr, "No such host");
    }

    bzero((char *) &serv_addr, sizeof(serv_addr)); //Clean serv_addr structure
    serv_addr.sin_family = AF_INET; //Setting type of connection

    bcopy((char *) server->h_addr, (char *) &serv_addr.sin_addr.s_addr, server->h_length); //Copying address of server into serv_addr structure
    
    serv_addr.sin_port = htons(portNo); //Setting the port number for the connection
    
    if(connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0){ //Establishing connection
        error("Connection failed");
    }

    while(1){ //Continually ask for user input to send to server
        bzero(buffer, 256);
        fgets(buffer, 256, stdin);

        n = write(sockfd, buffer, strlen(buffer));
        if(n < 0){
            error("Error on writing");
        }

        if(strncmp("!exit", buffer, 5) == 0){
		break;
	}
	bzero(buffer, 256);

    }
    close(sockfd);
    return 0;
}
