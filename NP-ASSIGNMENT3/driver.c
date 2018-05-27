#include "nmb.h"

#define try(expr) if((expr) == -1) fprintf(stderr,"\033[031mERROR:"),perror(""), fprintf(stderr,"\033[0m"),exit(EXIT_FAILURE);

int main( int argc, char *argv[] ) {
	if ( argc != 4 ){
		printf("FORMAT: executable local-port other-ip other-port\n");
		return -1;
	}
	int sockfd ;
	try ( sockfd = nmb_msgget(atoi(argv[1])) );
	char message[501];
	int pid ;
	try ( pid = fork() );
	if ( pid == 0 ){
		// child process
		while (1  ){
			printf("ENTER MSG: ");
			fgets( message, 500, stdin);
			message[500] = '\0';
			message[strlen(message)-1] =  (message[strlen(message)-1]=='\n')? '\0':message[strlen(message)-1];
		 	try( nmb_msgsnd(sockfd, argv[2], atoi(argv[3]), message) ) ;
		}
		exit(EXIT_SUCCESS);
	}
	while( nmb_msgrcv(sockfd, message, 501)  >  0 ){
		printf("GOT: %s\n", message);
		printf("ENTER MSG: ");
		fflush(stdout);
	}
	printf("DONE. KILLIING PID:%d\n", pid);
	kill(pid, SIGINT);
	close(sockfd);
		
	return 0;
}