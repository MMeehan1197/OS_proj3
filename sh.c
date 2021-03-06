/*
 * Sample Project 2
 *
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <pwd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include "sh.h"
#include <glob.h>
#include <sys/time.h>
#include <utmpx.h>
#include <time.h>

pid_t cpid;
pthread_mutex_t mutex_Users;
struct user* users = NULL;
struct utmpx *up;

/* function that starts the shell
 * argc: argument count
 * argv: the array of arguments given
 * envp: the array of ponters to environment variables
 */
int sh( int argc, char **argv, char **envp)
{
  //Setting up variables needed for the shell to function
  char * prompt = calloc(PROMPTMAX, sizeof(char));
  char * prefix = "";
  char *commandline = calloc(MAX_CANON, sizeof(char));
  char *command, *arg, *commandpath, *p, *pwd, *owd;
  char **args = calloc(MAXARGS, sizeof(char*));
  int uid, i, status, argsct, go = 1;
  struct passwd *password_entry;
  char *homedir;
  pthread_t* watchingUser = NULL;

  struct pathelement *pathlist;
  int count;
  char *token;
  struct prev_cmd* head = NULL;
  struct alias* alist = NULL;
  struct mail* mailingList = NULL;
  int space;
  int valid;
  int background;

  uid = getuid();
  password_entry = getpwuid(uid);               /* get passwd info */
  homedir = password_entry->pw_dir;		/* Home directory to start
						  out with*/
 
  // store the current working directory  
  if ( (pwd = getcwd(NULL, PATH_MAX+1)) == NULL )
  {
	if(watchingUser != NULL)
	{
		free(watchingUser);
		pthread_mutex_destroy(&mutex_Users);
	}
	freeUsers();
    perror("getcwd");
    exit(2);
  }
  owd = calloc(strlen(pwd) + 1, sizeof(char));
  memcpy(owd, pwd, strlen(pwd));

  // Set up the initial prompt
  prompt[0] = ' '; prompt[1] = '['; prompt[2] = '\0';

  strcat(prompt, pwd);
  prompt[strlen(prompt)+3] = '\0';
  prompt[strlen(prompt)] = ']';
  prompt[strlen(prompt)] = '>';
  prompt[strlen(prompt)] = ' ';

  /* Put PATH into a linked list */
  pathlist = get_path();

  while ( go )
  {
    /* print your prompt */
    valid = 1;
    printf("%s%s", prefix, prompt); 
    
    // Read in command line
	if(fgets(commandline, MAX_CANON, stdin) == NULL){
		commandline[0] = '\n';
		commandline[1] = '\0';
		valid = 0;
		printf("\n");
	}
	int space = 1;
	
	// Remove newline character from end of input
	if (strlen(commandline) > 1){
		commandline[strlen(commandline) - 1] = '\0';
	}
	else {
		valid = 0;
	}	
	
	// Check command for special cases
	for(i = 0; i < strlen(commandline); i++){
		if(commandline[i] != ' '){
			space = 0;
		}
	}
	if (space){
		commandline[strlen(commandline)-1] = '\n';
		valid = 0;
	}	
	
	// Parse the command line to separate the arguments
	count = 1;
	args[0] = strtok(commandline, " ");
	while((arg=strtok(NULL, " ")) != NULL){
		args[count] = arg;
		count++;
	}
   	args[count] = NULL;
	argsct = count;

	//Checks for ampersand
	if(argsct > 1){
		background = (strcmp(args[argsct-1], "&") == 0);//Checks for ampersand as last char
	}
	else{ background = 0; }

	if (background)
                args[argsct-1] = NULL; //Gets rid of ampersand


	// Check if command is an alias
	struct alias* curr = alist;
	int found = 0;
	int done = 0;
	if(argsct == 1){
		while(curr != NULL && !done){
			found = 0;
			if(strcmp(curr->name, args[0]) == 0){
				strcpy(commandline, curr->cmd);
				found = 1;
				count = 1;
				args[0] = strtok(commandline, " ");
				while((arg=strtok(NULL, " ")) != NULL){
					args[count] = arg;
					count++;
				}
   				args[count] = NULL;
				argsct = count;
				if(curr->used == 1){
					args[0] = "\n\0";
					printf("Alias Loop\n");
					done = 1;
				}
				curr->used = 1;
			}
			curr = curr->next;
			if(found) {
				curr = alist;
			}
		}
	}
	
	// Reset (used) aspect of each alias struct
	curr = alist;
	while(curr!=NULL){
		curr->used = 0;
		curr=curr->next;
	}
	
        // Check for each built in command
	command = args[0];
	if(strcmp(command, "exit") == 0){
		// Exit the shell
		printf("Executing built-in exit\n");
		free(watchingUser);
		exit(0);
	}
	else if(strcmp(command, "which") == 0){
		// Finds first alias or file in path directory that 
		// matches the command
		printf("Executing built-in which\n");
		if(argsct == 1){
			fprintf(stderr, "which: Too few arguments.\n");
		}
		else{
			// Checks for wildcard arguments
			glob_t globber;
			int i;
			for(i = 1; i < argsct; i++){
				int globreturn = glob(args[i], 0, NULL, &globber);
				if(globreturn == GLOB_NOSPACE){
					printf("glob: Runnning out of memory.\n");
				}
				else if(globreturn == GLOB_ABORTED){
					printf("glob: Read error.\n");
				}
				else{
				
					if(globber.gl_pathv != NULL){
						which(globber.gl_pathv[0], pathlist, alist);
					}
					else {
						which(args[i], pathlist, alist);
					}
				}
			}
		}		
	}
	else if(strcmp(command, "watchuser") == 0)
	{ 
		if(argsct < 2)
		{
			fprintf(stderr, "watchuser: Too few arguments.\n");
		}
		else if (argsct == 2)
		{
			//Prints out all current users on the watch list
			if (strcmp(args[1], "-l") == 0)
			{
				if(users != NULL)
				{	
					struct user* currUser = users;
					while(currUser->next != NULL)
					{
						printf("%s, ", currUser->name);
						currUser = currUser->next;
					}
					printf("%s\n", currUser->name);
					fflush(stdout);
				}
				else
				{
					printf("No users are being watched.\n");
					fflush(stdout);
				}
			}
			//Add a user to the watch list
			else
			{
				struct user* currUser;
				if(users != NULL)
				{
					currUser = users;
					while(currUser->next != NULL)
					{
						currUser = currUser->next; 
					}
					
					//Creates a element at the end of the list 
					currUser->next = malloc(sizeof (struct user));
					currUser = currUser->next;
				}
				else
				{
					users = malloc(sizeof (struct user));
					currUser = users;
				}

				currUser->name = malloc(strlen(args[1]) * sizeof (char));
				currUser->next = NULL;
				
				//initialize pthread and lock
				if(watchingUser == NULL)
				{
					watchingUser = malloc(sizeof(pthread_t));
					int error = pthread_create(watchingUser, NULL, watchuser, 0);
					if(error > 0)
					{
						printf("Error creating thread!");
					}
				}

				pthread_mutex_lock(&mutex_Users);
				strcpy(currUser->name, args[1]);
				pthread_mutex_unlock(&mutex_Users);
			}			
		}
		else
		{
			//Stop watching all instances of a user
			if(strcmp(args[2], "stop") == 0)
			{			
				//Checks if the head should be removedd
				while(users != NULL && strcmp(users->name, args[1]) == 0)
				{
					struct user* tmpUser = users->next;
					free(users->name);
					free(users);
					users = tmpUser;
				}
				
				if(users != NULL)
				{
					//Checks if the rest of the LL needs to be removed
					struct user* currUser = users;	

					while(currUser->next != NULL)
					{
						if(strcmp(currUser->next->name, args[1]) == 0)
						{
							struct user* tmpUser = currUser->next->next;
							free(currUser->next->name);
							free(currUser->next);
							currUser->next = tmpUser;
						}
						else
						{
							currUser = currUser->next;
						} 
					}
				}
			}
		}

	}
	else if (strcmp(command, "watchmail") == 0)
	{
		if(argsct < 2)
		{
			fprintf(stderr, "watchmail: Too few arguments.\n");
		}
		else
		{
			struct mail* currMail;

			if(fopen(args[1], "r") == NULL)
			{
				fprintf(stderr, "watchmail: File does not exist.\n");
			}
			else if(argsct > 2)
			{
				if(strcmp(args[2], "off") == 0)
				{
					if(mailingList == NULL)
					{
						fprintf(stderr, "watchmail: Nothing in list to turn off.\n");
					}
					//Free all values that match the off case
					else
					{
						currMail = mailingList;
						while(currMail->next != NULL)
						{
							if(strcmp(currMail->path, args[1]) == 0)
							{
								if(currMail->prev != NULL)
								{
									currMail->prev->next = currMail->next;
									currMail->next->prev = currMail->prev;
								}
								else
								{
									currMail->next->prev = NULL;
								}
								free(currMail->thread);
								free(currMail->path);
							}
						}
						if(strcmp(currMail->path, args[1]) == 0)
						{
							if(currMail->prev != NULL)
							{
								currMail->prev->next = currMail->next;
							}

							free(currMail->thread);
							free(currMail->path);
						}
					}
				}
			}
			else
			{				
				//Creates new list
				if(mailingList == NULL)
				{
					mailingList = malloc(sizeof(struct mail));
					mailingList->next = NULL;
					mailingList->prev = NULL;
					currMail = mailingList;
				}
				//Adds to end of the list
				else
				{
					currMail = mailingList;
					while(currMail->next != NULL)
					{
						currMail = currMail->next;
					}
					currMail->next = malloc(sizeof(struct mail));
					currMail->next->prev = currMail;
					currMail = currMail->next;
				}

				currMail->path = (char*)malloc(strlen(args[1]) * sizeof(char*));
				strcpy(currMail->path, args[1]);

				currMail->size = getFilesize(currMail->path);
				//Removes entry if there is an error getting file size
				if(currMail->size == -1)
				{
					fprintf(stderr, "watchmail: Issues calcuating size of file.\n");
					free(currMail->path);
					if(currMail == mailingList)
					{
						mailingList = NULL;
					}
					else
					{
						currMail->prev->next = NULL;
					}
					free(currMail);
				}
				//Creates a new thread that checks on the file
				else 
				{
					currMail->thread = malloc(sizeof(pthread_t));
					pthread_create(currMail->thread, NULL, watchmail, currMail);
					currMail->next = NULL;
				}
			}
		}
	}
	else{

        // If the command is not an alias or built in function, find it
        // in the path
	int found = 0;
	int status;
	char* toexec = malloc(MAX_CANON*sizeof(char));
	if(access(args[0], X_OK) == 0){
		found = 1;
		strcpy(toexec, args[0]);
	}
	else{
		struct pathelement* curr = pathlist;
		char *tmp = malloc(MAX_CANON*sizeof(char));
		
		while(curr!=NULL & !found){
			snprintf(tmp,MAX_CANON,"%s/%s", curr->element, args[0]);
			if(access(tmp, X_OK)==0){
				toexec = tmp;
				found = 1;		
			}
			curr=curr->next;
		}
	}
	
	// If the command if found in the path, execute it as a child process
	if(found){
		printf("Executing %s\n", toexec);

		if(argc > 1)
			background = (strcmp(args[argc-2], "&") == 0);//Checks for ampersand as last character
        	
		if (background)
			printf("testtest");
        		args[argc-2] = NULL; //Gets rid of ampersand
		
		// Create a child process
		cpid = fork();
		
		struct itimerval timer;		
		
		
		if(cpid == 0){
			// Child process executes command	
			execve(toexec, args, envp);	
		}
		else if(cpid == -1){
			perror(NULL);
		}
		else {
			// Parent process (shell) times child process 
			if(argc > 1){
				timer.it_value.tv_sec = atoi(argv[1]);
				timer.it_interval.tv_sec = 0;
				if(setitimer(ITIMER_REAL, &timer, NULL)==-1){
					perror(NULL);
				}
			}

			// Parent process (shell) waits for child process to terminate
			if(!background){
				waitpid(pid, &status, WNOHANG);
			}

			// Disable timer after child process terminates
			if(argc > 1){
				timer.it_value.tv_sec = 0;
				if(setitimer(ITIMER_REAL, &timer, NULL)==-1){
					perror(NULL);
				}
			}

			// Return non-normal exit status from child
			if(WIFEXITED(status)){
				if(WEXITSTATUS(status) != 0){
					printf("child exited with status: %d\n", WEXITSTATUS(status));
				}
			}
		}
	}

	// Give error if command not found
        else if (valid){
          fprintf(stderr, "%s: Command not found.\n", args[0]);
        }
      }
  }
  return 0;
} /* sh() */

