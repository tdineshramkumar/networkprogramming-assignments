/*
	T Dinesh Ram Kumar
	2014A3A70302P
*/

#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pwd.h>
#ifdef READLINE
	#include <readline/readline.h>
	#include <readline/history.h>
#endif


#define FG_BLACK "\033[30m"
#define FG_RED "\033[31m"
#define FG_GREEN "\033[32m"
#define FG_YELLOW "\033[33m"
#define FG_BLUE "\033[34m"
#define FG_MAGENTA "\033[35m"
#define FG_CYAN "\033[36m"
#define FG_WHITE "\033[37m"
#define FG_DEFAULT "\033[39m"

#define BG_BLACK "\033[40m"
#define BG_RED "\033[41m"
#define BG_GREEN "\033[42m"
#define BG_YELLOW "\033[43m"
#define BG_BLUE "\033[44m"
#define BG_MAGENTA "\033[45m"
#define BG_CYAN "\033[46m"
#define BG_WHITE "\033[47m"
#define BG_DEFAULT "\033[49m"

#define BOLD "\033[1m"
#define UNDERLINE "\033[4m"
#define ITALIC "\033[3m"
#define NOBOLD "\033[22m"

#define RESET "\033[0m"
#define FORMAT " ><,|\t\n\r"
#define SCAN(command) ({\
 	fgets(command,BUFSIZ,stdin); int len =strlen(command);\
 	if (command[len-1] == '\n') command[len-1]='\0';\
 })
#define IS(str1,str2) (strcmp(str1,str2) == 0)
#define CLEAR ({ printf("\033[H\033[J");})
#define HISTORY_MAX 20

struct _commandtree {
	char **args ; // this is the command 
	struct _commandtree ** commands ; // this is one's to pipe the result to ..
	char * infile ; // this is the input file .. (if not NULL then input taken from this file)
	char * outfile ; // this is the output file .. (this is used to output the file and breaks the chain of commands)
	int oflags ; // this is used while opening outfile 
} ;

typedef struct _commandtree *COMMANDTREE ;
char inputcommand[BUFSIZ]; // This contains the command currently read...
char inputcommandcopy[BUFSIZ]; // This contains a copy of the command ...
COMMANDTREE cmdtree = NULL ; // This contains the root of the command tree ...

char eusername[LOGIN_NAME_MAX];// This contains the effective user name
char cwd[PATH_MAX];// This contains the current path
char hostname[LOGIN_NAME_MAX]; // This contains the hostname of the machine
char prevpath[PATH_MAX]; // This is used by change directory ..

