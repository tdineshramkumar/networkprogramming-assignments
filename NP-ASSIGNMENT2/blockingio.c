/*
	T Dinesh Ram Kumar
	2014A3A70302P
	This program sends data to server using blocking I/O and select
	Use ECHOSERVER as the server for testing...
*/

#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/time.h>

// This one uses blocking IO
#define try(expr,msg) if ( (expr) == -1 ) { perror(msg); exit(EXIT_FAILURE); }
int main(int argc, char *argv[]) {
	if ( argc != 4 ) {
		printf("FORMAT: executable ip port size\n");
		exit(EXIT_FAILURE);
	}
	struct timeval starttime, endtime;
	double starttimedf, endtimedf;
	int sockfd, addrlen,  count, alreadyshutdown=0;
	long requestsize,readbytes =0, writebytes =0, totalreadbytes=0, totalwritebytes =0, totalrequests=0; 
	struct sockaddr_in serveraddr;
	char buffer[BUFSIZ], readbuffer[BUFSIZ];
	fd_set readset, writeset, readallset, writeallset;
	for ( int i =0; i < BUFSIZ; i++) {
		buffer[i] = (char) (rand()%26) + 'a';
	}
	gettimeofday(&starttime, NULL);
	try ( sockfd = socket(AF_INET, SOCK_STREAM, 0), "socket failed." );
	bzero(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = PF_INET;
	serveraddr.sin_port = htons(atoi(argv[2]));
	serveraddr.sin_addr.s_addr = inet_addr(argv[1]);
	addrlen = sizeof(serveraddr);
	if ( (requestsize = atol(argv[3])) == 0 ) { printf("Invalid requestsize.\n"); exit(EXIT_FAILURE);}
	printf("requestsize: %ld\n",requestsize );
	try ( connect(sockfd, (struct sockaddr *)&serveraddr, addrlen), "connect failed." );
	FD_ZERO(&readallset);
	FD_SET(sockfd, &readallset);
	FD_ZERO(&writeallset);
	FD_SET(sockfd, &writeallset);
	while(1) {
		if ( ! ( FD_ISSET(sockfd, &readallset) || FD_ISSET(sockfd, &writeallset)  ) ) 
			break;
		readset = readallset;
		writeset = writeallset;
		try ( count = select(sockfd+1, &readset, &writeset, NULL, NULL), "select failed." );
		if ( count == 0 ) break;
		if ( FD_ISSET(sockfd, &readset) ) {
			try ( readbytes = read(sockfd, readbuffer, BUFSIZ), "read1 failed." );
			if ( readbytes == 0 ) {
				printf("Finished reading on connection1.\n");
				close(sockfd);
				FD_CLR(sockfd, &readallset);
				FD_CLR(sockfd, &writeallset);
			}
			totalreadbytes += readbytes ;
		}
		if ( FD_ISSET(sockfd, &writeset) ) {
			try ( writebytes = write(sockfd, buffer, BUFSIZ), "write1 failed." );
			totalwritebytes += writebytes;
			totalrequests++;
		}
		if ( alreadyshutdown == 0 && totalwritebytes > requestsize ) {
			// If required size of data is sent
			// shutdown the socket to indicate server that no more request
			try ( shutdown(sockfd, SHUT_WR), "shutdown1 failed." );
			printf("Finished writing all data. shutting down connections. \n");
			FD_CLR(sockfd, &writeallset);
			alreadyshutdown = 1 ; // Set the flag..
		}
	}
	gettimeofday(&endtime, NULL);
	starttimedf = starttime.tv_sec + ((double)starttime.tv_usec)/1000000.0;
	endtimedf = endtime.tv_sec + ((double)endtime.tv_usec)/1000000.0;
	double totaltime = endtimedf - starttimedf;
	printf("Total Response Time: %lf seconds.\n", totaltime );
	printf("Total Read Bytes: %ld Total Write Bytes: %ld Throughput: %lf. \n",totalreadbytes, totalwritebytes, ((double) totalwritebytes)/((double)totaltime) );
	printf("Average Response time: %lf.\n", totaltime/totalrequests);
	return 0;
}