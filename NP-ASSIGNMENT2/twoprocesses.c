/*
	T Dinesh Ram Kumar
	2014A3A7302P
	This program creates two process, one to read responses from server,
	other to send requests.. (ALL BLOCKING..)
	Uses Shared Memory to return back value..
	Use ECHOSERVER as the server for testing...
*/
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
 #include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#define try(expr,msg) if ( (expr) == -1 ) { perror(msg); exit(EXIT_FAILURE); }

int main(int argc, char *argv[]) {
	if ( argc != 4 ) {
		printf("FORMAT: executable ip port size\n");
		exit(EXIT_FAILURE);
	}
	struct timeval starttime, endtime;
	double starttimedf, endtimedf;
	int sockfd, addrlen, childpid ;
	long requestsize,readbytes =0, totalreadbytes=0, totalwritebytes=0, totalrequests=0,writebytes=0; 
	struct sockaddr_in serveraddr;
	char readbuffer[BUFSIZ];
	bzero(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = PF_INET;
	serveraddr.sin_port = htons(atoi(argv[2]));
	serveraddr.sin_addr.s_addr = inet_addr(argv[1]);
	addrlen = sizeof(serveraddr);
	int shmid ;
	try ( shmid =shmget(IPC_PRIVATE, 2*sizeof(long), IPC_CREAT | 0700 ), "shmget failed." );
	long *totalwritebytesptr = shmat(shmid, NULL, 0);
	long *totalrequestsptr = totalwritebytesptr + 1;
	if ( (requestsize = atol(argv[3])) == 0 ) { printf("Invalid requestsize.\n"); exit(EXIT_FAILURE);}
	printf("requestsize: %ld\n",requestsize );
	gettimeofday(&starttime, NULL);
	
	try ( sockfd = socket(AF_INET, SOCK_STREAM, 0), "socket failed." );
	try ( connect(sockfd, (struct sockaddr *)&serveraddr, addrlen), "connect failed." );
	try ( childpid = fork(), "fork failed.");
	if ( childpid == 0 ){
		// child process
		while (1) {
			try ( writebytes= write(sockfd, readbuffer, BUFSIZ ), "write failed." );
			totalwritebytes += writebytes;
			totalrequests ++;
			if ( totalwritebytes > requestsize ) {
				printf("Finished writing on connection.\n");
				break;
			}
		}
		printf("shutdown write end.\n"); 
		try ( shutdown(sockfd, SHUT_WR), "shutdown1 failed." );
		*totalrequestsptr = totalrequests;
		*totalwritebytesptr = totalwritebytes;
		shmdt(totalwritebytesptr);
		exit(EXIT_SUCCESS);
	}
	else {
		while(1) {
			try ( readbytes = read(sockfd, readbuffer, BUFSIZ), "read1 failed." );
			if ( readbytes == 0 ) {
				printf("Finished reading on connection.\n");
				close(sockfd);
				break;
			}
			totalreadbytes += readbytes ;
		}
	}
	try ( wait(NULL), "wait failed.");
	gettimeofday(&endtime, NULL);

	

	totalwritebytes = *totalwritebytesptr;	
	totalrequests = *totalrequestsptr;

	starttimedf = starttime.tv_sec + ((double)starttime.tv_usec)/1000000.0;
	endtimedf = endtime.tv_sec + ((double)endtime.tv_usec)/1000000.0;
	double totaltime = endtimedf - starttimedf;
	printf("Total Response Time: %lf seconds.\n", totaltime );
	printf("Total Read Bytes: %ld Total Write Bytes: %ld Throughput: %lf. \n",totalreadbytes, totalwritebytes, ((double) totalwritebytes)/((double)totaltime) );
	printf("Average Response time: %lf.\n", totaltime/totalrequests);
	try ( shmdt(totalwritebytesptr), "shmdt failed." );
	try ( shmctl(shmid,IPC_RMID,NULL) , "shm rmid failed.");
	return 0;
}