char history[HISTORY_MAX][BUFSIZ]; // This stores the command history
int currentpos = 0 ;  // This stores the current position in the history 
void showpromptandreadinput() {
	getcwd(cwd,PATH_MAX);
	cuserid(eusername);
	gethostname(hostname,LOGIN_NAME_MAX);
	#ifdef READLINE
		char prompt[BUFSIZ];
		sprintf(prompt, BG_GREEN FG_RED BOLD "%s" NOBOLD FG_BLACK "(%s)" BG_WHITE FG_BLUE " %s" BG_DEFAULT FG_GREEN " >> " RESET , eusername, hostname, cwd);
		char *tmp = readline(prompt);
		add_history(tmp);
		strcpy(inputcommand,tmp);
		free(tmp);
	#else
		printf( BG_GREEN FG_RED BOLD "%s" NOBOLD FG_BLACK "(%s)" BG_WHITE FG_BLUE " %s" BG_DEFAULT FG_GREEN " >> " RESET , eusername, hostname, cwd);
		SCAN(inputcommand); // Read the input 
	#endif
	strcpy(inputcommandcopy, inputcommand); // Make a copy ..
}
void updatehistory(char *command) {
	if ( currentpos < HISTORY_MAX ) { // History table not full  append the entry ...
		strcpy(history[currentpos], command);
		currentpos ++;
	} 
	else { // If history table is full .. add to end ..
		// if CURRENTPOS == HISTORY_MAX .. then shift one position ahead ..
		for ( int i = 1 ; i < HISTORY_MAX ; i ++) strcpy(history[i-1],history[i]);
		strcpy(history[HISTORY_MAX-1],command);
	}
}
bool isShellCommand() {
	char *cmdptr = inputcommandcopy;
	int pos, historyoption ; struct passwd *pw;
	// Skip Initial Space if any ..
	pos = strspn(cmdptr,FORMAT);
	cmdptr += pos ;
	// Get the command ..
	pos = strcspn(cmdptr,FORMAT);
	if ( pos == 0 ) return true ; // If No Command ... Read next command ..
	cmdptr[pos] = '\0'; // To separate out the command ..
	if ( IS(cmdptr,"clear") ) {
		CLEAR ;
		return true;
	}
	else if ( IS(cmdptr,"cd") || IS(cmdptr,"dir") ) {
		cmdptr[pos] = ' '; cmdptr += pos ; // After the command 
		// Skip Spaces..
		pos = strspn( cmdptr,FORMAT );
		cmdptr = cmdptr + pos ;
		// Now it points to directory ...
		if ( strlen(cmdptr) == 0 || IS(cmdptr,"~") ) {
			// If No INPUT is specified or ~ .. go to home ..
			getcwd(prevpath,PATH_MAX); // Save the previous directory 
			pw = getpwuid(getuid());
			chdir(pw->pw_dir); 
		}
		else if ( IS(cmdptr,"-")) {
			// Go to previous working directory ..
			getcwd(inputcommandcopy,PATH_MAX); // copy current working directory
			chdir(prevpath); // Change the directory
			strcpy(prevpath,inputcommandcopy); // Copy the previous directory
		}
		else { 
			getcwd(prevpath,PATH_MAX); // Path is updated irrespective of whether chdir is successful or not.
			if ( chdir(cmdptr) == -1 ) 
				perror("");
		}
		updatehistory(inputcommand);
		return true ;
	}
	else if ( IS(cmdptr,"exit") ){
		exit(EXIT_SUCCESS);
	}
	else if ( IS(cmdptr,"history") ){
		cmdptr[pos] = ' '; cmdptr += pos ; // After the command 
		// Skip Spaces..
		pos = strspn( cmdptr,FORMAT );
		cmdptr = cmdptr + pos ;
		historyoption = atoi(cmdptr);
		// Now check it contains a number of not ..
		if ( historyoption == 0 ) // No Option is entered ,,
		{
			for ( int i= 0; i < currentpos; i++) {
				printf("%3d. %s\n",i+1,history[i]);
			}
			return true;
		}
		else if ( historyoption <= currentpos ) {
			strcpy(inputcommand,history[historyoption-1]);
			strcpy(inputcommandcopy,history[historyoption-1]);
			updatehistory(inputcommand); // copy the new command to history ..
			return false ; // So that it is executed ...
		}
		else {
			printf("No Command to execute.\n");
			return true ;
		}
	}
	updatehistory(inputcommand);
	return false ;
}
// Once you get the command part 
// Next need to know the operator
bool getoperator (char *symbols, char *operator) {
	bool updated = true ;
	int pos ;
	while( updated ) {
		updated = false ;
		pos = strspn(symbols, " \t\n"); if ( pos != 0)  { updated = true ; symbols = symbols + pos ; continue ;} 
		pos = strspn(symbols,"|"); if (pos != 0) { updated= true; strncpy(operator,symbols,pos); operator[pos]='\0'; symbols =symbols + pos; return true; }
		pos = strspn(symbols,","); if (pos != 0) { updated= true; strncpy(operator,symbols,pos); operator[pos]='\0'; symbols =symbols + pos; return true; }
		pos = strspn(symbols,">"); if (pos != 0) { updated= true; strncpy(operator,symbols,pos); operator[pos]='\0'; symbols =symbols + pos; return true; }
		pos = strspn(symbols,"<"); if (pos != 0) { updated= true; strncpy(operator,symbols,pos); operator[pos]='\0'; symbols =symbols + pos; return true; }
	}
	return false ;
}
bool parseinput( char ** cmdptr , COMMANDTREE *node) {
	int pos, numpipes=0, numargs=0;
	char *tmp , symbols[BUFSIZ], operator[BUFSIZ];
	bool expected = false, infile= false , write = false ; // If any file name expected ..

	if ( **cmdptr == '\0' ) return false ; // if end of string ...
	*node = (COMMANDTREE) malloc(sizeof(struct _commandtree)) ; // allocate a tree node
	(*node)->infile = NULL ;
	(*node)->outfile = NULL ;
	(*node)->commands = NULL ;
	(*node)->args = NULL ;
	// Remove the starting space.
	pos = strspn(*cmdptr, FORMAT);
	*cmdptr = *cmdptr + pos ;
	tmp = *cmdptr ; // First look onces and determine if right format and identify number of pipes ...
	while ( true ){
		pos = strcspn( tmp, FORMAT);
		if ( pos == 0 ) break ; // if no more command break and return
		if ( expected ) expected = false ; // got the file argument ...
		else numargs ++ ; // if not file argument increment count ...
		tmp = tmp + pos ; // also move to check for more
		pos = strspn( tmp,FORMAT);  // Now obtain the symbols 
		strncpy(symbols, tmp, pos); symbols[pos] = '\0'; // copy it ...
		if ( getoperator(symbols,operator) ){
			if ( strcmp(operator, ",") == 0 ) {
				if ( expected ) return false ;
				numpipes = 0 ; // no pipe
				break ;
			}
			else if ( strcmp(operator, ">") == 0 ||  strcmp(operator, ">>") == 0 ||  strcmp(operator, "<") == 0 ) 
				expected = true ;
			else { 
				if ( expected ) return false ;
				pos = strspn(operator,"|");
				if ( pos == 0 ) return false ;
				numpipes = pos; 
				break ;
			}
		}
		tmp = tmp + pos ;
	}
	// Now build the tree ..
	(*node)->args = (char **) malloc(sizeof(char *)*(numargs+1));
	for ( int i = 0; i < numargs ; i ++) (*node)->args[i] = NULL ;
	(*node)->args[numargs] = NULL ; // NULL terminalated array of arguments ..
	(*node)->commands = (COMMANDTREE *) malloc(sizeof(COMMANDTREE)*(numpipes+1));
	for ( int i = 0 ; i < numpipes ; i ++ ) (*node)->commands[i] = NULL ;
	(*node)->commands[numpipes] = NULL ;
	numargs = 0 ;

	while ( true ) {
		pos = strcspn( *cmdptr ,FORMAT);
		if ( pos == 0 ) break ; // if no more command break and return
		strncpy(inputcommandcopy, *cmdptr,pos); inputcommandcopy[pos] = '\0'; //printf(" %s ",inputcommandcopy);
		if ( expected ) { 
			if ( infile ){
				(*node)->infile = (char *) malloc(sizeof(char) * (strlen(inputcommandcopy) + 2)); 
				strcpy((*node)->infile,inputcommandcopy); 
			}
			else {
				// outfile
				(*node)->outfile = (char *) malloc(sizeof(char) * (strlen(inputcommandcopy) + 2)); 
				strcpy((*node)->outfile,inputcommandcopy); 
				if (write) (*node)->oflags = O_CREAT|O_WRONLY|O_TRUNC ;
				else (*node)->oflags = O_CREAT|O_APPEND|O_WRONLY ;
			}
			expected = false ;
		}
		else {  
			(*node)->args[numargs] = (char *) malloc(sizeof(char) * (strlen(inputcommandcopy) + 2)); 
			strcpy((*node)->args[numargs],inputcommandcopy); 
			numargs ++ ;
		}
		// shift the comman
		*cmdptr = *cmdptr + pos ;
		// get length of operator
		pos = strspn( *cmdptr,FORMAT);
		// copy that into symbols
		strncpy(symbols,*cmdptr,pos); symbols[pos] = '\0';
		if ( getoperator(symbols,operator) ){
			if ( strcmp(operator, ",") == 0 ) {
				if ( expected ) return false ;
				*cmdptr = *cmdptr + pos ;
				return true;
			}
			else if ( strcmp(operator, ">") == 0 ) 
				expected = true , infile = false , write = true;
			else if (  strcmp(operator, ">>") == 0 )
				expected = true  , infile = false , write = false;
			else if ( strcmp(operator, "<") == 0 ) 
				expected = true , infile = true ;
			else {
				// Else pipe ...
				if ( expected ) return false ;
				*cmdptr = *cmdptr + pos ;
				for (int i=0 ;i < numpipes; i++) {
					if ( ! parseinput(cmdptr,&((*node)->commands[i])) ) return false;
				}
				return true ;
			}
		}
		*cmdptr = *cmdptr + pos ;
	}
	if ( expected ) return false ;
	return true ; // ?
}

