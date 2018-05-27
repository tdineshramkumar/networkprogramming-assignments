#ifndef __NMB__
#define __NMB__

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>

struct mcast_msg{
	// These in network address format
	in_addr_t interface;
	short int port;
	// This is the actual message
	char message[501]; // use last character for NULL
} ;


#define LOCAL_PORT 1111
#define LOCAL_ADDR "127.0.0.1"

// INPUT: Specify input port to bind to 
// so as to get messages send to that port 
// OUTPUT: The socket fd ( Used for further messages )
int nmb_msgget(short int port) ;
// INPUT: Socket fd , IP:PORT to send message to 
// OUTPUT: -1 Failure
int nmb_msgsnd( int sockfd, char ip[], short int port, char message[] );
// INPUT: Socket fd, buffer to recieve message to , sizeof buffer ..
// OUTPUT: 	-1 -> ERROR
//		 	0 -> If no message .. 
int nmb_msgrcv( int sockfd, char message[], int size);
#endif