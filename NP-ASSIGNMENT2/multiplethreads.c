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
#include <pthread.h>

#define OFFSET_MAX 20

#define EXIT(msg,...) ({ printf( "Line:%d " msg, __LINE__, ##__VA_ARGS__); if (errno != 0) perror(""); exit(EXIT_FAILURE); })
#define try(expr,msg,...) ({ if ((expr) == -1) printf( "Line:%d " msg, __LINE__, ##__VA_ARGS__), perror(""), exit(EXIT_FAILURE); })
#define tryt(expr,msg,...) ({ if ((expr) != 0 ) printf( "Line:%d [PTHREAD]" msg, __LINE__, ##__VA_ARGS__), perror(""), exit(EXIT_FAILURE); })

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

struct sockaddr_in serveraddr; // declare it as global so threads can access
char  outfile[PATH_MAX];

void * thread_download_fragment( void * data ){
	_Request request = (_Request) data;
	printrequest(request);

	uint64_t offset = atol(request->offset);
	int sockfd, filefd, readbytes;
	char buffer[BUFSIZ]; 
	try( sockfd = socket(PF_INET, SOCK_STREAM , 0), "socket failed.");
	try( connect(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)), "connect failed." );
	try( send(sockfd, request, sizeof(Request), 0), "send failed." );
	try( filefd = open(outfile, O_CREAT| O_WRONLY, S_IRUSR| S_IWUSR), "open file failed." );
	try( lseek64(filefd, offset, SEEK_SET), "lseek64 failed." );
	while(1) {
		try( readbytes = read(sockfd, buffer, BUFSIZ) , "read failed." );
		if ( readbytes <= 0 ) break; // finish writing ...
		try( write(filefd, buffer, readbytes), "write failed." );
	}
	try( close(filefd), "close failed.");
	try( close(sockfd), "close failed.");
	return (void *)NULL;
}
int main(int argc, char *argv[]) {
	if ( argc != 6){
		printf("Format: executable server-ip server-port num-connections infile outfile\n");	
		exit(EXIT_FAILURE);
	}

	char infile[PATH_MAX];
	uint32_t connections = atoi(argv[3]); 
	if ( connections <= 0 ) printf("Invalid number of connections.\n"), exit(EXIT_FAILURE);
	uint64_t offsets[connections], sizes[connections], filesize;
	struct timeval starttime, endtime;
	double starttimedf, endtimedf;
	pthread_t threadid[connections];
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = inet_addr(argv[1]);
	serveraddr.sin_port = htons(atoi(argv[2]));
	Request requests[connections];
	strncpy(infile, argv[4], PATH_MAX);
	strncpy(outfile, argv[5], PATH_MAX);
	
	if ( checkiffileexist(outfile) ) remove(outfile);

	getfileinfo( &serveraddr, infile, &filesize);
	printf("Server sent: FILENAME: %s FILSIZE: %lu\n", infile, filesize);
	
	// Now update the offsets and sizes
	printf("Requesting file in fragments.\n");
	for ( int i = connections-1; i >= 0; i --){
		offsets[i] = (filesize * i)/ connections;
		sizes[i] = ( i == (connections -1) )? (filesize - offsets[i]): (offsets[i+1] - offsets[i]);
		writetorequest(&(requests[i]), 'F', infile, offsets[i], sizes[i]);
		printf("[%d] OFFSETS: %ld SIZES: %ld\n",i+1, offsets[i], sizes[i]);
	}

	gettimeofday(&starttime, NULL);
	// Now establish the sockets ...
	for ( int i=0; i <connections; i++) {
		tryt(pthread_create(&(threadid[i]), NULL, thread_download_fragment, (void *) &(requests[i])),"pthread_create failed.");
	}
	for ( int i=0; i < connections; i++){
		tryt(pthread_join(threadid[i], NULL),"pthread_join failed.");
	}
	gettimeofday(&endtime, NULL);
	starttimedf = starttime.tv_sec + ((double)starttime.tv_usec)/1000000.0;
	endtimedf = endtime.tv_sec + ((double)endtime.tv_usec)/1000000.0;
	double totaltime = endtimedf - starttimedf;
	printf("Total Response Time: %lf seconds.\n", totaltime );
	printf("File size: %lu Throughput: %lf. \n",filesize, ((double) filesize)/((double)totaltime) );
	return 0;
}