void printtree(COMMANDTREE node) {
	int count  = 0;
	if ( node == NULL ) return ;
	for ( char **cmdstr = node->args; *cmdstr != NULL; cmdstr++)
		printf(" %s",*cmdstr );
	if ( node->infile != NULL) printf(" < %s", node->infile);
	if ( node->outfile != NULL) 
		if ( node->oflags & O_APPEND ) printf(">> %s", node->outfile);
		else printf("> %s", node->outfile);
	printf(" ");
	for ( COMMANDTREE *cmdtmp = node->commands ; *cmdtmp != NULL ; cmdtmp++ ) count ++ ;
	for ( int i = 0 ; i < count ; i ++)  printf("|");
	for (int i = 0 ; i < count ; i ++ ) {
		printtree(node->commands[i]);
		if ( i < count-1 )
			printf(",");
	}
}

void removecommandtree( COMMANDTREE node ) {
	if ( node == NULL ) return ;
	for ( char **args = node->args ; *args != NULL ; args ++ ) free(*args);
	free(node->args); // Free the double pointer
	for ( COMMANDTREE *tmp = node->commands ; *tmp != NULL ; tmp ++ ) removecommandtree(*tmp);
	free(node->commands); // Free the double pointer
	free(node->infile);
	free(node->outfile);
	free(node);
}
#define EXIT(msg) ({ perror(msg); exit(EXIT_FAILURE); })
#define TRY(_EXPR_,_EXIT_,_MSG_) ({ if ( ((int)(_EXPR_)) == -1) { perror(_MSG_); if (_EXIT_) exit(EXIT_FAILURE);} })
#define try(_EXPR_) TRY(_EXPR_,1,"") 
void executecommandnode(COMMANDTREE node) ;
// This function duplicates the output from one pipe to others..
int pipeout( COMMANDTREE * commands ) {
	// if many or single to pipe out to ...
	int pipecount = 0, status ;
	int ** allpipes ; int pipefd[2];
	pid_t pid ; int readbytes ; 	char buffer[BUFSIZ];
	for ( COMMANDTREE * tmp = commands ; *tmp != NULL; tmp++ ) pipecount ++ ; // increment the pipe out count
	// Now create the pipe ...
	allpipes = (int **) malloc(sizeof(int *)*pipecount);
	for (int i= 0 ; i < pipecount; i ++) allpipes[i] = (int *)malloc(sizeof(int)*2) ; // Now all memory allocated
	// create the pipes
	for (int i= 0; i < pipecount; i++) 	try ( pipe(allpipes[i]) ) ;

	// Once all pipes created successfully...
	// create all the children which will execute their commands ...
	for (int i = 0; i < pipecount; i++) {
		// create a new process ..
		try ( pid = fork() );
		// Now if fork successful ..
		if ( pid == 0) {
			// child process ...
			// once it closes unneccesary pipes 
			// it executes the command
			for (int p= 0 ; p < pipecount; p ++) {
				close(allpipes[p][1]); // close all write ends
				if ( p != i )
					close (allpipes[p][0]); // close all read ends except its own
			}
			// Once unneccesary pipes closed ..
			// It executes it changes its standard input
			// and executes its command
			dup2(allpipes[i][0],0); // duplicate to standard input ...
			executecommandnode(commands[i]); // execute that command ..
			// it automatically exits out .. 
		}
		// If parent 
		// close the read end of the pipe ...
		close(allpipes[i][0]);
	}
	// create a master pipe ...
	// Only in the master 
	try ( pipe(pipefd) == -1) ;
	// Also fork ...
	try ( pid = fork() );
	if ( pid == 0 ) {
		// new process created will write to all the pipes ..
		close(pipefd[1]); // closes the write end of the master process ..
		dup2(pipefd[0],0); // duplicate the read to stdin
		// Now its only job to read from pipe and write to remaining pipes
		while ( (readbytes= read(0, buffer, BUFSIZ) ) > 0) {
			for (int p = 0; p < pipecount; p++) {
				write(allpipes[p][1], buffer, readbytes); // write to all the remaining pipes ..
			}
		}
		while ( (pid  = waitpid(-1,&status,0)) > 0 );
		// When no more to write ... exit ...
		exit(EXIT_SUCCESS);
	}
	// The original process will continue ...
	// current process also closes all its open pipes (writing end..)
	for (int i = 0; i < pipecount; i ++)
		close(allpipes[i][1]);
	for ( int i=0 ; i < pipecount; i++ ) free(allpipes[i]); // Free all the pipes ..
	free(allpipes); // Free all the pipes ..
	close(pipefd[0]); // it will also close the read end of the pipe ..
	return pipefd[1]; // return an write end to write to ...
}

