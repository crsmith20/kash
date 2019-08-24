#include <fcntl.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>

#include "history.h"
#include "timer.h"

/* Background Struct for matching id and cmd number */
struct bg {
	pid_t id;
	unsigned long cmd_num;
};

/* prototypes */
void add(struct history_entry *entry);
void sigint_handler(int signo);
void sigchld_handler(int signo);
void getpwd();
void gettime();
void print_prompt();
void gethome();
void getcmd(char** cmd, struct history_entry *entry);
void getargs(char *tokens[10], char* line);
void cd(char *tokens[10], struct history_entry *entry);
void add_background(int id);
void remove_background(int id);
void print_background(struct history_entry *entry);
void execute(char** tokens, struct history_entry *entry);
void redirection(char** tokens, struct history_entry *entry);
void file_redirection(char** tokens, struct history_entry *entry);
int getentry(int id);
bool isbackground(int id);

/* Global Variables */
bool pipe_redirect = false;
bool file_redirect = false;
bool prompt = true;
bool forked = false;
bool background = false;
struct bg *background_cmd[1000] = { NULL };
int background_count = 0;
int g_result;
int g_line_count = 0;
char g_user[100];
char g_hostname[HOST_NAME_MAX];
char g_home[PATH_MAX];
char* g_pwd = NULL;
char g_curr_time[6];
struct history_entry *history[HIST_MAX] = { NULL };

/* Handles redirection, i.e. pipes and files */
void redirection(char** tokens, struct history_entry *entry) {
	int i = 0;
	int fd[2];
	while (*(tokens + i) != NULL) {
		// handles piping
		if (!strcmp(*(tokens + i), "|")) {
			*(tokens + i) = (char *) NULL;
			char** nextcmd = &tokens[i + 1];
		    if (*nextcmd == NULL) {
		    	break;
		    }
		    if (pipe(fd) == -1) {
		        perror("-kash: pipe");
		        break;
		    }
		    pid_t pid = fork();
		    if (pid == 0) {
		    	close(fd[0]);
		    	if (dup2(fd[1], STDOUT_FILENO) == -1) {
		    		perror("-kash: dup2");
		    		exit(0);
		    	}
		    	g_result = execvp(*tokens, tokens);
		    	if (g_result) {
					printf("-kash: %s: command not found\n", tokens[0]);
					exit(0);
				}
				close(fd[1]);
		    } else {
		    	close(fd[1]);
		    	if (dup2(fd[0], STDIN_FILENO) == -1) {
		    		perror("-kash: dup2");
		    		exit(0);
		    	}
		    	// close(fd[0]);
		    	redirection(nextcmd, entry);
		    	close(fd[0]);
		    }
		}
		i++;
	}
	execute(tokens, entry);
}

/* Handles file redirections */
void file_redirection(char** tokens, struct history_entry *entry) {
	int open_perms = 0644;
	int output_flags;
	int i = 0;
	while (tokens[i] != NULL) {
		if (!strcmp(tokens[i], ">")) {
			output_flags = O_RDWR | O_CREAT | O_TRUNC;
			break;
		} else if (!strcmp(tokens[i], ">>")) {
			output_flags = O_RDWR | O_CREAT | O_APPEND;
			break;
		}
		i++;
	}
	tokens[i] = (char *) NULL;
	if (tokens[i + 1] != NULL) {
		int fd = open(tokens[i + 1], output_flags, open_perms);
		if (dup2(fd, STDOUT_FILENO) == -1) {
			perror("-kash: dub2");
		}
	}
}

/* Checks if the given pid is a background process */
bool isbackground(int id) {
	int i;
	for (i = 0; i < background_count; i++) {
		if (background_cmd[i]->id == id) {
			return true;
		}
	}
	return false;
}

/* Prints all the current background jobs */
void print_background(struct history_entry *entry) {
	double start_time = get_time();
	int i, j, max;
	if (g_line_count < 100) {
		max = g_line_count;
	} else {
		max = HIST_MAX;
	}
	// prints background
	for (i = 0; i < background_count; i++) {
		for (j = 0; j < max; j++) {
			if (background_cmd[i]->cmd_num == history[j]->cmd_id) {
				printf("%ld|%s\n", history[j]->cmd_id, history[j]->cmd);
				break;
			}
		}
	}
	// finishes entry and adds it
	double end_time = get_time();
	entry->run_time = end_time - start_time;
	entry->cmd_id = g_line_count;
	strcpy(entry->time, g_curr_time);
	add(entry);
}

