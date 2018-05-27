/*
	T Dinesh Ram Kumar
	2014A3A70302P
	this is an echo server which capitalizes input and echo them back...
*/

#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>

#define LISTEN_BACKLOG 5
#define try(expr,msg) if ( (expr) == -1 ) { perror(msg); exit(EXIT_FAILURE); }
int main( int argc, char *argv []) {
	if ( argc != 3 ) {
		printf("FORMAT: executable ip port\n");
		exit(EXIT_FAILURE);
	}
	int sockfd, connfd, procfd, addrlen, numbytes, options =1;
	char buffer[BUFSIZ];
	struct sockaddr_in serveraddr, clientaddr;
	try ( sockfd = socket(AF_INET, SOCK_STREAM, 0), "socket failed." );
	try ( setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (void *) &options, sizeof(options)), "setsockopt failed." );
	bzero(&serveraddr,sizeof(serveraddr));
	serveraddr.sin_family = PF_INET;
	serveraddr.sin_port = htons(atoi(argv[2]));
	serveraddr.sin_addr.s_addr = inet_addr(argv[1]);
	addrlen = sizeof(serveraddr);
	try ( bind(sockfd, (struct sockaddr *) &serveraddr, addrlen), "bind failed." );
	try ( listen(sockfd, LISTEN_BACKLOG), "listen failed." );
	while ( 1 ) {
		try ( connfd = accept(sockfd, (struct sockaddr *)&clientaddr,&addrlen ), "accept failed.");
		try ( procfd = fork(), "fork failed." ) ;
		if ( procfd == 0 ) {
			close(sockfd);
			printf("New client: %s:%d \n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
			while ( 1 ) {
				try ( numbytes = read(connfd, buffer, BUFSIZ), "read failed." );
				if ( numbytes == 0 ) break ;  
				for ( int i= 0; i <numbytes; i ++) {
					if ( buffer[i] >= 'a' || buffer[i] <= 'z' ) {
						buffer[i] -= 32;
					}
				}
				try ( write(connfd, buffer, numbytes), "write failed." );
			}
			printf("Connection closed %s:%d.\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
			exit(EXIT_SUCCESS);
		}
		close(connfd);
	}
	return 0;
}