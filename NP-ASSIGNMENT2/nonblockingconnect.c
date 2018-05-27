/*
	 T Dinesh Ram Kumar
	 2014A3A70302P
	 This is a fileclient (Use the fileserver)
	This gets a file from server using required number of connections ...
*/

#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/time.h>
#include <poll.h>
#include <sys/uio.h>

#define OFFSET_MAX 20

#define EXIT(msg,...) ({ printf( "Line:%d " msg, __LINE__, ##__VA_ARGS__); if (errno != 0) perror(""); exit(EXIT_FAILURE); })
#define try(expr,msg,...) ({ if ((expr) == -1) printf( "Line:%d " msg, __LINE__, ##__VA_ARGS__), perror(""), exit(EXIT_FAILURE); })

struct request {
	char type;
	char msg[PATH_MAX];
	char offset[OFFSET_MAX];
	char size[OFFSET_MAX];	
};
// Pointer by leading _
typedef struct request Request;
typedef struct request * _Request; 

void readfromrequest(_Request request, char *type, char msg[], uint64_t *offset, uint64_t *size) {
	*type = request->type;
	strcpy(msg,request->msg);
	*offset = atol(request->offset);
	*size = atol(request->size);
}
void writetorequest(_Request request, char type, char msg[], uint64_t offset, uint64_t size){
	request->type = type;
	strcpy(request->msg,msg);
	sprintf(request->offset,"%lu",offset);
	sprintf(request->size,"%lu",size);
}
void printrequest(_Request request){
	printf(" LINE: %d Request> type:%c msg:%s offset:%s size:%s\n", __LINE__, request->type, request->msg, request->offset, request->size);
}

void getfileinfo(struct sockaddr_in *serveraddr, char filename[], uint64_t *size){
	int fd, recvbytes ;
	char type;
	uint64_t offset;
	Request request;
	writetorequest(&request, 'I', filename, 0l, 0l);
	try ( fd = socket(AF_INET, SOCK_STREAM, 0), "socket failed." );
	try ( connect(fd, (struct sockaddr *)serveraddr, sizeof(struct sockaddr_in)), "connect failed." ) ;
	try ( send(fd, &request, sizeof(request), 0), "send failed." );
	try ( recvbytes = recv(fd, &request, sizeof(request), MSG_WAITALL), "recv failed." ); // get entire response
	if ( recvbytes < sizeof(request) ) EXIT("Invalid response.");
	try ( close(fd) ,"close failed." );
	readfromrequest(&request, &type, filename, &offset, size);
	if ( type == 'E' ) {
		EXIT("Server returned an error.(\033[031m%s\033[0m)\n", filename);
	}
	// return with the size of file for offset computations ...
}

bool checkiffileexist(char filename[]) {
	struct stat fileinfo;
	if ( stat( filename, &fileinfo ) == -1 ) 
		return false;
	return true;
}