/* function call for 'which' command
 * command: the command that you're checking
 * pathlist: the path list data structure
 * alist: the alias data structure
 */
char *which(char *command, struct pathelement *pathlist, struct alias *alist )
{
	// Set up path linked list variables
	struct pathelement *curr = pathlist;
	char *path = malloc(MAX_CANON*(sizeof(char)));
	int found = 0;
	

	// Search aliases for command
	struct alias* curra = alist;
	while(curra != NULL){
		if(strcmp(curra->name, command)==0){
			printf("%s:\t aliased to %s\n", curra->name, curra->cmd);
			found = 1;
		}
		curra=curra->next;
	}

	// Search path for command
	while(curr != NULL && !found){
		strcpy(path, curr->element);
		path[strlen(path)+1] = '\0';
		path[strlen(path)] = '/';
		strcat(path, command);
		if(access(path, F_OK) == 0){
			printf("%s\n", path);
			found = 1;
		}
		curr = curr->next;
	}

	// Print error if command not found
	if (!found){
		fprintf(stderr, "%s: command not found\n", command);
		return NULL;
	}

} /* which() */

void* watchuser(void* n)
{
	FILE *output;
	while(1)
	{
		sleep(SLEEPTIMER);
		//Locks beause main thread can write, not from concurrent read
		pthread_mutex_lock(&mutex_Users);
		struct user* currUser = users;
		//File to to show when a user was logged in
		output = fopen("watchuser_log.txt", "a");

		while (up = getutxent())	/* get an entry */
		{
			if ( up->ut_type == USER_PROCESS )	/* only care about users */
			{
				time_t rawtime;
				time ( &rawtime );
				//Checks if a user has a process
				while(currUser->next != NULL)
				{
					if(strcmp(currUser->name, up->ut_user) == 0)
					{
						fprintf(output,"%s: Logged in at %s.\n", currUser->name, asctime (localtime ( &rawtime )));
						printf("%s: Logged in at %s.\n", currUser->name, asctime (localtime ( &rawtime )));
					}
					currUser = currUser->next; 
				}
				if(strcmp(currUser->name, up->ut_user) == 0)
				{
					fprintf(output,"%s: Logged in at %s.\n", currUser->name, asctime (localtime ( &rawtime )));
					printf("%s: Logged in at %s.\n", currUser->name, asctime (localtime ( &rawtime )));
				}
				fflush(output);
			}
		}
		fclose(output);
		pthread_mutex_unlock(&mutex_Users);
	}
}

void* watchmail(void* n)
{
	struct mail* currMail = (struct mail*)n;
	size_t size;
	struct timeval tv;
	while(1)
	{
		sleep(1);
		size = getFilesize(currMail->path);
		if(currMail->size < size)
		{
			currMail->size = size;
			if(gettimeofday(tv.tv_sec, NULL) == 0)
			{			
				printf("\aYou've Got Mail!!! \nFILE: %s \nTIME: %s! \n", currMail->path, ctime(tv.tv_sec));
			}
			else
			{
				printf("\aYou've Got Mail!!!");
			}
			fflush(stdout);
		}
	}
}

size_t getFilesize(const char* filename)
 {
    struct stat st;
    if(stat(filename, &st) != 0) {
        return 0;
    }
    return st.st_size;   
}

void freeUsers()
{
	struct user* currUser = users;	

	while(currUser->next != NULL)
	{
		struct user* tmpUser = currUser->next->next;
		free(currUser->next->name);
		free(currUser->next);
		currUser->next = tmpUser;
	}
	
	free(currUser->name);
	free(currUser);
}
