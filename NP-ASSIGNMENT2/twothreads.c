/*
	T Dinesh Ram Kumar
	2014A3A7302P
	This program creates two threads, one to read responses from server,
	other to send requests.. ( ALL BLOCKING CALLS.. )
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

#define try(expr,msg) if ( (expr) == -1 ) { perror(msg); exit(EXIT_FAILURE); }
struct _data{
	long requestsize;
	int sockfd;
};
struct _returndata{
	long totalwritebytes;
	long totalrequests;
};
typedef struct _data data ;
typedef struct _data *Data ;
typedef struct _returndata returndata;
typedef struct _returndata *Returndata;
void *writer_thread(void *args) {
	Data somedata = (Data) args;
	int sockfd = somedata->sockfd;
	Returndata returnvalue = malloc(sizeof(returndata));
	long requestsize = somedata->requestsize, totalwritebytes =0, writebytes =0, totalrequests =0;
	char buffer[BUFSIZ];

	// printf("WRITE THREAD SOCKFD: %d REQUEST SIZE: %ld\n",sockfd, requestsize );
	for ( int i =0; i < BUFSIZ; i++) {
		buffer[i] = (char) (rand()%26) + 'a';
	}
	while (1) {
		try ( writebytes= write(sockfd, buffer, BUFSIZ ), "write failed." );
		totalwritebytes += writebytes;
		totalrequests ++;
		if ( totalwritebytes > requestsize ) {
			returnvalue->totalwritebytes = totalwritebytes;
			returnvalue->totalrequests = totalrequests;
			break;
		}
	}
	printf("Finished writing to socket. Shutting it down (WRITE END)..\n");
	try ( shutdown(sockfd, SHUT_WR), "shutdown1 failed." );
	return (void *)returnvalue;
}
int main(int argc, char *argv[]) {
	if ( argc != 4 ) {
		printf("FORMAT: executable ip port size\n");
		exit(EXIT_FAILURE);
	}
	struct timeval starttime, endtime;
	double starttimedf, endtimedf;
	int sockfd, addrlen;
	long requestsize,readbytes =0, totalreadbytes=0, totalwritebytes=0, totalrequests=0; 
	struct sockaddr_in serveraddr;
	char readbuffer[BUFSIZ];
	pthread_t threadid;
	Data somedata = malloc(sizeof(data));
	Returndata returnvalue;
	bzero(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = PF_INET;
	serveraddr.sin_port = htons(atoi(argv[2]));
	serveraddr.sin_addr.s_addr = inet_addr(argv[1]);
	addrlen = sizeof(serveraddr);
	if ( (requestsize = atol(argv[3])) == 0 ) { printf("Invalid requestsize.\n"); exit(EXIT_FAILURE);}
	printf("requestsize: %ld\n",requestsize );
	gettimeofday(&starttime, NULL);
	
	try ( sockfd = socket(AF_INET, SOCK_STREAM, 0), "socket failed." );
	try ( connect(sockfd, (struct sockaddr *)&serveraddr, addrlen), "connect failed." );
	somedata->requestsize = requestsize;
	somedata->sockfd = sockfd;
	// printf("MAIN SOCKFD: %d\n", sockfd);
	if ( pthread_create(&threadid, NULL, writer_thread, (void *)somedata) != 0 ){ 
		printf("Thread creation failed.\n");exit(EXIT_FAILURE);
	}
	while(1) {
			try ( readbytes = read(sockfd, readbuffer, BUFSIZ), "read1 failed." );
			if ( readbytes == 0 ) {
				printf("Finished reading on connection.\n");
				close(sockfd);
				break;
			}
			totalreadbytes += readbytes ;
	}
	pthread_join( threadid, (void **) &returnvalue);
	totalwritebytes = returnvalue->totalwritebytes;	
	totalrequests = returnvalue->totalrequests;
	gettimeofday(&endtime, NULL);
	starttimedf = starttime.tv_sec + ((double)starttime.tv_usec)/1000000.0;
	endtimedf = endtime.tv_sec + ((double)endtime.tv_usec)/1000000.0;
	double totaltime = endtimedf - starttimedf;
	printf("Total Response Time: %lf seconds.\n", totaltime );
	printf("Total Read Bytes: %ld Total Write Bytes: %ld Throughput: %lf. \n",totalreadbytes, totalwritebytes, ((double) totalwritebytes)/((double)totaltime) );
	printf("Average Response time: %lf.\n", totaltime/totalrequests);
	
	return 0;
}