/* returns the command_id of the given pid */
int getentry(int id) {
	int i;
	for (i = 0; i < background_count; i++) {
		if (background_cmd[i]->id == id) {
			return background_cmd[i]->cmd_num;
		}
	}
	return -1;
}

/* Removes the background id from background processes*/
void remove_background(int id) {
	int i = 0;
	while (background_cmd[i]->id != id && i < 1000) {
		i++;
	}
	free(background_cmd[i]);
	if (i + 1 < 1000 && background_cmd[i + 1] != NULL) {
		while (background_cmd[i] != NULL && i < 999) {
			background_cmd[i] = background_cmd[i + 1];
			i++;
		}
	} else {
		background_cmd[i] = NULL;
	}
	background_count--;
}

/* Adds the background id to background processes */
void add_background(int id) {
	struct bg *background = malloc(sizeof(struct bg));
	background->id = id;
	background->cmd_num = g_line_count;
	background_cmd[background_count] = background;
	background_count++;
}

/* Gets the hom directory for the user */
void gethome() {
	struct passwd *pw = getpwuid(getuid());
	strcpy(g_home, pw->pw_dir);
}

/* Changes the directory. Handles "~" for home directory.
   Since cd is a command, adds to history. */
void cd(char *tokens[10], struct history_entry *entry) {
	int i;
	double start_time = get_time();
	char temp[PATH_MAX];
	// handling "~" home directory
	if (tokens[1] != NULL) {
		strcpy(temp, tokens[1]);
	} else {
		strcpy(temp, "~");
	}
	if (strstr(temp, "~") == &temp[0]) {
		strcpy(temp, g_home);
		int end = strlen(temp);
		if (tokens[1] != NULL) {
			for (i = 1; i < strlen(tokens[1]); i++) {
				temp[end] = tokens[1][i];
				end++;
			}
			temp[end] = '\0';
		}
	}
	// actually changing of dir
	g_result = chdir(temp);
	if (g_result) {
		printf("-kash: %s: %s: No such directory\n", tokens[0], tokens[1]);
	} else {
		getpwd();
	}
	// adds to history
	double end_time = get_time();
	entry->run_time = end_time - start_time;
	entry->cmd_id = g_line_count;
	strcpy(entry->time, g_curr_time);
	add(entry);
}

/* Tokenizes string to get arguments. Handles comments and quotes */
void getargs(char *tokens[10], char* line) {
	int i = 0;
	// handles redirection
	if (strstr(line, "|") != NULL) {
		pipe_redirect = true;
	}
	if (strstr(line, ">") != NULL || strstr(line, ">>") != NULL) {
		file_redirect = true;
	}
	// handles comments
	if (strstr(line, "#") != NULL) {
		while (line[i] != 35) {
			i++;
		}
		line[i] = '\0';
	}
	
	// quote handling/tokenizing for getting arguments
	bool start = false;
    if (line[0] == 34) {
		start = true;
	}
	char *token2;
	char *token = strtok_r(line, "\"", &token2);
	i = 0;
	while (token != NULL) {
		if (!start) {
	        char *temp1 = malloc(strlen(token) + 1 * sizeof(char));
	        strcpy(temp1, token);
	        char *temp2 = strtok(temp1, " \t\n\r");
	        while (temp2 != NULL) {
	            tokens[i++] = strdup(temp2);
	            temp2 = strtok(NULL, " \t\n\r");
	        }
	        start = true;
	        free(temp1);
	    } else {
	        int j = strlen(token);
		    int letter = 1;
		    char *temp = malloc((j + 3) * sizeof(char));
		    temp[0] = '"';
		    for (j = 0; j < strlen(token); j++) {
		        temp[letter] = token[j];
		        letter++;
		    }
		    temp[letter] = '"';
		    temp[letter + 1] = '\0';
	        tokens[i++] = strdup(temp);
	        free(temp);
	    }
		token = strtok_r(NULL, "\"\n", &token2);
	}
	tokens[i] = (char *) NULL;
	// checks if a background process
	if (tokens[0] != NULL && strstr(tokens[0], "&") == &(tokens[0])[strlen(tokens[0]) - 1]) {
		background = true;
		// removes the "&" character
		tokens[0][strlen(tokens[0]) - 1] = '\0';
	}
}

