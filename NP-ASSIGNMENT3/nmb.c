#include "nmb.h"

int nmb_msgget(short int port) {
	int sockfd ;
	struct sockaddr_in serveraddr ;
	bzero(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = PF_INET;
	serveraddr.sin_port = htons(port);
	serveraddr.sin_addr.s_addr = inet_addr(LOCAL_ADDR);
	if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1 ) return -1;
	if ( setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)) == -1 ) return -1;
	// bind to specified port ...
	if ( bind(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1) return -1;
	serveraddr.sin_family = PF_INET;
	serveraddr.sin_port = htons(LOCAL_PORT);
	serveraddr.sin_addr.s_addr = inet_addr(LOCAL_ADDR);
	if ( connect(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1 ) return -1; 
	return sockfd;
}
// Note: Inputs in host format 
int nmb_msgsnd( int sockfd, char ip[], short int port, char message[] ) {
	struct mcast_msg msg;
	msg.interface = inet_addr(ip);
	msg.port = htons(port);
	strncpy(msg.message, message, 500);
	msg.message[500] = '\0';
	return send(sockfd, &msg, sizeof(msg), 0); 
}

int nmb_msgrcv( int sockfd, char message[], int size) {
	struct mcast_msg msg;
	int numbytes ;
	if( ( numbytes = recv(sockfd, &msg, sizeof(msg), MSG_WAITALL) ) == -1 ) return -1;
	strncpy(message, msg.message, size -1);
	message[size-1] = '\0'; 
	return numbytes;
}
