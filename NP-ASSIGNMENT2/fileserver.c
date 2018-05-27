#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/sendfile.h>
#include <sys/wait.h>
#define OFFSET_MAX 20
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
	printf("\033[033mLINE: %d Request> type:%c msg:%s offset:%s size:%s\033[0m\n", __LINE__, request->type, request->msg, request->offset, request->size);
}
#define EXIT(msg,...) ({ printf( "Line:%d " msg, __LINE__, ##__VA_ARGS__), perror(""), exit(EXIT_FAILURE); })
#define try(expr,msg,...) ({ if ((expr) == -1) printf( "Line:%d " msg, __LINE__, ##__VA_ARGS__), perror(""), exit(EXIT_FAILURE); })
#define LISTEN_BACKLOG 5
int main(int argc, char const *argv[])
{
	if ( argc != 3 ){
		printf("FORMAT: executable ip port\n");
		exit(EXIT_FAILURE);
	}
	int sockfd, connfd, addrlen = sizeof(struct sockaddr_in), childfd, readbytes, filefd, options=1;
	struct sockaddr_in serveraddr, clientaddr;
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = inet_addr(argv[1]);
	serveraddr.sin_port = htons(atoi(argv[2]));
	Request request;
	uint64_t offset, size, sentbytes;
	char msg[PATH_MAX], type;
	struct stat fileinfo;
	// waitpid call later ...	
	try ( sockfd = socket(PF_INET, SOCK_STREAM, 0), "socket failed." );
	try ( setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (void *) &options, sizeof(options)), "setsockopt failed." );
	try ( bind( sockfd, (struct sockaddr *)&serveraddr, addrlen), "bind failed.");
	try ( listen(sockfd, LISTEN_BACKLOG), "listen failed." );
	while ( 1 ){
		try ( connfd = accept(sockfd, (struct sockaddr *) &clientaddr, &addrlen), "accept failed.");
		try ( childfd = fork(), "fork failed." );
		if ( childfd == 0 ){
			close(sockfd);
			printf("NEW CLIENT CONNECTED IP: \033[32m%s\033[0m PORT: \033[32m%d\033[0m\n",inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));		
			try ( readbytes = recv(connfd, &request, sizeof(request), MSG_WAITALL) ,"recv failed." ) ; // get entire request...
			if ( readbytes < sizeof(request) ) 
				close(connfd),EXIT("Invalid message request from client.");
			printrequest(&request);
			readfromrequest( &request, &type, msg, &offset, &size);
			switch ( type ){
				case 'I':
					if ( stat( msg, &fileinfo) == -1 ) {
						printf("FILE: %s (ERROR: %s) CLIENT %s:%d \n", msg, strerror(errno),
						 inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));					
						type = 'E'; // Indicate error
						strncpy(msg, strerror(errno), PATH_MAX);
						size = 0l;
						offset = 0l; 
					}
					else {
						printf("FILE: %s SIZE: %lu CLIENT IP: %s PORT: %d \n",msg, fileinfo.st_size, inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
						type = 'A'; // indicate answer
						size = fileinfo.st_size; // set the size..
						offset = 0l; 
					}
					writetorequest( &request, type, msg, offset, size);
					try ( send(connfd, &request, sizeof(request), 0), "send failed." );
					try ( close(connfd), "close failed." );
					break;
				case 'F': // get file segment..
					if ( (filefd = open(msg, O_RDONLY)) == -1 ) {
						printf("ERROR UNABLE TO OPEN FILE %s\n", msg);
						// if failed ... then just close the connection
						try ( close(connfd), "close failed." );
						// printf("file:%s open failed (%s) client: %s:%d \n", msg, strerror(errno), inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
						// type = 'E'; // Indicate error
						// strncpy(msg, strerror(errno), PATH_MAX);
						// size = 0l;
						// offset = 0l; 
						// writetorequest( &request, type, msg, offset, size);
						// try ( send(connfd, &request, sizeof(request), 0), "send failed." );
						// try ( close(connfd), "close failed." );
					}
					else {
						// if ok send back the request as response ...
						// try ( send(connfd, &request, sizeof(request), 0), "send failed." );
						// Now send the file ...
						uint64_t currsize = size;
						uint64_t curroffset = offset ;
						uint64_t totalsentbytes = 0;
						while ( 1 ) {
							// do some thing here ..
							try ( sentbytes = sendfile(connfd, filefd, &curroffset, currsize), "sendfile failed.");
							currsize = currsize - sentbytes;
							totalsentbytes += sentbytes; 
							if ( currsize > 0 ){
								printf("Send file sending again... \n");
							}
							else {
								break;
							}
						}
						printf("\n SENT FILE: %s SIZE:%ld OFFSET:%lu)\n."
							"NEXT EXPECTED OFFSET: %lu.\n"
							"SENT %lu BYTES TO CLIENT: %s:%d.\n", 
							msg, size, offset, curroffset, sentbytes,
							inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
						if ( totalsentbytes != size ) {
							printf("ERROR IN FILE TRANSFER NOT ALL REQUEST BYTES SENT.\n");
							perror("");
						}					
					}
					break;
				default:
					close(connfd); // don't reply if invalid response ..
			}
			exit(EXIT_SUCCESS);
		}
		close(connfd);
		while ( waitpid(-1, NULL, WNOHANG) > 0 ); // in case of zombies ...
	}
	try ( close(sockfd), "close failed." );
	return 0;
}