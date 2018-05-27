/*
	T Dinesh Ram Kumar
	2014A3A70302P
	Modelled after Lighttpd, supports only single thread though
	Consist of two units
	1. Cache manager: To improve response time ... All web pages are cached ...
		Currently supports one level of files 
	2. Web Server: Non-blocking call based webserver using epoll for performance ...
*/
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#define IP "127.0.0.1"
#define PATH "/var/www/html/"
#define PORT 80
#define LISTEN_BACKLOG 1000
#define SOCKADDR (struct sockaddr *)
#define MAX_EVENTS 100
#define MIN_REQUEST_SIZE 15
#define MAX_REQUEST_SIZE BUFSIZ
#define MAX_FDS 65536
#define DELIM " \n\t\r"
#define DEFAULTPATH "/index.html"
#define FOUND "HTTP/1.0 200 OK\n\n"
#define NOTFOUND "HTTP/1.0 404 Not Found\n\n"
#define SLOWCONNECTION "HTTP/1.0 200 OK\n\n<html><body><h1>SLOW CONNECTION CLOSED</h1><p>Try Again.</p></body></html>"
#define try(expr) if ( (expr) == -1 ) if(errno != EWOULDBLOCK && errno != EAGAIN) printf( "\033[031mLINE: %d STMT: %s\033[0m", __LINE__, #expr), perror(""), exit(EXIT_FAILURE);\
	 				else if (errno == EWOULDBLOCK && errno == EAGAIN) printf("WOULD BLOCK.\n");
#define tryNULL(expr) if ( (expr) == NULL ) printf( "\033[031mLINE: %d STMT: %s\033[0m", __LINE__, #expr), perror(""), exit(EXIT_FAILURE);
#define DEBUG1(msg, ...) printf("\033[033mLINE: %d " msg"\033[0m\n", __LINE__, ##__VA_ARGS__);
#define DEBUG2(msg, ...) printf("\033[034mLINE: %d " msg"\033[0m\n", __LINE__, ##__VA_ARGS__);