/* Gets cmd from history */
void getcmd(char** cmd, struct history_entry *entry) {
	// checks if looking for last cmd
	if (!strcmp(*cmd, "!!")) {
		// handles if first cmd given since starting
		if (g_line_count == 0) {
			return;
		}
		strcpy(entry->cmd, history[g_line_count - 1]->cmd);
		getargs(cmd, history[g_line_count - 1]->cmd);
		return;
	}
	// handles bang
	char* temp = malloc(strlen(*cmd) * sizeof(char));
	strcpy(temp, *cmd);
	if (strstr(temp, "!") == (&(temp))[0]) {
		int i;
		for (i = 0; i < strlen(*cmd) - 1; i++) {
			temp[i] = temp[i + 1];
		}
		temp[i] = '\0';
		int num = atoi(temp);
		// if ! given was a command
		if (num == 0 && strlen(temp) > 1 && temp[0] != 48) {
			for (i = g_line_count - 1; i >= 0; i--) {
				if (strstr(history[i]->cmd, temp) == &history[i]->cmd[0]) {
					strcpy(entry->cmd, history[i]->cmd);
					getargs(cmd, history[i]->cmd);
					free(temp);
					return;
				}
			}
		// if ! given was a number
		} else {
			// makes sure we don't go outside of bounds
			int max;
			if (g_line_count < HIST_MAX) {
				max = g_line_count;
			} else {
				max = HIST_MAX;
			}
			// iterate over history to find command by id
			for (i = 0; i < max; i++) {
				if (num == history[i]->cmd_id) {
					strcpy(entry->cmd, history[i]->cmd);
					getargs(cmd, history[i]->cmd);
					free(temp);
					return;
				}
			}
		}
	}
	free(temp);
}

/* Adds history entry to the history array */
void add(struct history_entry *entry) {
	if (g_line_count < HIST_MAX) {
		history[g_line_count] = entry;
	} else {
		free(history[0]);
		int i;
		for (i = 0; i < HIST_MAX - 1; i++) {
			history[i] = history[i + 1];
		}
		history[i] = entry;
	}
	g_line_count++;
}

/* Handles SIGINT signal */
void sigint_handler(int signo) {
	printf("\n");
	if (!forked) { 
		print_prompt();
	}
	fflush(stdout);
}

/* Handles SIGCHLD signal */
void sigchld_handler(int signo) {
	int status;
	pid_t chld = wait(&status);
	// if it's not a background process exit this function
	if (!isbackground(chld)) {
		return;
	}
	// gets entry id
	double end_time = get_time();
	int id = getentry(chld);
	if (id == -1) {
		printf("-kash: there was an error locating background process\n");
	}
	int max;
	if (g_line_count < HIST_MAX) {
		max = g_line_count;
	} else {
		max = HIST_MAX;
	}
	// updates runtime and removes command from background processes
	int i;
	for (i = 0; i < max; i++) {
		if (history[i]->cmd_id == id) {
			history[i]->run_time = end_time - history[i]->run_time;
			remove_background(chld);
			break;
		}
	}
}

/* Gets the present working directory */
void getpwd() {
	char temp[PATH_MAX];
	g_pwd = getcwd(NULL, 0);
	// if the current dir is the home dir then replace with "~"
	if (!strcmp(g_pwd, g_home)) {
		g_pwd = "~";
	} else if (strstr(g_pwd, g_home) == &g_pwd[0]) {
		// if contains home dir at the beginning
		// replace with "~" at the beignning
		int i = 0;
		while (g_pwd[i] == g_home[i]) {
			i++;
		}
		temp[0] = '~';
		int j = 1;
		for (i = i; i < strlen(g_pwd); i++) {
			temp[j] = g_pwd[i];
			j++;
		}
		temp[j] = '\0';
		strcpy(g_pwd, temp);
	}
}