int main(int argc, char *argv[]) {
	if ( argc != 6){
		printf("Format: executable server-ip server-port num-connections infile outfile\n");	
		exit(EXIT_FAILURE);
	}
	char infile[PATH_MAX], outfile[PATH_MAX];
	uint32_t connections = atoi(argv[3]); if ( connections <= 0 ) printf("Invalid number of connections.\n"), exit(EXIT_FAILURE);
	int sockfds[connections], filefds[connections], readbytes, writebytes, addrlen =sizeof(struct sockaddr_in), flags, pendingconnections= connections, pendingfiles= connections, optlen= sizeof(int), option;
	struct sockaddr_in serveraddr;
	uint64_t offsets[connections], sizes[connections], filesize;
	char buffers[connections][BUFSIZ]; // this is the buffers to read data to before writing them back to file..
	uint32_t readfrom[connections], writeto[connections];
	struct pollfd pollfds[connections*2]; // half of them for sockets remaining for files ...
	struct timeval starttime, endtime;
	double starttimedf, endtimedf;
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = inet_addr(argv[1]);
	serveraddr.sin_port = htons(atoi(argv[2]));
	
	strncpy(infile, argv[4], PATH_MAX);
	strncpy(outfile, argv[5], PATH_MAX);
	if ( checkiffileexist(outfile) ) remove(outfile); // if already exists remove it ...

	getfileinfo( &serveraddr, infile, &filesize);
	printf("Server sent: FILENAME: %s FILSIZE: %lu\n", infile, filesize);
	// Now update the offsets and sizes
	printf("Requesting file in fragments.\n");
	for ( int i = connections-1; i >= 0; i --){
		offsets[i] = (filesize * i)/ connections;
		sizes[i] = ( i == (connections -1) )? (filesize - offsets[i]): (offsets[i+1] - offsets[i]);
		printf("[%d] OFFSETS: %ld SIZES: %ld\n",i+1, offsets[i], sizes[i]);
	}

	gettimeofday(&starttime, NULL);
	// Now establish the sockets ...
	for ( int i= 0; i <connections; i++){
		try( sockfds[i] = socket(PF_INET, SOCK_STREAM , 0), "socket failed.");
		pollfds[i].fd = sockfds[i];
		try ( flags = fcntl(sockfds[i], F_GETFL, 0) , "fcntl: getfl failed,");
		try( fcntl(sockfds[i], F_SETFL, flags | O_NONBLOCK), "fcntl: setfl failed.");
		if ( connect(sockfds[i], (struct sockaddr *)&serveraddr, addrlen) == -1) {
			if ( errno != EINPROGRESS ) {
				EXIT("connect failed.");
			}
			pollfds[i].events = POLLOUT; // check for writability ...
		}
		else 
			pollfds[i].events = POLLIN; // check for readability

		readfrom[i] = 0; // this contains the pointer to where to start reading to write to file ...
		writeto[i] = 0;
		// also open the file (several times with different offsets) ...
		try ( filefds[i] = open(outfile, O_CREAT| O_WRONLY, S_IRUSR| S_IWUSR), "open file failed." );
		try ( lseek64(filefds[i], offsets[i], SEEK_SET), "lseek64 failed." ); // set the position to start writing...
		try ( flags = fcntl(filefds[i], F_GETFL, 0) , "fcntl: getfl failed,");
		try( fcntl(filefds[i], F_SETFL, flags | O_NONBLOCK), "fcntl: setfl failed.");
		pollfds[i+connections].fd = filefds[i];
		pollfds[i+connections].events = POLLOUT;
	}
	// Now poll till no more events possible ...
	// While some connection is yet to write or some file has 
	bool bufferempty[connections];
	for ( int i =0; i < connections; i ++)
		bufferempty[i] = true;
	while ( pendingconnections > 0 || pendingfiles > 0){
		
		try ( poll( pollfds, 2*connections, -1), "poll failed." );
		for ( int i = 0; i < connections; i ++) {
			// this is to check for sockets
			if ( pollfds[i].revents & POLLOUT ) {
				// if some some socket connect done or failed...
				try ( getsockopt(pollfds[i].fd , SOL_SOCKET, SO_ERROR, &option, &optlen) , "getsockopt failed." );
				if ( option == 0 ){
					Request request;
					pollfds[i].events = POLLIN; // start reading later on..
					writetorequest(&request, 'F', infile, offsets[i], sizes[i]);
					printf("SENT REQUEST %d %c %s OFFSET:%s SIZE:%s\n",i, request.type, request.msg, request.offset, request.size );
					// Make one call block ... and send the request ...
					try ( flags = fcntl(pollfds[i].fd, F_GETFL, 0) , "fcntl: getfl failed,");
					try( fcntl(pollfds[i].fd, F_SETFL, flags & (~O_NONBLOCK) ), "fcntl: setfl failed.");
					try ( send( pollfds[i].fd, &request, sizeof(request), 0) , "send failed.");
					try( fcntl( pollfds[i].fd, F_SETFL, flags|O_NONBLOCK), "fcntl: setfl failed.");
				}
				else {
					EXIT("some error happened while connecting..\n");
				}

			}
			// Now rather use readv to specify two buffers .. one at start and other at the end ...
			
			else if ( pollfds[i].revents & POLLIN ) {
				// <-------------ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
				if ( writeto[i] > readfrom[i] ) {
					// if writeto is ahead of readfrom
					// -------- r -- w ------- 
					// Then from 0 - r and w -- BUFSIZ
					
					struct iovec iov[2];
					iov[0].iov_base = &(buffers[i][writeto[i]]); // start from after base ..
					iov[0].iov_len = BUFSIZ - writeto[i];
					iov[1].iov_base = &(buffers[i][0]);
					iov[1].iov_len = readfrom[i]; //  ???? readfrom points to a location ( write till before it ...)
					try ( readbytes = readv(pollfds[i].fd, iov, 2) ,"readv failed." );
					writeto[i] = ( writeto[i] + readbytes ) % BUFSIZ;
					if ( writeto[i] == readfrom[i] ) {
						// then written one round ..
						// the buffer is full and we need read to write to happen ...
						bufferempty[i] = false;
					}
					if ( readbytes == 0 ) {
						pendingconnections -- ; // remove one connection ...
						try ( close(pollfds[i].fd), "close failed." );
						pollfds[i].fd = -pollfds[i].fd; // negate it to ignore it ...
					}
				}
				else if ( writeto[i] == readfrom[i] ) {
					if ( bufferempty[i] ){ // if buffer is empty ...
						struct iovec iov[2];
						iov[0].iov_base = &(buffers[i][writeto[i]]); // start from after base ..
						iov[0].iov_len = BUFSIZ - writeto[i];
						iov[1].iov_base = &(buffers[i][0]);
						iov[1].iov_len = readfrom[i]; //  ???? readfrom points to a location ( write till before it ...)
						try ( readbytes = readv(pollfds[i].fd, iov, 2) ,"readv failed." );
						writeto[i] = ( writeto[i] + readbytes ) % BUFSIZ;
						if ( writeto[i] == readfrom[i] ) {
							// then written one round ..
							// the buffer is full and we need read to write to happen ...
							bufferempty[i] = false;
						}
						if ( readbytes == 0 ) {
							pendingconnections -- ; // remove one connection ...
							try ( close(pollfds[i].fd), "close failed." );
							pollfds[i].fd = -pollfds[i].fd; // negate it to ignore it ...
						}
					}
					else {
						// if buffer is full ....
						// do nothing ...
					}
					
				}
				else {
					/// --------- w ---- r-------
					struct iovec iov[1];
					iov[0].iov_base = &(buffers[i][writeto[i]]); // start from after base ..
					iov[0].iov_len = readfrom[i] - writeto[i];
					try ( readbytes = readv(pollfds[i].fd, iov, 1) ,"readv failed." );
					writeto[i] = ( writeto[i] + readbytes ) % BUFSIZ;
					if ( writeto[i] == readfrom[i] ) {
						// then written one round ..
						// the buffer is full and we need read to write to happen ...
						bufferempty[i] = false;
					}
					if ( readbytes == 0 ) {
						pendingconnections -- ; // remove one connection ...
						try ( close(pollfds[i].fd), "close failed." );
						pollfds[i].fd = -pollfds[i].fd; // negate it to ignore it ...
					}
				}				
			}
			else if ( pollfds[i].revents & POLLERR ){
				EXIT("error in sockfd.");
			}
		}
		for ( int i = 0; i < connections; i ++) {
			// this is to check for files ...
			if ( pollfds[i+connections].revents & POLLOUT ) {

				if ( writeto[i] > readfrom[i] ) {
					// if writeto is ahead of readfrom
					// -------- r -- w ------- 
					// Then from 0 - r and w -- BUFSIZ
					struct iovec iov[1];
					iov[0].iov_base = &(buffers[i][readfrom[i]]); // start from after base ..
					iov[0].iov_len = writeto[i] - readfrom[i];
					try ( writebytes = writev(pollfds[i+connections].fd, iov, 1), "writev failed." );
					readfrom[i] = (readfrom[i] + writebytes) %BUFSIZ;
					if ( writeto[i] == readfrom[i] && pollfds[i].fd < 0 ) {
						try ( close(pollfds[i+connections].fd), "close failed.");
						pollfds[i+connections].fd = -pollfds[i+connections].fd;
						pendingfiles --;
					}
					if ( writebytes > 0)
						bufferempty[i] = true ;

				}
				else if ( writeto[i] == readfrom[i] ){
					// either buffer is full or empty
					if ( bufferempty[i] ){
						// nothing to write to file ...
					}
					else {
						struct iovec iov[2];
						iov[0].iov_base = &(buffers[i][readfrom[i]]); // start from after base ..
						iov[0].iov_len = BUFSIZ - readfrom[i];
						iov[1].iov_base = &(buffers[i][0]);
						iov[1].iov_len = writeto[i]; //  ???? readfrom points to a location ( write till before it ...)
						try ( writebytes = writev(pollfds[i+connections].fd, iov, 2), "writev failed." );
						readfrom[i] = (readfrom[i] + writebytes) %BUFSIZ;
						if ( writeto[i] == readfrom[i] && pollfds[i].fd < 0 ) {
							try ( close(pollfds[i+connections].fd), "close failed.");
							pollfds[i+connections].fd = -pollfds[i+connections].fd;
							pendingfiles --;
						}
						if ( writebytes > 0)
							bufferempty[i] = true ;
					}
				}
				else {
					// ---- w --- r ------
					struct iovec iov[2];
					iov[0].iov_base = &(buffers[i][readfrom[i]]); // start from after base ..
					iov[0].iov_len = BUFSIZ - readfrom[i];
					iov[1].iov_base = &(buffers[i][0]);
					iov[1].iov_len = writeto[i]; //  ???? readfrom points to a location ( write till before it ...)
					try ( writebytes = writev(pollfds[i+connections].fd, iov, 2), "writev failed." );
					readfrom[i] = (readfrom[i] + writebytes) %BUFSIZ;
					if ( writeto[i] == readfrom[i] && pollfds[i].fd < 0 ) {
						try ( close(pollfds[i+connections].fd), "close failed.");
						pollfds[i+connections].fd = -pollfds[i+connections].fd;
						pendingfiles --;
					}
					if ( writebytes > 0)
						bufferempty[i] = true ;
				}
			}
			else if ( pollfds[i+connections].revents & POLLERR ){
				EXIT("error in filefd..");
			}

		}

	}
	gettimeofday(&endtime, NULL);
	starttimedf = starttime.tv_sec + ((double)starttime.tv_usec)/1000000.0;
	endtimedf = endtime.tv_sec + ((double)endtime.tv_usec)/1000000.0;
	double totaltime = endtimedf - starttimedf;
	printf("Total Response Time: %lf seconds.\n", totaltime );
	printf("File size: %lu Throughput: %lf. \n",filesize, ((double) filesize)/((double)totaltime) );

	return 0;
}