int main() {
	/*
		Cache Manager
		Notes: 
			1. ONLY ONE LEVEL OF FILES IS RECOGNIZED 
	*/
	DEBUG1("Cache Manager Started.");
	DIR *dir;
	int num_files = 0, file_index =0, file_fd, bytes_read, total_bytes;
	struct dirent *dir_entry;
	
	tryNULL (dir = opendir(PATH)); // Open directory
	try( chdir(PATH) ); // Change path to that directory
	while ( (dir_entry = readdir(dir) ) != NULL ){
		if ( dir_entry->d_type == DT_REG ){
			num_files ++ ;  // Keep track of number of files in directory
		}
	}
	rewinddir(dir); // rest the position to beginning
	struct {
		char  file_name[PATH_MAX];
		unsigned int   file_size;
		char *file_contents;
	} file_cache[num_files] ; // Allocate the cache structure

	while ( (dir_entry = readdir(dir))!= NULL ){
		if ( dir_entry->d_type == DT_REG ){
			snprintf(file_cache[file_index].file_name,PATH_MAX, "/%s", dir_entry->d_name); // copy the file name
			try( file_fd = open(dir_entry->d_name, O_RDONLY) );// open the file
			try( file_cache[file_index].file_size = lseek(file_fd, 0, SEEK_END)); // get the file size
			try( lseek(file_fd, 0, SEEK_SET) ); // reset the position to beginning
			file_cache[file_index].file_contents = malloc(file_cache[file_index].file_size * sizeof(char)); // allocate the cache buffer
			total_bytes = 0;
			while ( total_bytes < file_cache[file_index].file_size ){ // Load the file to buffer cache ..
				try( bytes_read = read( file_fd, file_cache[file_index].file_contents + total_bytes, file_cache[file_index].file_size - total_bytes ) ); 
				total_bytes += bytes_read ;
			}
			printf("\033[032mRead file: %s size:%u \033[0m\n", file_cache[file_index].file_name, file_cache[file_index].file_size);
			file_index ++; // Load the next file ..
		}
	}
	/*
		Asynchronous Web Server
		Note:
			1. Supports only single thread ...
	*/
	DEBUG1("Asynchronous Web Server Started.");
	int listen_fd, epoll_fd, conn_fd, total_request_count =0;
	int nfds, addrlen, fd_flags, read_bytes;
	char request_message[MAX_REQUEST_SIZE] ,dump_buffer[BUFSIZ];
	char *request_type, *request_file;
	struct sockaddr_in serveraddr, clientaddr;
	bzero(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = PF_INET;
	serveraddr.sin_addr.s_addr = inet_addr(IP);
	serveraddr.sin_port = htons(PORT);
	struct epoll_event ev, events[MAX_EVENTS];

	struct {
		int fileno; // the file which it is sending
		int sent_bytes; // the number of bytes already sent .
	} reply_info[MAX_FDS]; // This contains information about each client ...

	try(epoll_fd = epoll_create1(EPOLL_CLOEXEC));
	try(listen_fd = socket(AF_INET, SOCK_STREAM, 0));
	try(setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)));
	try(bind(listen_fd, SOCKADDR &serveraddr, sizeof(serveraddr)));
	try(listen(listen_fd, LISTEN_BACKLOG));
	ev.events = EPOLLIN;
	ev.data.fd = listen_fd;
	try(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev));
	while (1) {
		try( nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1) );
		// printf("TOTAL REQUEST COUNT: %d\n", total_request_count);
		for ( int i = 0; i < nfds ; i ++ ){
			if ( events[i].data.fd == listen_fd ){
				// IF NEW CONNECTION REQUEST ..
				addrlen = sizeof(clientaddr);
				try(conn_fd = accept(listen_fd, SOCKADDR &clientaddr, &addrlen) );
				DEBUG1("New Connection %s:%d", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
				total_request_count ++;
				// NON-BLOCKING CONNECTION ? 
				try(fd_flags = fcntl(conn_fd, F_GETFD, 0));
				try(fcntl(conn_fd, F_SETFD, fd_flags| O_NONBLOCK));
				ev.events = EPOLLIN | EPOLLONESHOT; // Wait for only one event then automatically remove it
				ev.data.fd = conn_fd;
				try(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &ev) );
			}
			else {
				// Just for debugging get info about client
				addrlen = sizeof(clientaddr);
				try( getpeername(events[i].data.fd, SOCKADDR &clientaddr, &addrlen) );
				
				// Some other FD ...
				if ( events[i].events & EPOLLIN ) {
					// If some request is sent ...
					try( read_bytes = read(events[i].data.fd, request_message, MAX_REQUEST_SIZE) );
					// write(1, request_message, read_bytes);
					if ( read_bytes < MIN_REQUEST_SIZE ){
						// If not read enough bytes in one go ...
						// deregister and close the connection 
						// Slow connection ...
						// Though not necessary (since one shot)
						DEBUG2("Slow Connection %s:%d Closed.", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
						try(write(events[i].data.fd, SLOWCONNECTION, sizeof(SLOWCONNECTION)));
						try(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, &ev)); // ev ignored ..
						try(close(events[i].data.fd));
					}
					else {
						// Enough bytes in request ...
						// Update the fileno of the request ...
						DEBUG1("Read Connection %s:%d Request.", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
						request_type = strtok(request_message, DELIM); // get the request type ...
						request_file = strtok(NULL, DELIM); // get the file path ...
						// Note: We dont worry about request type (Assumed GET) or HTTP version we stick to 1.0 
						if ( strcmp(request_type, "GET") == 0 ){
							DEBUG1("Connection %s:%d Requested file %s.", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port), request_file);
							if ( strcmp(request_file, "/") == 0 ){
								request_file = DEFAULTPATH;
							}
							reply_info[events[i].data.fd].fileno = -1; // If file not found default file...
							reply_info[events[i].data.fd].sent_bytes = 0; // initialize with 0
							for ( int n = 0; n < num_files; n ++ ){
								if ( strcmp(file_cache[n].file_name, request_file) == 0 ){
									reply_info[events[i].data.fd].fileno = n;
									break;
								}
							}
							// to rearm with new descriptor mask 
							ev.events = EPOLLOUT;
							ev.data.fd =  events[i].data.fd; 
							try(epoll_ctl(epoll_fd, EPOLL_CTL_MOD, events[i].data.fd, &ev));
						}
						else {
							DEBUG2("Connection %s:%d Unknown request %s.", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port), request_type);
							try(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, &ev)); // ev ignored ..
							try(close(events[i].data.fd));
						}
						
					}	
				}
				else if ( events[i].events & EPOLLOUT ){
					// Now time to write data 
					if ( reply_info[events[i].data.fd].fileno == -1 ){
						// If file was not found ..
						DEBUG2("Connection %s:%d File Not Found.", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
						try(write(events[i].data.fd, NOTFOUND, strlen(NOTFOUND)));
						try(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, &ev)); 
						try(close(events[i].data.fd));
						continue;
					}
					// Send the file if found ..
					if ( reply_info[events[i].data.fd].sent_bytes  == 0 ) {
						// WARNING: Assuming header sent in one go ...
						try( write( events[i].data.fd, FOUND, strlen(FOUND)) ); 
					}
					DEBUG1("Connection %s:%d Sending File.", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
					try( reply_info[events[i].data.fd].sent_bytes +=  write(events[i].data.fd, 
						file_cache[reply_info[events[i].data.fd].fileno].file_contents + reply_info[events[i].data.fd].sent_bytes,
						file_cache[reply_info[events[i].data.fd].fileno].file_size - reply_info[events[i].data.fd].sent_bytes) );
					// If all bytes send close the connection and remove from epoll 
					if ( reply_info[events[i].data.fd].sent_bytes == file_cache[reply_info[events[i].data.fd].fileno].file_size ){
						try(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, &ev));  
						// do {
						// 	try( read_bytes = recv(events[i].data.fd, dump_buffer, BUFSIZ, MSG_DONTWAIT) );
						// } while( read_bytes >= BUFSIZ ); // FLUSH OUT ANY REMAINING DATA else it sends reset flag...
						// Read all bytes availabe
						try(close(events[i].data.fd));
						DEBUG1("Connection %s:%d File Sent.", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
					}
				}
				else if ( events[i].events & EPOLLHUP || events[i].events & EPOLLERR  ) {
					// close the socket
					try(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, &ev)); 
					try(close(events[i].data.fd));
					DEBUG2("Connection %s:%d Client Closed Connection.", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
				}
			}
		}
	}
	try(close(listen_fd));
	return 0;
}