/* Gets the time in 24 hour time format */
void gettime() {
	// gets rawtime
	time_t rawtime = time(0);
	struct tm * timeinfo;
	// converts it into local time
	timeinfo = localtime(&rawtime);
	char* datetime = asctime(timeinfo);
	char* token = strtok(datetime, " ");
	// converts localtime to 24 hour format
	int i;
	for (i = 0; i < 3; i++) {
		token = strtok(NULL, " ");
	}
	for (i = 0; i < strlen(token) - 3; i++) {
		g_curr_time[i] = token[i];
	}
	g_curr_time[i] = '\0';
}

/* Prints the standard kash prompt */
void print_prompt() {
	// if prompt is turned on print
	if (prompt) {
		// gets time
		gettime();
		printf("[%d|%s|%s@%s:%s]$ ", g_line_count, g_curr_time, g_user, g_hostname, g_pwd);
		fflush(stdout);
	}
}

void execute(char** tokens, struct history_entry *entry) {
	// time holders
	double end_time;
	// forks the process
	pid_t pid = fork();
	forked = true;
	if (pid == 0) {
		// child process
		// if history is given break from the child to print history
		if (!strcmp(tokens[0], "history")) {
			// prints the history for last 100 cmds
			print_history(history);
			exit(0);
		}

		if (file_redirect) {
			file_redirection(tokens, entry);
		}
		// execute the given command line areguments
		g_result = execvp(tokens[0], tokens);
		if (g_result) {
			printf("-kash: %s: command not found\n", tokens[0]);
			exit(0);
		}
	} else {
		//parent process
		// if its a background process
		if (!background) {
			int status;
			wait(&status); // waits for child to finish
			end_time = get_time();
			forked = false;
			// sets data for the entry and adds it
			entry->run_time = end_time - entry->run_time;
			entry->cmd_id = g_line_count;
			strcpy(entry->time, g_curr_time);
			add(entry);
		} else {
			// adds it to background process and adds information for entry
			entry->cmd_id = g_line_count;
			add_background(pid);
			strcpy(entry->time, g_curr_time);
			add(entry);
			background = false;
		}
		file_redirect = false;
		pipe_redirect = false;
	}
}

/* Main method handles signals and input */
int main(void) {

	signal(SIGINT, sigint_handler);
	signal(SIGCHLD, sigchld_handler);

	g_result = getlogin_r(g_user, 100);
	if (g_result) {
		perror("-kash: couldn't get user");
	}
	g_result = gethostname(g_hostname, 100);
	if (g_result) {
		perror("-kash: couldn't get hostname");
	}
	gethome();
	getpwd();
	if (!isatty(STDIN_FILENO)) {
        prompt = false;
    }

	// main loop: loop forever prompting user for commands
	while (true) {
		struct history_entry *entry = malloc(sizeof(struct history_entry));
		print_prompt();

		// variables to get line from standard in
		char *line = NULL;
		size_t line_sz = 0;
		g_result = getline(&line, &line_sz, stdin);
		if (g_result == -1 && !prompt) {
			return 0;
		}

		// if its an empty entry
		if (strlen(line) == 1 || g_result == -1) {
			continue;
		}
		// copy cmd to entry
		strcpy(entry->cmd, strtok(line, "\n"));
		char *tokens[10];
		getargs(tokens, line);

		// checks if it's exit
		if (!strcmp(tokens[0], "exit")) {
			exit(0);
		}

		// checks it ! is included
		if (strstr(tokens[0], "!") && strlen(tokens[0]) > 1) {
			getcmd(&tokens[0], entry);
		}

		// checks if cd was gien to change directory
		if (!strcmp(tokens[0], "cd")) {
			cd(tokens, entry);
			continue;
		}

		// checks if jobs was given
		if (!strcmp(tokens[0], "jobs")) {
			print_background(entry);
			continue;
		}

		// checks if a pipe redirection was given
		if (pipe_redirect || (pipe_redirect && file_redirect)) {
			entry->run_time = get_time();
		    redirection(tokens, entry);
		}

		if (!pipe_redirect) {
			entry->run_time = get_time();
			execute(tokens, entry);
		}
	}
    return 0;
}