// Note: Execute command does not return ..
// It exits with success or failure ..
void executecommandnode(COMMANDTREE node) {
	int fd ;
	// Set the input of the command ...
	if ( node->infile == NULL ) {
		// if no infile is mentioned 
		// assumes input from stdin ...
	}
	else {
		// if infile is mentioned 
		// close the stdin and use this file 
		// Open in READONLY
		try ( fd = open(node->infile,O_RDONLY) ) ;
		dup2(fd,0); // duplicate the current fd to stdin and close the stdin ...
	}
	// Set the output of the command ...
	if ( node->outfile == NULL ) {
		// if no out file is mentioned 
		// assume writing to stdout or next command whichever it is
		if ( *(node->commands) != NULL ) {
			// if there are pipes following it ..
			try ( fd = pipeout(node->commands) ) ; // Create a pipe to write to which will copy to remaining commands ...
			dup2(fd,1); // duplicate the current fd to stdout and close the stdout ..
		}
		// Else use the stdout port ...
	}
	else {
		// if file name exists
		try ( fd = open(node->outfile, node->oflags, 0777) );
		dup2(fd,1); // duplicate the current fd to stdout and close the stdout ..
		// Now output is set ..
	}
	// Once both input and output is set execute the command ...

	// Also check if command to our prompt ...
	// <----------------------- COMPLETE IT LATER 
	execvp(node->args[0],node->args);
	// If unable to execute given command
	char errorstr[BUFSIZ];
	sprintf(errorstr,"Unable to Execute given command '%s'", node->args[0]);
	perror(errorstr);
	// Exit out with error
	exit(EXIT_FAILURE);
}
// // this function executes the command
// void executecommand() {
// 	pid_t pid ;
// 	int pipefd[2], readbytes , status ;
// 	char buffer[BUFSIZ];
// 	// Fork to create a child which will wait for commands to finish execution...
// 	if ( (pid = fork()) == -1 ) perror("Fork Failed." );
// 	else if ( pid == 0) {
// 		// create a pipe .. this is use to redirect all the stdout of all commands ...
// 		try ( pipe(pipefd) );
// 		// Fork and create a child which will execute the commands in the command tree ...
// 		try ( pid = fork() );
// 		if ( pid == 0 ) {
// 			// this will execute the command
// 			close(pipefd[0]); // close the read end of the pipe
// 			dup2(pipefd[1],1); // copy to write ...
// 			close(pipefd[1]); // close the original
// 			executecommandnode(cmdtree); // Now execute the command
// 			exit(EXIT_FAILURE); // It won;t come here ...
// 		} 
// 		else {
// 			// This will wait for all processes to complete
// 			close(pipefd[1]); // close the write end of the pipe 
// 			// Now its only job to read from pipe and write to stdout..
// 			while ( (readbytes= read(pipefd[0], buffer, BUFSIZ) ) > 0) {
// 				printf(FG_BLUE); fwrite(buffer, sizeof(char),readbytes,stdout); printf(RESET);
// 				fflush(stdout);
// 			} // This is used to synchronize the output
// 			printf("\n");
// 			pid = wait(&status);
// 			exit(status); // Now exit with the status obtained from the command execution ...
// 		}
// 	}
// 	else {// The current process waits for the completion of the process waiting for completion of all commands..
// 		pid = wait(&status);
// 		printf("%d exited with status %d\n",pid, WEXITSTATUS(status));
// 	}
// } 
char masterinput[BUFSIZ]; // this contains the master input command
COMMANDTREE *commandtrees = NULL ; // This contains a list of null terminated command tree pointers
void removemastercommandtree() {
	if ( commandtrees == NULL ) return ;
	for ( COMMANDTREE *tmp = commandtrees; *tmp != NULL ; tmp++) 
		removecommandtree(*tmp); // Delete each of the command trees...
	free(commandtrees);
}
bool masterparseinput() {
	char *masterptr = masterinput , *cmdptr;
	int pos, count = 0 ;
	// Ignore initial characters ..
	pos = strspn(masterptr, "& \t\n\r");
	masterptr = masterptr + pos ;
	// First get a count ...
	while ( true ) {
		pos = strcspn(masterptr, "&");
		if ( pos == 0 ) break ;
		masterptr[pos] = '\0';
		// printf("%s\n", masterptr);
		masterptr[pos] = '&';
		count ++ ;
		masterptr += pos ;
		pos = strspn(masterptr,"& \t\n\r");
		masterptr += pos ; // ignore all the remaining convergent pipes ..
	}
	// Now get all commands between masterptr
	// printf("COUNT:%d\n",count);
	commandtrees = (COMMANDTREE *) malloc(sizeof(COMMANDTREE) * (count + 1));
	for ( int i = 0; i < count ; i ++) commandtrees[i] = NULL ;
	commandtrees[count] = NULL;
	count = 0 ; // reset count ... Now build the list
	masterptr = masterinput ;
	while ( true ) {
		pos = strcspn(masterptr, "&");
		if ( pos == 0 ) break ;
		masterptr[pos] = '\0';
		bzero(inputcommand,BUFSIZ);
		bzero(inputcommandcopy,BUFSIZ);
		strcpy(inputcommand,masterptr);
		strcpy(inputcommandcopy,masterptr);
		cmdptr = inputcommand ;
		if ( !parseinput(&cmdptr,&(commandtrees[count])) ) {
			printf( FG_RED "ERROR: Invalid Section: " BG_WHITE FG_BLACK " %s " RESET "\n",masterptr);
			return false ;
		}
		count ++ ;
		masterptr[pos] = '&';
		masterptr += pos ;
		pos = strspn(masterptr,"& \t\n\r");
		masterptr += pos ; // ignore all the remaining convergent pipes ..
	}
	return true ;
}

