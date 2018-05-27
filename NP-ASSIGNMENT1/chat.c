/*
	T Dinesh Ram Kumar
	2014A3A70302P
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ipc.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#define PORT 5000
#define IPADDRESS "0.0.0.0"
#define LISTEN_BACKLOG 5
#define MAX_USERS 1024
#define MAX_USERNAME_LEN 100
#define MAX_PASSWORD_LEN 100
#define MAX_MSG_SIZE 1024 // this defines the maximum size of message ..
#define TRY(_EXPR_,_EXIT_,_MSG_) ({ if ( ((int)(_EXPR_)) == -1) { perror(_MSG_); if (_EXIT_) exit(EXIT_FAILURE);} })
#define try(_EXPR_) TRY(_EXPR_,1,"") 
#define CHILD(pid) ( pid == 0 )
#define IS(str1,str2) (strcmp(str1,str2) == 0)
#ifndef _NO_DEBUG_
	#define DEBUG(FORMAT,...) printf("\033[36mDEBUG [Line: %d]"FORMAT"\033[0m\n",__LINE__,##__VA_ARGS__);
#else 
	#define DEBUG(FORMAT,...)
#endif
struct  _UserInfo {
	char username[MAX_USERS][MAX_USERNAME_LEN+1] ;
	char password[MAX_USERS][MAX_PASSWORD_LEN+1] ;
	// Currently Login is allowed from only one place to avoid inconsistences 
	// That is reduce the complexity of handling various cases ...
	bool online[MAX_USERS]; // this contains the status of online users
	bool blocked[MAX_USERS][MAX_USERS]; // this contains the list of blocked users
	bool hide[MAX_USERS][MAX_USERS]; // this contains the hiding users
	bool follow[MAX_USERS][MAX_USERS]; // this contains the list of follow users
	time_t lastlogin[MAX_USERS]; // this contains the last login
	char ipaddress[MAX_USERS][INET_ADDRSTRLEN]; // this contains the place of last login
	bool isvalid[MAX_USERS]; // this checks if user record is valid or not
};

struct _UserInfo * Users ;
int msqid, shmid, semid ; // All the resources
struct sembuf waitoperation[1], postoperation[1];
int clientmsghandlerpid ; // This is used by client to remove the Message listener when exiting
union {  int val ;  struct semid_ds * buf ; unsigned short * array ; } setval ; 
// NOTE: To enable for multisession Use named resources (by Means of KEY)...
// And Don't Deallocate them ..
// Also USER IPC_EXCL to check if resource exist ...
// If exists then just get them without initializing ...
// Else initialize ...
void allocateresources() {
	struct msqid_ds msqbuf ;

	// shared memory
	try ( shmid = shmget(IPC_PRIVATE, sizeof(struct _UserInfo), IPC_CREAT| 0600) ) ;
	if (( Users = (struct _UserInfo *) shmat(shmid, (void *)NULL, 0)) == (void *)-1) perror(""), exit(EXIT_FAILURE) ;
	for ( int i = 0 ; i < MAX_USERS; i ++) Users->isvalid[i] = false ; // Invalidate all users...
	// semaphore
	try ( semid = semget(IPC_PRIVATE, 1, IPC_CREAT| 0600) ) ;
	waitoperation[0].sem_num = 0 ; waitoperation[0].sem_op = -1 ; waitoperation[0].sem_flg = 0 ;
	postoperation[0].sem_num = 0 ; postoperation[0].sem_op = 1 ; postoperation[0].sem_flg = 0 ;
	setval.val = 1 ; 
	try ( semctl ( semid, 0, SETVAL, setval ) );
	// message queue
	try ( msqid = msgget(IPC_PRIVATE,IPC_CREAT|0600) ) ;
	try ( msgctl(msqid,IPC_STAT,&msqbuf) );
	msqbuf.msg_qbytes = 1048576 ;// Set the message queue size to 1MB
	try ( msgctl(msqid,IPC_SET,&msqbuf) );
	DEBUG("SYSTEM V Allocated Resource IDs SHM:%d SEM:%d MSQ:%d.",shmid,semid,msqid);
}

void dellocateresources() {
	TRY ( shmdt(Users) , 0, "Deattaching shared memory failed." ) ;
	TRY ( shmctl(shmid,IPC_RMID,NULL) , 0, "Removing shared memory failed.") ;
	TRY ( semctl(semid, 0, IPC_RMID), 0, "Removing Semaphore failed." );
	TRY ( msgctl(msqid,IPC_RMID,NULL), 0, "Removing Message queue failed." ) ;
	DEBUG("SYSTEM V Deallocated Resource IDs SHM:%d SEM:%d MSQ:%d.",shmid,semid,msqid);
}

// Call this on exit or leave as well
void killclientmessagehandler() { 
	DEBUG("SENDING KILL SIGNAL... to %d\n",clientmsghandlerpid );
	kill(clientmsghandlerpid,SIGQUIT); 
}


void signalhandler1( int signo ) {
	DEBUG("Exiting Server Process [pid: %d].",getpid());
	dellocateresources();
	//kill(0 ,SIGINT);
	exit(EXIT_SUCCESS);
} 

void signalhandler2(int signo ){
	DEBUG("Exiting Client Process [pid: %d]. \n",getpid());
	killclientmessagehandler();
	exit(EXIT_SUCCESS);
}

void signalhandler3(int signo) {
	DEBUG("Exiting Client Message Handler Process [pid: %d]. \n",getpid());
	exit(EXIT_SUCCESS);
}

void signalhandler4( int signo ) {
	DEBUG("Exiting ADMIN Process [pid: %d].",getpid());
	exit(EXIT_SUCCESS);
} 

char* getnextstr(char **strptr,char *delim) {
	char *token ;
	while ( (token = strsep(strptr,delim)) != NULL ) if ( strlen(token) != 0 ) break ;  ;
	return token ;
}

long clientmessagehandle( int userid ) {
	long hash = 5381 ; int c;
	char *str = Users->username[userid];
	while ( c= *str++) {
		hash = ((hash << 5) + hash) + c; 
	}
	hash = ( hash % 1024 ) + (userid + 1) << 10 ;
	return hash ;
}
struct msgbuf {
	long mtype;
	bool bcast;
	bool notification;
	char sender[MAX_USERNAME_LEN+1];
	char message[];
};

void handleclientmessages( FILE *sfp , int userid ) {
	DEBUG("Handling Message for client %s[%d].",Users->username[userid],userid);
	long cmh = clientmessagehandle(userid) ;
	int nb ;
	struct msgbuf * msgptr ;
	try ( clientmsghandlerpid = fork() ) ;
	
	#define SENDMSG(msg) ({\
		if (msg->notification) fprintf(sfp, "%s\r\n" ,msg->message);\
		else if (msg->bcast) fprintf(sfp,"[%s|BCAST)> %s\r\n", msg->sender, msg->message);\
		else fprintf(sfp,"[%s)> %s\r\n", msg->sender, msg->message);\
		fflush(sfp); })

	if ( CHILD(clientmsghandlerpid) ) {
		signal(SIGINT, signalhandler3);	signal(SIGQUIT, signalhandler3);
		msgptr = (struct msgbuf *) malloc(sizeof(struct msgbuf) + MAX_MSG_SIZE + 1);
		// if child process listen for any incoming messages and send them client..
		while ( msgrcv(msqid, msgptr, sizeof(struct msgbuf)+ MAX_MSG_SIZE+ 1, cmh, MSG_NOERROR) > 0 ) {
			// check if file descriptor is open first ...
			if ( fcntl(fileno(sfp), F_GETFD) == -1 ) {
				// Just a check ... Either way its gonna get killed...
				// if file descriptor is closed .. then push back the message ...
				// Note: Message is Dropped if message queue is full ...
				// This is not executed ...
				// Or is it ?
				try ( msgsnd(msqid, msgptr, sizeof(struct msgbuf)+ strlen(msgptr->message)+ 1, IPC_NOWAIT) );
				exit(EXIT_SUCCESS);
			}
			else {
				DEBUG("%s ->> %s [%c] MSG: %s", msgptr->sender, Users->username[userid], ((msgptr->notification)?'N':((msgptr->bcast)?'B':'T')), msgptr->message);
				SENDMSG(msgptr);
			}
		}
		exit(EXIT_SUCCESS);
	}
	// This causes problem when same session multiple users login ..
	//atexit(killclientmessagehandler); // this is used to kill the process if client is exiting ...
}

void handleclient(int cfd) {
	#define WAIT ( try ( semop(semid, waitoperation, 1) ) );
	#define POST ( try ( semop(semid, postoperation, 1) ) );
	char rbuf[BUFSIZ], *bufptr, *cmd, *username, *password, *recipient, *manyrecipients[MAX_USERS], *message, noticemessage[MAX_USERNAME_LEN+30], ipaddress[INET_ADDRSTRLEN], timestr[40] ;
	int nb, caddrlen = sizeof(struct sockaddr_in), userid, usercount, recipientuserid, msgsize  ; bool loggedin = false, isusernameavailable, isvaliduser, validrcount ;
	struct sockaddr_in clientaddr ;
    struct tm* tm_info;
    struct msgbuf * msgptr ;
	FILE *sfp = fdopen(cfd,"w");  
	getpeername(cfd,(struct sockaddr *) &clientaddr,&caddrlen) ; // Ignoring the errors
	strcpy(ipaddress,inet_ntoa(clientaddr.sin_addr)); 

	#define SEPRINTF(FORMAT,...) ({ fprintf(sfp, "ERROR: %d " FORMAT "\r\n" ,__LINE__,##__VA_ARGS__);fflush(sfp); })
	#define SOPRINTF(FORMAT,...) ({ fprintf(sfp, "OKAY: %d " FORMAT "\r\n" ,__LINE__,##__VA_ARGS__);fflush(sfp); })
	#define FOLLOWBLOCKHIDEBLOCK(_FIELD_,_VALUE_,_VALID_USER_STMT_,_INVALID_USER_STMT_) ({\
		if ( ! loggedin ) {\
			SEPRINTF("Login is Required.");\
			continue ;\
		}\
		if ( (recipient = getnextstr(&bufptr," \t\n\r")) == NULL ) SEPRINTF("Recipient not entered.");\
		else {\
			isvaliduser = false ;\
			for ( int i= 0 ; i < MAX_USERS; i++) {\
				if ( Users->isvalid[i] && IS ( Users->username[i], recipient ) ) {\
					WAIT ;\
					Users->_FIELD_[userid][i] = _VALUE_  ;\
					POST ;\
					isvaliduser = true ;\
					break ;\
				}\
			}\
			if ( isvaliduser ) SOPRINTF(_VALID_USER_STMT_ " %s.", recipient);\
			else SEPRINTF(_INVALID_USER_STMT_ " %s.", recipient);\
		}\
	})

	while ( (nb = read(cfd, rbuf, BUFSIZ)) > 0) {
		//write(cfd, rbuf, nb);
		bufptr = rbuf ; // point to beginning of the buffer
		cmd = getnextstr(&bufptr," \t\n\r");
		if ( cmd == NULL ) continue ; // if no command ..
		if ( IS(cmd,"JOIN") ) {
			if ( loggedin ) {
				// if already logged in ...
				SEPRINTF("Already Logged in as '%s'.",Users->username[userid]);
				continue;
			}
			if ( (username = getnextstr(&bufptr," \t\n\r")) == NULL ) SEPRINTF("Username not entered.");
			else if ( strlen(username) > MAX_USERNAME_LEN ) SEPRINTF("Username too long.");
			else if ( IS(username,"ADMIN") ) SEPRINTF("Username not available."); // Username can't be admin
			else if ( (password = getnextstr(&bufptr," \t\n\r")) == NULL ) SEPRINTF("Password not entered.");
			else if ( strlen(password) > MAX_PASSWORD_LEN ) SEPRINTF("Password too long.");
			else {
				isusernameavailable = true;
				// Check if already if user name taken ...
				for ( int i=0 ; i < MAX_USERS; i++) {
					if ( Users->isvalid[i] ) {
						if ( IS(Users->username[i], username) ) {
							SEPRINTF("Username already taken.");
							isusernameavailable = false ;
							break ;
						}
					}
				}
				if ( ! isusernameavailable ) continue ; // if user name not available don;t create an account ..
				// Check if space available then create one 
				for ( int i=0 ; i < MAX_USERS; i++) {
					if ( ! Users->isvalid[i] ) {
						WAIT ;
						strcpy(Users->username[i],username);
						strcpy(Users->password[i],password);
						Users->online[i] = true ;
						for ( int j = 0; j < MAX_USERS; j ++) 
							Users->blocked[i][j] = false, Users->blocked[j][i] = false,
							Users->hide[i][j] = false, Users->hide[j][i] = false,
							Users->follow[i][j] = false, Users->follow[j][i] = false ;
						Users->lastlogin[i] = time(NULL);
						strcpy(Users->ipaddress[i], ipaddress);
						Users->isvalid[i] = true ;
						POST ;
						loggedin = true ; // Login that user ...
						userid = i ;
						SOPRINTF("Successfully created a new account.");
						handleclientmessages( sfp , userid ); // Also start listening for messages ...
						break ; // Break out so that only one user is created ...
					}
				}
				if ( loggedin ) SOPRINTF("Welcome %s.", username);
				else SEPRINTF("Unable to a new account. Too many users.");
			} 
				
		}
		else if ( IS(cmd,"USER") ) {
			if ( loggedin ) {
				// if already logged in ...
				SEPRINTF("Already Logged in as '%s'.",Users->username[userid]);
				continue;
			}
			if ( (username = getnextstr(&bufptr," \t\n\r")) == NULL ) SEPRINTF("Username not entered.");
			else if ( strlen(username) > MAX_USERNAME_LEN ) SEPRINTF("Username too long.");
			else if ( (password = getnextstr(&bufptr," \t\n\r")) == NULL ) SEPRINTF("Password not entered.");
			else if ( strlen(password) > MAX_PASSWORD_LEN ) SEPRINTF("Password too long.");
			else {
				isvaliduser = false ;
				for ( int i = 0 ; i < MAX_USERS; i ++) {
					if ( Users->isvalid[i] ) {
						if ( IS(Users->username[i],username) && IS(Users->password[i],password) ) {
							if ( Users->online[i] ) {
								SEPRINTF("User Already Logged in somewhere else.");
								isvaliduser = true ;
								break ;
							}
							else {
								WAIT ;
								Users->online[i] = true ;
								tm_info = localtime(&(Users->lastlogin[i])); strftime(timestr, 40, "%a %d-%b-%Y %H:%M:%S", tm_info);
								SOPRINTF("Welcome %s.\n Last Login: %s From %s.", username, timestr, Users->ipaddress[i]);  
								Users->lastlogin[i] = time(NULL); // Update time and ip of login ...
								strcpy(Users->ipaddress[i], ipaddress);
								POST ;
								loggedin = true ;
								userid = i ;
								isvaliduser = true ;

								handleclientmessages( sfp , userid ); // Also start listening for messages ...
								// Notify the logged in user of the status of users it follows ..
								for ( int j=0; j< MAX_USERS; j++) {
									if ( Users->isvalid[j] && !(Users->hide[j][i] || Users->hide[i][j]) && Users->follow[i][j] && Users->online[j] ){
										SOPRINTF("Notification: User '%s' is Online.",Users->username[j]);
									} 
								}

								// Now send message to all those following if they are not hiding..
								for ( int j=0; j< MAX_USERS; j++) {
									if ( Users->isvalid[j] && !(Users->hide[j][i] || Users->hide[i][j]) && Users->follow[j][i] && Users->online[j] ) {
										// if some one following the user send message
										//if ( Users->follow[j][i] ) SOPRINTF(" ** User %s has come online **", username);
										// IMPLEMENT THIS WITH SEND .....
										// LATER ....
										// SEND a NOTIFICATION ...
										///
										sprintf(noticemessage,"NOTIFICATION: User %s Online.",Users->username[userid]);
										msgsize = sizeof(struct msgbuf) + strlen(noticemessage) + 1;
										msgptr = (struct msgbuf *) malloc( msgsize );
										msgptr->mtype = clientmessagehandle(j);
										msgptr->bcast = false ;
										msgptr->notification = true ;
										strcpy(msgptr->sender,Users->username[userid]);
										strcpy(msgptr->message, noticemessage);
										if ( msgsnd(msqid, msgptr, msgsize, IPC_NOWAIT) == -1) 
											SEPRINTF("Sending Notification to Follower %s failed.",Users->username[j]);
										else 
											SOPRINTF("Notification Sent to %s.",Users->username[j]);
										free(msgptr);

										///
									}
								}
								break; 
							}
						}
					}
				}
				if ( !isvaliduser ) SEPRINTF("Not Valid Username or Password.");
			}
		}
		else if ( IS(cmd,"LIST") ) {
			if ( loggedin ) {
				usercount = 0 ;
				for ( int i= 0; i < MAX_USERS; i ++) {
					if ( Users->isvalid[i] ) {
						usercount ++ ;
						if ( i != userid ) {
							if ( Users->hide[i][userid] || Users->hide[userid][i] )  
								SOPRINTF("USER: %s [%c] %c",Users->username[i],'N', ((Users->blocked[i][userid]||Users->blocked[userid][i])?'B':' ') );
							else SOPRINTF("USER: %s [%c] %c",Users->username[i],(Users->online[i]?'Y':'N'), ((Users->blocked[i][userid]||Users->blocked[userid][i])?'B':' '));
						}
						else {
							SOPRINTF("USER: %s [C]", Users->username[i]);
						}
					}
				}
				SOPRINTF("Total %d Users.",usercount);
			}
			else {
				SEPRINTF("Login is Required.");
			}
		}
		else if ( IS(cmd,"SEND") ) {
			if ( !loggedin ) {
				SEPRINTF("Login is Required.");
				continue ;
			}

			if ( (recipient = getnextstr(&bufptr," \t\n\r")) == NULL ) SEPRINTF("Recipient not entered.");
			else if ( (message = getnextstr(&bufptr,"\n\r")) == NULL ) SEPRINTF("Message not entered.");
			else if ( strlen(message) > MAX_MSG_SIZE ) SEPRINTF("Message not sent. Message too long.");
			else {
				isvaliduser = false ;
				// Send the message if not blocked ...
				for ( int i = 0; i < MAX_USERS; i ++) {
					if (  Users->isvalid[i] && IS(Users->username[i],recipient) ) {
						isvaliduser = true ;
						if ( Users->blocked[i][userid] || Users->blocked[userid][i] )
							SEPRINTF("Sending Message to Blocked Recipient %s.",recipient);
						else {
							// Send the message ..
							msgsize = sizeof(struct msgbuf) + strlen(message) + 1;
							msgptr = (struct msgbuf *) malloc( msgsize );
							msgptr->mtype = clientmessagehandle(i);
							msgptr->bcast = false ;
							msgptr->notification = false ;
							strcpy(msgptr->sender,Users->username[userid]);
							strcpy(msgptr->message, message);
							if ( msgsnd(msqid, msgptr, msgsize, IPC_NOWAIT) == -1) 
								SEPRINTF("Sending Message to Recipient %s failed. Try again later.",recipient);
							else 
								SOPRINTF("Message Sent to %s.",recipient);
							free(msgptr);
						}
						break ;
					}
				}
				if ( !isvaliduser ) SEPRINTF("Sending Message to Invalid Recipient %s.",recipient); 
			}
		}
		else if ( IS(cmd,"MCAST") || IS(cmd,"MULTICAST") ) {
			if ( !loggedin ) {
				SEPRINTF("Login is Required.");
				continue ;
			}

			if ( (recipient = getnextstr(&bufptr," \t\n\r")) == NULL ) SEPRINTF("Recipient count not entered.");
			else if ( (usercount = atoi(recipient)) <= 0 || usercount > MAX_USERS ) SEPRINTF("Recipient count is not valid or exceed maximum.");
			else {
				validrcount = true ;
				// Now get the recipients..
				for ( int i = 0; i < usercount; i ++) {
					if ( (manyrecipients[i] = getnextstr(&bufptr," \t\n\r")) == NULL ) {
						validrcount= false ;
						SEPRINTF("Recipients and count not matching.");
						break;
					}
				}
				if ( validrcount ) {
					// Send the message to all those recipients ..
					if ( (message = getnextstr(&bufptr,"\n\r")) == NULL ) SEPRINTF("Message not entered for recipients.");
					else if ( strlen(message) > MAX_MSG_SIZE ) SEPRINTF("Message not sent. Message too long.");
					else {
						for ( int i = 0 ; i < usercount; i++) {
							isvaliduser = false ;
							for ( int j = 0 ; j < MAX_USERS; j ++) {
								if ( Users->isvalid[j] && IS(Users->username[j], manyrecipients[i]) ) {
									isvaliduser = true ; // Matching username
									if ( Users->blocked[userid][j] || Users->blocked[j][userid] ) 
										SEPRINTF("Sending Message to Blocked Recipient %s.", manyrecipients[i]);
									else {
										// Send Message..
										msgsize = sizeof(struct msgbuf) + strlen(message) + 1;
										msgptr = (struct msgbuf *) malloc( msgsize );
										msgptr->mtype = clientmessagehandle(j);
										msgptr->bcast = false ;
										msgptr->notification = false ;
										strcpy(msgptr->sender,Users->username[userid]);
										strcpy(msgptr->message, message);
										if ( msgsnd(msqid, msgptr, msgsize, IPC_NOWAIT) == -1) 
											SEPRINTF("Sending Multicast Message to Recipient %s failed. Try again later.",manyrecipients[i]);
										else 
											SOPRINTF("Multicast Message Sent to %s.",manyrecipients[i]);
										free(msgptr);
									}
									break ; // Look at next Username
								}
							}
							if ( !isvaliduser ) SEPRINTF("Sending Message to Invalid Recipient %s.",manyrecipients[i]);
						}
					}
				}
			}
		}
		else if ( IS(cmd,"BCAST") || IS(cmd,"BROADCAST") ) {
			if ( !loggedin ) {
				SEPRINTF("Login is Required.");
				continue ;
			}

			if ( (message = getnextstr(&bufptr,"\n\r")) == NULL ) SEPRINTF("Message not entered.");
			else if ( strlen(message) > MAX_MSG_SIZE ) SEPRINTF("Message not sent. Message too long.");
			else {
				usercount = 0 ;
				// Send the message if not blocked ...
				for ( int i = 0; i < MAX_USERS; i ++) {
					if (  Users->isvalid[i] && i != userid ) {
						// Message can be sent only to Unblocked recipients..
						// Dont send to ur self ...
						if ( ! (Users->blocked[i][userid] || Users->blocked[userid][i]) ) {
							// Send the message ..
							msgsize = sizeof(struct msgbuf) + strlen(message) + 1;
							msgptr = (struct msgbuf *) malloc( msgsize );
							msgptr->mtype = clientmessagehandle(i);
							msgptr->bcast = true ;
							msgptr->notification = false ;
							strcpy(msgptr->sender,Users->username[userid]);
							strcpy(msgptr->message, message);
							if ( msgsnd(msqid, msgptr, msgsize, IPC_NOWAIT) == -1) 
								SEPRINTF("Broadcast to %s failed. Try again later.", Users->username[i]);
							else 
								SOPRINTF("Broadcast sent to %s.", Users->username[i]) , usercount ++ ;
							free(msgptr);
						}
						else {
							SEPRINTF("Broadcast Not Send to Blocked User %s.",Users->username[i]);
						}
					}
				}
				SOPRINTF("Broadcast complete. (Sent to %d users.)", usercount);
			}
		}
		else if ( IS(cmd,"BLOCK") ) {
			FOLLOWBLOCKHIDEBLOCK(blocked,true,"Blocking User","Trying to block invalid user");
		}
		else if ( IS(cmd,"UNBLOCK") ) {
			FOLLOWBLOCKHIDEBLOCK(blocked,false,"Stop Blocking User","Trying to unblock invalid user");
		}
		else if ( IS(cmd,"HIDE") ) {
			FOLLOWBLOCKHIDEBLOCK(hide,true,"Hiding from User","Trying to hide from invalid user");
		}
		else if ( IS(cmd,"UNHIDE") ) {
			FOLLOWBLOCKHIDEBLOCK(hide,false,"Stop Hiding from User","Trying to stop hide from invalid user");
		}
		else if ( IS(cmd,"FOLLOW") ) {
			FOLLOWBLOCKHIDEBLOCK(follow,true,"Following User","Trying to follow invalid user");
		}
		else if ( IS(cmd,"UNFOLLOW") ) {
			FOLLOWBLOCKHIDEBLOCK(follow,false,"Stopped Following User","Trying to stop following invalid user");
		}
		else if ( IS(cmd,"FINGER") ) {
			if ( !loggedin ) {
				SEPRINTF("Login is Required.");
				continue ;
			}
			if ( (recipient = getnextstr(&bufptr," \t\n\r")) == NULL ) SEPRINTF("Username not entered.");
			else {
				isvaliduser = false ;
				// Any one can send FINGER message ? ...
				// If not implement the restriction  ...
				for ( int i = 0; i < MAX_USERS; i ++) {
					if (  Users->isvalid[i] && IS(Users->username[i],recipient) ) {
						isvaliduser = true ;
						if ( Users->online[i] && !(Users->hide[i][userid] || Users->hide[userid][i]) )  SOPRINTF("USER: %s IPADDRESS: %s", Users->username[i],Users->ipaddress[i]);
						else SOPRINTF("USER: %s IPADDRESS: OFFLINE", Users->username[i]);
						break ;
					}
				}
				if ( !isvaliduser ) SEPRINTF("Invalid user '%s'.",recipient); 
			}
		}
		else if ( IS(cmd,"EXIT") ) {
			if ( loggedin ) {
				SOPRINTF("Logging out User '%s'.", Users->username[userid]);
				WAIT ;
				Users->online[userid] = false ;
				POST ;
				loggedin = false ;

				// Also kill the process waiting for messages for this client logging out 
				 killclientmessagehandler();
			}
			else { // If not logged in ...
				SEPRINTF("Login is Required.");
			}

		}
		else if (IS(cmd,"LEAVE")) {
			if ( loggedin ) {
				SOPRINTF("Deleting Account of User '%s'. GoodBye.", Users->username[userid]);
				WAIT ;
				Users->isvalid[userid] = false ;
				POST ;
				loggedin = false ;

				// Also kill the process waiting for messages for this client logging out 
				 killclientmessagehandler();

				// <------------ADDED LATER ...
				// If not already removed all messages just remove it ...
				msgptr = (struct msgbuf *) malloc(sizeof(struct msgbuf) + MAX_MSG_SIZE + 1);
				// if child process listen for any incoming messages and send them client..
				while ( msgrcv(msqid, msgptr, sizeof(struct msgbuf)+ MAX_MSG_SIZE+ 1, 
					clientmessagehandle(userid), MSG_NOERROR|IPC_NOWAIT) > 0 ) ;
				free(msgptr);
			}
			else { // If not logged in ...
				SEPRINTF("Login is Required.");
			}
		}
		else if ( IS(cmd,"HELP") ) {
			SOPRINTF("Create a new account: JOIN <username> <password>");
			SOPRINTF("Login from an existing account: USER <username> <password>");
			SOPRINTF("List all users: LIST");
			SOPRINTF("Send message to other user: SEND <username> <message>");
			SOPRINTF("Send message to multiple users: MCAST <usercount> <username> {<username>} <message>");
			SOPRINTF("Broadcast a message: BCAST <message>");
			SOPRINTF("Block an User: BLOCK <username>");
			SOPRINTF("Unblock an User: UNBLOCK <username>");
			SOPRINTF("Hide from User: HIDE <username>");
			SOPRINTF("Stop Hide from User: UNHIDE <username>");
			SOPRINTF("Follow User: FOLLOW <username>");
			SOPRINTF("Stop follow User: UNFOLLOW <username>");
			SOPRINTF("Location of user: FINGER <username>");
			//SOPRINTF("Last Seen of user: LASTSEEN <username>");
			// Also ADD last seen option .. ?
			SOPRINTF("Logout an account: EXIT");
			SOPRINTF("Delete an account: LEAVE");
			SOPRINTF("Help Menu: HELP");
		}
		else {
			SEPRINTF("Unknown Command.");
		}
		bzero(rbuf,BUFSIZ);
	}

	if ( loggedin ) {
		// If user got disconnected then log out the user ...
		Users->online[userid] = false ; 
	}
}
void createadmin() {
	// This is the admin process ..
	// Implement ADMIN Functionality Here...
	// This Exits when server exits ...

	char rbuf[BUFSIZ], * cmd , timestr[40], *bufptr, *recipient, *message , msgsize,*manyrecipients[MAX_USERS];
	bool isvaliduser, validrcount ;
	int usercount, onlinecount, offlinecount, count ;
	struct tm* tm_info ;
	struct msqid_ds msqbuf ;
    struct msgbuf * msgptr ;
	while ( fgets(rbuf,BUFSIZ,stdin) != NULL ) {
		bufptr = rbuf;
		cmd = getnextstr(&bufptr," \t\r\n");
		if ( cmd == NULL || strlen(cmd) == 0 ) continue ; 
		// Currently Implementing only LIST
		if ( IS(cmd,"LIST") ) {
			usercount = 0 ;	onlinecount = 0 ; offlinecount = 0 ; 
			for ( int i=0 ; i < MAX_USERS; i++) {
				if ( Users->isvalid[i] ){
					usercount++;
					printf("User:\033[031m%s\033[0m[userid: %d] Pass:\033[031m%s\033[0m Online:%c\n",Users->username[i],i,Users->password[i],(Users->online[i]?'Y':'N'));
					tm_info = localtime(&(Users->lastlogin[i])); strftime(timestr, 40, "%a %d-%b-%Y %H:%M:%S", tm_info);
					if ( Users->online[i] ) onlinecount ++ ;
					else offlinecount ++ ;
					printf("Last Login:%s IP Address:%s\n",timestr,Users->ipaddress[i]);
					#define CHECKNPRINT1(_COLOR_,_FIELD_,_MSG_) ({\
						count = 0 ; for ( int j = 0 ; j < MAX_USERS; j ++ )  if ( Users->_FIELD_[i][j] ) count ++ ;\
						if ( count != 0 ) {\
							printf(_COLOR_ _MSG_" ");\
							for ( int j = 0 ; j < MAX_USERS; j ++ )  if ( Users->_FIELD_[i][j] ) printf("%s[userid: %d] ",Users->username[j],j);\
							printf("\033[0m\n");\
						}\
					})
					#define CHECKNPRINT2(_COLOR_,_FIELD_,_MSG_) ({\
						count = 0 ; for ( int j = 0 ; j < MAX_USERS; j ++ )  if ( Users->isvalid[j] && Users->_FIELD_[j][i] ) count ++ ;\
						if ( count != 0 ) {\
							printf(_COLOR_ _MSG_" ");\
							for ( int j = 0 ; j < MAX_USERS; j ++ )  if ( Users->isvalid[j] && Users->_FIELD_[j][i] ) printf("%s[userid: %d] ",Users->username[j],j);\
							printf("\033[0m\n");\
						}\
					})
					CHECKNPRINT1("\033[33m",follow,"FOLLOWS ");
					CHECKNPRINT1("\033[33m",blocked,"BLOCKED ");
					CHECKNPRINT1("\033[33m",hide,"HIDE FROM");
					CHECKNPRINT2("\033[34m",follow,"FOLLOWED BY");
					CHECKNPRINT2("\033[34m",blocked,"BLOCKED BY");
					CHECKNPRINT2("\033[34m",hide,"HIDE BY ");

					printf("\n");
				}
			}
			printf("\033[32mTotal User count:%d [Online:%d Offline:%d]\033[0m\n", usercount,onlinecount,offlinecount);
		}
		else if ( IS(cmd,"RESOURCE") ) {
			if ( msgctl(msqid,IPC_STAT,&msqbuf)  == -1) {
				perror("Unable to get Information about Message Queue.");
			}
			else {
				printf("MESSAGE QUEUE: BYTES[%d/%d] MESSAGES[%d].\n",msqbuf.__msg_cbytes,msqbuf.msg_qbytes,msqbuf.msg_qnum);
			}
			printf("SEMAPHORE SEMVAL:%d SEMNCNT:%d\n", semctl(semid, 0, GETVAL, setval), semctl(semid, 0, GETNCNT, setval) );
		}
		else if ( IS(cmd,"SEND") ) {
			if ( (recipient = getnextstr(&bufptr," \t\n\r")) == NULL ) printf("Recipient not entered.\n");
			else if ( (message = getnextstr(&bufptr,"\n\r")) == NULL ) printf("Message not entered.\n");
			else if ( strlen(message) > MAX_MSG_SIZE ) printf("Message not sent. Message too long.\n");
			else {
				isvaliduser = false ;
				// Send the message ...
				for ( int i = 0; i < MAX_USERS; i ++) {
					if (  Users->isvalid[i] && IS(Users->username[i],recipient) ) {
						isvaliduser = true ;
						// Send the message ..
						msgsize = sizeof(struct msgbuf) + strlen(message) + 1;
						msgptr = (struct msgbuf *) malloc( msgsize );
						msgptr->mtype = clientmessagehandle(i);
						msgptr->bcast = false ;
						msgptr->notification = false ;
						strcpy(msgptr->sender,"\033[31mADMIN\033[0m");
						strcpy(msgptr->message, message);
						if ( msgsnd(msqid, msgptr, msgsize, IPC_NOWAIT) == -1) 
							printf("Sending Message to Recipient %s failed. Try again later.\n",recipient);
						else 
							printf("Message Sent to %s.\n",recipient);
						free(msgptr);
						
						break ;
					}
				}
				if ( !isvaliduser ) printf("Sending Message to Invalid Recipient %s.\n",recipient); 
			}
		}
		else if ( IS(cmd,"MCAST") || IS(cmd,"MULTICAST") ) {
			if ( (recipient = getnextstr(&bufptr," \t\n\r")) == NULL ) printf("Recipient count not entered.\n");
			else if ( (usercount = atoi(recipient)) <= 0 || usercount > MAX_USERS ) printf("Recipient count is not valid or exceed maximum.\n");
			else {
				validrcount = true ;
				// Now get the recipients..
				for ( int i = 0; i < usercount; i ++) {
					if ( (manyrecipients[i] = getnextstr(&bufptr," \t\n\r")) == NULL ) {
						validrcount= false ;
						printf("Recipients and count not matching.\n");
						break;
					}
				}
				if ( validrcount ) {
					// Send the message to all those recipients ..
					if ( (message = getnextstr(&bufptr,"\n\r")) == NULL ) printf("Message not entered for recipients.\n");
					else if ( strlen(message) > MAX_MSG_SIZE ) printf("Message not sent. Message too long.\n");
					else {
						for ( int i = 0 ; i < usercount; i++) {
							isvaliduser = false ;
							for ( int j = 0 ; j < MAX_USERS; j ++) {
								if ( Users->isvalid[j] && IS(Users->username[j], manyrecipients[i]) ) {
									isvaliduser = true ; // Matching username
									// Send Message..
									msgsize = sizeof(struct msgbuf) + strlen(message) + 1;
									msgptr = (struct msgbuf *) malloc( msgsize );
									msgptr->mtype = clientmessagehandle(j);
									msgptr->bcast = false ;
									msgptr->notification = false ;
									strcpy(msgptr->sender,"\033[31mADMIN\033[0m");
									strcpy(msgptr->message, message);
									if ( msgsnd(msqid, msgptr, msgsize, IPC_NOWAIT) == -1) 
										printf("Sending Multicast Message to Recipient %s failed. Try again later.\n",manyrecipients[i]);
									else 
										printf("Multicast Message Sent to %s.\n",manyrecipients[i]);
									free(msgptr);
									
									break ; // Look at next Username
								}
							}
							if ( !isvaliduser ) printf("Sending Message to Invalid Recipient %s.",manyrecipients[i]);
						}
					}
				}
			}
		}
		else if ( IS(cmd,"BCAST") ) {
			if ( (message = getnextstr(&bufptr,"\n\r")) == NULL ) printf("Message not entered.\n");
			else if ( strlen(message) > MAX_MSG_SIZE ) printf("Message not sent. Message too long.\n");
			else {
				usercount = 0 ;
				for ( int i = 0; i < MAX_USERS; i ++) {
					if (  Users->isvalid[i] ) {
						msgsize = sizeof(struct msgbuf) + strlen(message) + 1;
						msgptr = (struct msgbuf *) malloc( msgsize );
						msgptr->mtype = clientmessagehandle(i);
						msgptr->bcast = true ;
						msgptr->notification = false ;
						strcpy(msgptr->sender,"\033[31mADMIN\033[0m");
						strcpy(msgptr->message, message);
						if ( msgsnd(msqid, msgptr, msgsize, IPC_NOWAIT) == -1) 
							printf("Sending Message to Recipient %s failed. Try again later.\n",Users->username[i]);
						else 
							printf("Message Sent to %s.\n",Users->username[i]), usercount ++;
						free(msgptr);
					}
				}
				printf("Broadcast complete. (Sent to %d users.)\n", usercount);
			}
		} 
		else {
			printf("\033[35mUnknown Command %s.\033[0m\n", cmd);
		}
		bzero(rbuf,BUFSIZ);
	}
}
int main() {
	// All global variables
	int fd , cfd , addrlen=sizeof(struct sockaddr_in), option=1, pid, status;
	struct sockaddr_in serveraddr, clientaddr ;

	serveraddr.sin_family = AF_INET ; serveraddr.sin_port = htons(PORT);
	inet_pton(AF_INET,IPADDRESS,&serveraddr.sin_addr); 
	
	allocateresources();
	signal(SIGINT, signalhandler1);	signal(SIGQUIT, signalhandler1);
	try ( fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP) );
	try ( setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) );
	try ( bind(fd,(struct sockaddr *)&serveraddr,addrlen) );
	try ( listen(fd, LISTEN_BACKLOG) );
	// Now create a process for ADMIN at the server ...
	try ( pid = fork() ) ;
	if ( CHILD(pid) ) {
		// Create an admin process ...
		try ( close(fd) ); // Don't Listen on socket ..
		DEBUG("Admin Process [PID: %d] created.", getpid());
		signal(SIGINT, signalhandler4);	signal(SIGQUIT, signalhandler4);
		createadmin();
		DEBUG("Admin Process [PID: %d] exiting.", getpid());
		exit(EXIT_SUCCESS);
	}
	while (1) {
		try ( cfd = accept(fd, (struct sockaddr *)&clientaddr, &addrlen) ) ;
		try ( pid = fork() ); 
		if ( CHILD(pid) ) {
			DEBUG("Client [IP: %s, PORT: %d] [PID: %d] is created.",inet_ntoa(clientaddr.sin_addr),htons(clientaddr.sin_port),getpid());

			try ( close(fd) );
			signal(SIGINT, signalhandler2);	signal(SIGQUIT, signalhandler2);
			handleclient(cfd);
			
			DEBUG("Client [IP: %s, PORT: %d] [PID: %d] is exiting.",inet_ntoa(clientaddr.sin_addr),htons(clientaddr.sin_port),getpid());
			while ( ( pid = waitpid(0, &status, WNOHANG) ) > 0 ) DEBUG("[CLEAR ZOMBIE] Process [PID: %d] exited with status [STATUS: %d].", pid, WEXITSTATUS(status) ); // clear all zombies ... (Here the message handler if it exists)
			exit(EXIT_SUCCESS);
		}
		try ( close(cfd) );
		while ( ( pid = waitpid(0, &status, WNOHANG) ) > 0 ) DEBUG("[CLEAR ZOMBIE] Process [PID: %d] exited with status [STATUS: %d].", pid, WEXITSTATUS(status) ); // clear all zombies ...
	}

	try ( close(fd) );
	return 0;
}