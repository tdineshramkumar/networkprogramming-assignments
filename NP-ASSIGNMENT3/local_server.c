#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <signal.h>
#include <sys/msg.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>

#define MCAST_PORT 1112
#define MCAST_ADDR "239.0.0.1"

#define MQ_PATH "/dev/null"
#define MQ_PROG 99

#define LOCAL_PORT 1111
#define LOCAL_ADDR "127.0.0.1"
#define try(expr, msg) if((expr) == -1) fprintf(stderr,"\033[031mERROR:"),perror(msg), fprintf(stderr,"\033[0m"),exit(EXIT_FAILURE);

struct mcast_msg{
	// These in network address format
	in_addr_t interface;
	short int port;
	// This is the actual message
	char message[501]; // use last character for NULL
} ;

struct mq_msg{
	long type; 			// this contains the type 
	char message[501];  // this is the actual NULL terminated message 
};

in_addr_t localinterface;

int mfd ;
void signalhandler(int signo){
	close(mfd);
	exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
	if ( argc != 2) {
		// the argument 1 is the local interface (used as filter)
		printf("FORMAT: executable interface-ip\n");
		return -1;
	}
	localinterface = inet_addr(argv[1]);
	signal(SIGINT, signalhandler);

	int pid, lfd;
	struct sockaddr_in serveraddr, mserveraddr;
	struct ip_mreq mreq;
	int msqid;

	// Set up the message queue ...
	try( msqid = msgget( ftok(MQ_PATH, MQ_PROG), IPC_CREAT|0600 ) , "msgget failed.");

	// Set the multicast messaging ...
	mreq.imr_multiaddr.s_addr = inet_addr(MCAST_ADDR); 
	mreq.imr_interface.s_addr = inet_addr(argv[1]);

	bzero(&mserveraddr,sizeof(mserveraddr));
	mserveraddr.sin_family = PF_INET;
	mserveraddr.sin_port = htons(MCAST_PORT);
	mserveraddr.sin_addr.s_addr = inet_addr(MCAST_ADDR); 

	try ( mfd = socket(AF_INET, SOCK_DGRAM, 0) , "dgram socket failed.");
	try ( setsockopt( mfd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int))  , "set sock opt failed.");
	try ( setsockopt(mfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) ,"set sock opt failed.");
	try ( bind(mfd, (struct sockaddr *)&mserveraddr, sizeof(mserveraddr)) , "mcast bind failed.");
	
	try ( pid = fork() , "fork failed.");
	if ( pid == 0 ){
		// child process wait for messages and match the interfaces
		// then push it to queue ...
		struct mcast_msg msg;
		struct mq_msg qmsg ;
		in_addr_t interface;
		short int port ;
		printf("WAITING FOR MCAST MESSAGE %d...\n",getpid());
		while ( recvfrom(mfd, &msg, sizeof(msg), 0, NULL, NULL) > 0 ) // Not interested in the receiver..
		{	
			struct in_addr tmp ;
			tmp.s_addr = msg.interface;
			printf("MCAST TO:%s:%d %s \n",inet_ntoa(tmp), ntohs(msg.port), msg.message);
			interface = msg.interface;
			port = ntohs(msg.port);
			if ( interface == localinterface ){
				printf("MATCHING INTERFACE.. PUSHING TO QUEUE...\n");
				// If destined for current node ..
				qmsg.type = port;
				strncpy(qmsg.message, msg.message, 500);
				qmsg.message[500] = '\0';
				// Push it to queue ...
				try ( msgsnd(msqid, &qmsg, sizeof(qmsg), 0) , "mq send failed.");
			}
			else {
				printf("DROPPING MESSAGE\n");
			}
		}
		printf("EXITING MULTICAST RECIVER ..\n");
		exit(EXIT_SUCCESS);
	}

	// To listen from local connections 
	bzero(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = PF_INET;
	serveraddr.sin_port = htons(LOCAL_PORT);
	serveraddr.sin_addr.s_addr = inet_addr(LOCAL_ADDR);
	try ( lfd = socket(AF_INET, SOCK_STREAM, 0) , "local socket faeild.");
	try ( setsockopt( lfd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)), "set sock opt failed."  );
	// printf("TCP SOCKET\n");
	try ( bind(lfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)),"bind local falied." );
	try ( listen(lfd, 15) , "listen falied.");
	// printf("TCP DONE\n");
	while(1) {
		// wait for incoming connections ..
		int cfd, addrlen = sizeof(struct sockaddr_in);
		struct sockaddr_in clientaddr;
		try( cfd = accept(lfd, (struct sockaddr *) &clientaddr, &addrlen) ,"accept failed.");
		printf("ACCEPTED CONNECTION FROM: %s:%d\n", inet_ntoa(clientaddr.sin_addr) ,ntohs(clientaddr.sin_port));
		try( pid = fork() ,"fork failed");
		if ( pid == 0 ){
			// child process handles the connection 
			close(lfd);
			struct mcast_msg msg;
			struct mq_msg qmsg ;
			in_addr_t interface;
			short int port ;
			port = ntohs(clientaddr.sin_port); // get the port of client ...
			// Now one process for reading the request 
			// other for writing back responses ..
			try( pid = fork() ,"fork failed.");
			if ( pid == 0 ){
				// type: port 
				while ( 1 ){
					try ( msgrcv( msqid, &qmsg, sizeof(qmsg), port, 0) ,"msgrcv failed.");
					printf("MQ: FETCHED FOR PORT:%d %s\n", port, qmsg.message);
					msg.port = clientaddr.sin_port;// Note: Not used by client
					msg.interface = clientaddr.sin_addr.s_addr;
					strncpy(msg.message, qmsg.message, 500);
					msg.message[500] = '\0';
					if ( strlen(msg.message) > 0 ){
						try( send(cfd, &msg, sizeof(msg), 0), "send failed."); // send to client ...
					}
				}
				exit(EXIT_SUCCESS);
			}

			// Note: Assuming the it has been converted to 
			// network format by sender ...
			// <---------------------------------
			while ( recv(cfd, &msg, sizeof(msg), MSG_WAITALL) > 0) {
				// once get the message to be send mcast it ..
				struct in_addr tmp ;
				tmp.s_addr = msg.interface;
				printf("MCASTING DEST:%s:%d %s\n", inet_ntoa(tmp), ntohs(msg.port), msg.message );
				try( sendto( mfd, &msg, sizeof(msg), 0, (struct sockaddr *) &mserveraddr, sizeof(mserveraddr)),"sendto failed." );
			}
			kill(pid, SIGINT); // kill the waiting child process
			printf("KILLING %d\n", pid);
			exit(EXIT_SUCCESS);
		} 
		close(cfd);
	}

	while(wait(NULL) > 0); // wait for all children ..
	return 0;
}