void printmastertree() {
	int count = 0 ;
	if ( commandtrees == NULL ) return ;
	for ( COMMANDTREE *tmp = commandtrees; *tmp != NULL ; tmp++) 
		count ++ ;
	for (int i = 0 ; i < count ; i ++ ) {
		printf(BG_WHITE FG_MAGENTA); printtree(commandtrees[i]); printf(RESET); fflush(stdout);
		if ( i != count - 1)
			printf(" & ");
	} 
	printf("\n");
}
void executemastercommandnode(COMMANDTREE *commands){
	if ( commands == NULL && *commands == NULL ) return ;
	if ( *(commands+1) == NULL ) {
		// No need for pipe 
		executecommandnode(*commands);
	}
	else {
		// Create a Pipe ...
		int pipefd[2];
		try ( pipe(pipefd) ) ;
		int pid ;
		try ( pid = fork() ) ;
		if ( pid == 0 ){
			// CHILD ...
			close(pipefd[1]); // CLose Write End of pipe ..
			dup2(pipefd[0],0); // Duplicate Pipe Read end to stdin..
			close(pipefd[0]); // Close the duplicate copy ..
			// Write END is STDOUT ... or the MASTER PIPE ...
			executemastercommandnode(commands+1);
		}
		else {
			close(pipefd[0]); // CLose Read End of the pipe ..
			dup2(pipefd[1],1); // Duplicate Pipe Write end to stdout ..
			close(pipefd[1]); // Close the duplicate copy ..
			// READ End is the STDIN or elsewhere..
			// Write to PIPE 
			executecommandnode(*commands);
		}
	}
}
void executemastercommand() {
	if ( commandtrees == NULL ) return ;
	pid_t pid ;
	int pipefd[2], readbytes , status ;
	char buffer[BUFSIZ];
	// Fork to create a child which will wait for commands to finish execution...
	if ( (pid = fork()) == -1 ) perror("Fork Failed." );
	else if ( pid == 0) {
		// create a pipe .. this is use to redirect all the stdout of all commands ...
		try ( pipe(pipefd) );
		// Fork and create a child which will execute the commands in the command tree ...
		try ( pid = fork() );
		if ( pid == 0 ) {
			// this will execute the command
			close(pipefd[0]); // close the read end of the pipe
			dup2(pipefd[1],1); // copy to write ...
			close(pipefd[1]); // close the original
			executemastercommandnode(commandtrees);
			exit(EXIT_FAILURE); // It won;t come here ...
		} 
		else {
			// This will wait for all processes to complete
			close(pipefd[1]); // close the write end of the pipe 
			// Now its only job to read from pipe and write to stdout..
			while ( (readbytes= read(pipefd[0], buffer, BUFSIZ) ) > 0) {
				printf(FG_BLUE); fwrite(buffer, sizeof(char),readbytes,stdout); printf(RESET);
				fflush(stdout);
			} // This is used to synchronize the output
			printf("\n");
			pid = wait(&status);
			exit(status); // Now exit with the status obtained from the command execution ...
		}
	}
	else {// The current process waits for the completion of the process waiting for completion of all commands..
		pid = wait(&status);
		printf("%d exited with status %d\n",pid, WEXITSTATUS(status));
	} 
}


// This enables & command ....
int main() {
	char *cmdptr; // This is a pointer to command ...
	getcwd(prevpath,PATH_MAX);
	while (1) {
		// Always read from input
		showpromptandreadinput();
		if ( !isShellCommand() ) {
			bzero(masterinput,BUFSIZ);
			strcpy(masterinput,inputcommand) ;
			if ( masterparseinput() ) { 
				printmastertree(); 
				executemastercommand();
			}
			removemastercommandtree();
		}
		bzero(inputcommand,BUFSIZ);
		bzero(inputcommandcopy,BUFSIZ);
	}
	return 0;
}