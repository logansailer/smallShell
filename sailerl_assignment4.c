#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/*
Author: Logan Sailer
File: sailerl_assignment4.c
Date: 2/28/26
Description: A shell written in C, with a subset of known shell commands and ability
             run programs in both foreground and background.
*/

#define INPUT_LENGTH 2048
#define MAX_ARGS 512
#define MAX_BACKGROUND_PROCESSES 100
int last_status = 0;
int background_count = 0;
pid_t background_pids[MAX_BACKGROUND_PROCESSES];
int foreground_only_mode = 0;


struct command_line
{
	char *argv[MAX_ARGS + 1];
	int argc;
	char *input_file;
	char *output_file;
	bool is_bg;
};

void handle_SIGTSTP(int signo){
    if (foreground_only_mode == 0){
        char* message = "\nEntering foreground-only mode (& is now ignored)\n: ";
        write(STDOUT_FILENO, message, strlen(message));
        foreground_only_mode = 1;
    } else {
        char* message = "\nExiting foreground-only mode\n: ";
        write(STDOUT_FILENO, message, strlen(message));
        foreground_only_mode = 0;
    }
}

// redirects input
void direct_input(struct command_line *command){
    int targetFD = open(command->input_file, O_RDONLY);
    if (targetFD == -1) {
        printf("cannot open %s for input\n", command->input_file);
        exit(1);
    }
    int result = dup2(targetFD, 0);
    if (result == -1){
        perror("dup2");
        exit(2);
    }
}

// redirects output
void direct_output(struct command_line *command){
    int targetFD = open(command->output_file, O_WRONLY | O_CREAT);
    if (targetFD == -1) {
        printf("cannot open %s for output\n", command->output_file);
        exit(1);
    }
    int result = dup2(targetFD, 1);
    if (result == -1){
        perror("dup2");
        exit(2);
    }
}

// checks to see if background processes are running
void check_background(){
    int child_status;
    pid_t finished_pid;

    while ((finished_pid = waitpid(-1, &child_status, WNOHANG)) > 0 ){
        if(WIFEXITED(child_status)) {
            printf("background pid %d is done: exit value %d\n", finished_pid, WEXITSTATUS(child_status));
        } else if(WIFSIGNALED(child_status)) {
            printf("background pid %d is done: terminated by signal %d\n", finished_pid, WTERMSIG(child_status));
        }
    };
}

// runs the passed command in the foreground
void run_foreground(pid_t child_pid){
    int child_status;
    child_pid = waitpid(child_pid, &child_status, 0);
    last_status = child_status;
    if(WIFSIGNALED(child_status)) {
        last_status = WTERMSIG(child_status);
        printf("terminated by signal %d\n", WTERMSIG(last_status));
    }
}

// runs the passed command in the background
void run_background(pid_t child_pid){
    int child_status;
    printf("background pid is %d \n", child_pid);
    background_pids[background_count] = child_pid;
    background_count++;
}

// executes non built-in commands using fork, exec, and waitpid
void other_commands(struct command_line *command){
    pid_t child_pid = fork();

    switch(child_pid){
    // fork fails
    case -1:
        perror("fork() failed!");
        exit(EXIT_FAILURE);

    // child branch    
    case 0:
    // register signals for SIGINT
    struct sigaction SIGINT_action = {0};
    if (command->is_bg){
        SIGINT_action.sa_handler = SIG_IGN;
    } else {
        SIGINT_action.sa_handler = SIG_DFL;
    }
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);

    //registers signals for SIGTSTP
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = SIG_IGN;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = 0;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    // handle input direction
    if (command->input_file != NULL){
        direct_input(command);
    }
    if (command->output_file != NULL){
        direct_output(command);
    }

    execvp(command->argv[0], command->argv);
    printf("%s: no such file or directory\n", command->argv[0]);
    exit(EXIT_FAILURE);

    default:
        if (command->is_bg){
            run_background(child_pid);
            return;
        } else {
            run_foreground(child_pid);
            return;
        }
    }
}

// kills any running processes and exits the shell
void shell_exit(){
    for(int i = 0; i < background_count; i++){
        kill(background_pids[i], SIGTERM);
    }
    exit(0);
}

// changes the working directory
void shell_cd(struct command_line *command){
    // if no arguements other than cd, changes to directory in HOME env
    if (command->argc == 1){
        char *home = getenv("HOME");
        chdir(home);
    } 
    // if 1 arguement after cd, change to directory specified in arguement
    if (command->argc == 2){
        chdir(command->argv[1]);
    }
}

// prints exit status of of the last foreground process
void shell_status(){
    if(WIFEXITED(last_status)){
        printf("exit value %d\n", WEXITSTATUS(last_status));
    } else {
        printf("terminated by signal %d\n", WTERMSIG(last_status));
    }
}

// processes the command
void process_input(struct command_line *command){
    if (command->argc == 0 || command->argv[0][0] == '#'){
        return;
    }
    
    if (strcmp(command->argv[0], "exit") == 0){
        shell_exit(command);
        return;
    }
    if (strcmp(command->argv[0], "cd") == 0){
        shell_cd(command);
        return;
    }
    if (strcmp(command->argv[0], "status") == 0){
        shell_status();
        return;
    } else {
        other_commands(command);
    }
}


// extracts arguements from command line
struct command_line *parse_input()
{
	char input[INPUT_LENGTH];
	struct command_line *curr_command = (struct command_line *) calloc(1, sizeof(struct command_line));

	// Get input
	printf(": ");
	fflush(stdout);
	fgets(input, INPUT_LENGTH, stdin);

	// Tokenize the input
	char *token = strtok(input, " \n");
	while(token){
		if(!strcmp(token,"<")) {
			curr_command->input_file = strdup(strtok(NULL," \n"));
		} else if(!strcmp(token,">")) {
			curr_command->output_file = strdup(strtok(NULL," \n"));
		} else if(!strcmp(token,"&")) {
			curr_command->is_bg = true;
		} else {
			curr_command->argv[curr_command->argc++] = strdup(token);
		}
		token=strtok(NULL," \n");
	}
	return curr_command;
}


int main()
{
    // register handle_SIGINT as signal handler
    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = SIG_IGN;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);

    // register handle_SIGSTP as signal handler
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    // get command line arguements
	struct command_line *curr_command;
	while(true)
	{
        check_background();
		curr_command = parse_input();
        if (foreground_only_mode == 1) {
            curr_command->is_bg = 0;
        }
        process_input(curr_command);
	}
	return EXIT_SUCCESS;
}