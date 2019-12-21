#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "jobs.h"
#include <signal.h>

#define STDIN 0
#define REDIR_OUT 1
#define REDIR_APPEND 2
#define REDIR_IN 0
#define UNCHANGED 4
#define NOPATH 3
#define MULT_IN 5
#define JOB_COMMAND 8
#define NOT_JOB_COMMAND 9
#define BACKGROUND 6

// Global Variables
job_list_t *job_list;
int job_id = 1;

// Forward Declarations
void parse(char* cmd_line_buf);
int contains_f_flag(char *argv[]);
int is_redirected(char *file_path, char *input_path, char* output_path, char *append_path);
void run_commands(char *file_path, char *argv[], char *input_path,
                  char *output_path, char *append_path, int argc);
int is_job_command(char *file_path, char *job_num, char *next);
void reap_foreground(int status, int child_pid, char *file_path);
void reap_background(void);
void reap(int status, int pid, int f_or_g);
void error_exit(char *error_message);

/*
 * Main function
 */
int main() {
    long int read_state = 0;
    job_list = init_job_list();
    char cmd_line_buf[BUFSIZ];
    
    // Install signal handlers
    if (signal(SIGINT, SIG_IGN) == SIG_ERR)
        error_exit("signal");
    if (signal(SIGTSTP, SIG_IGN) == SIG_ERR)
        error_exit("signal");
    if (signal(SIGQUIT, SIG_IGN) == SIG_ERR)
        error_exit("signal");
    if (signal(SIGTTOU, SIG_IGN) == SIG_ERR)
        error_exit("signal");
    
    while (1) {
        memset(cmd_line_buf, 0, BUFSIZ);
        reap_background();
        
#ifdef PROMPT
        if (write(1, "33sh> ", 7) == -1) {
            perror("write");
            cleanup_job_list(job_list);
            return 1;
        }
#endif
    
        // Read from stdin and check for EOF
        read_state = read(STDIN, cmd_line_buf, BUFSIZ);
        if (read_state  == -1) {
            perror("read");
            cleanup_job_list(job_list);
            return 1;
        } else if (read_state == 0){
            cleanup_job_list(job_list);
            return 0;
        }
        
        // Parse terminal input and clear it each time to allow for new inputs
        parse(cmd_line_buf);
    }
    return 0;
}


/*
 * Parses the input buffer and makes calls to helper functions to handle redirection
 *
 * Parameters:
 *  - cmd_line_buf: The array of command line input filled by the read sys call
 */
void parse(char* cmd_line_buf){
    
    // argv: array of char* to store execv input
    char *(argv[BUFSIZ]);
    int argc = 1;
    char *file_path;
    char in_path[BUFSIZ];
    char out_path[BUFSIZ];
    char app_path[BUFSIZ];
    memset(in_path, 0, BUFSIZ);
    memset(out_path, 0, BUFSIZ);
    memset(app_path, 0, BUFSIZ);
    int state;
    
    // Don't break if a newline is entered by itself
    file_path = strtok(cmd_line_buf, " \t\n");
    if(file_path == NULL){
        return;
    }
    
    // Check if first input requires redirection (is >>, >, or <)
    state = is_redirected(file_path, in_path, out_path, app_path);
    if (state == NOPATH){
        return;
    }
    if(state != UNCHANGED && state != NOPATH){
        file_path = strtok(NULL, " \t\n");
        if(file_path == NULL){
            fprintf(stderr, "error: redirects with no command\n");
            return;
        }
    }
    
    // Parse remaining arguments and check for redirection
    argv[argc] = strtok(NULL, " \t\n");
    while(argv[argc] != NULL){
        state = is_redirected(argv[argc], in_path, out_path, app_path);
        if (state == NOPATH){
            return;
        }
        if(state != UNCHANGED && state != NOPATH){
            if(state == MULT_IN)
                return;
            argv[argc] = strtok(NULL, " \t\n");
            continue;
        }
        argv[++argc] = strtok(NULL, " \t\n");
    }
    
    // Check if first input is fg, bg, or jobs
    state = is_job_command(file_path, argv[1], argv[2]);
    if(state == JOB_COMMAND || state == -1){
        return;
    }
    
    // Copy filepath into temp_path so strtok doesn't destroy file_path
    char temp_path[BUFSIZ];
    char *temp_argv;
    strcpy(temp_path, file_path);
    temp_argv = strtok(temp_path, "/");
    
    // Loop through and use strtok to find final path component
    while(temp_argv != NULL){
        argv[0] = temp_argv;
        temp_argv = strtok(NULL, "/");
    }
    
    run_commands(file_path, argv, in_path, out_path, app_path, argc);
}

/*
 * Checks if the first parsed token is bg, fg, or jobs and handles them accordingly
 *
 * Parameters:
 *  - command: The array of command line input filled by the read sys call
 *  - job_num: second parsed token, potential job number
 */
int is_job_command(char *command, char *job_num, char *next){
    int status;
    if(strcmp(command, "jobs") == 0){
        if(job_num == NULL){
            jobs(job_list);
            return JOB_COMMAND;
        } else {
            fprintf(stderr, "syntax error\n");
            return -1;
        }
    }
    
    if(strcmp(command, "bg") == 0 || strcmp(command, "fg") == 0){
        if(job_num == NULL){
            fprintf(stderr, "syntax error\n");
            return -1;
        }
        if(*job_num++ != '%'){
            fprintf(stderr, "%s: job input does not begin with %%\n", command);
            return -1;
        }
        pid_t job_pid = get_job_pid(job_list, atoi(job_num));
        if(job_pid == -1){
            fprintf(stderr, "job not found\n");
            return -1;
        }
        
        // Handle continuing background jobs
        if(strcmp(command, "bg") == 0){
            if(next != NULL){
                fprintf(stderr, "bg: syntax error\n");
                return -1;
            }
            if(kill(-job_pid, SIGCONT) == -1){
                error_exit("kill");
            }
            if(waitpid(job_pid, &status, WUNTRACED | WCONTINUED | WNOHANG) == -1){
                fprintf(stderr, "failed during waitpid()\n");
                return -1;
            }
            reap(status, job_pid, BACKGROUND);
        }
        
        // Handle continuing foreground jobs
        if(strcmp(command, "fg") == 0){
            if(next != NULL){
                fprintf(stderr, "fg: syntax error\n");
                return -1;
            }
            if(tcsetpgrp(STDIN_FILENO, job_pid) == -1){
                error_exit("tcsetpgrp");
            }
            if(kill(-job_pid, SIGCONT) == -1){
                error_exit("kill");
            }
            if(waitpid(job_pid, &status, WUNTRACED) == -1){
                fprintf(stderr, "waitpid error\n");
                return -1;
            }
            reap(status, job_pid, 0);
            if(tcsetpgrp(STDIN_FILENO, getpid()) == -1){
                error_exit("tcsetpgrp");
            }
        }
        return JOB_COMMAND;
    }
    return NOT_JOB_COMMAND;
}

/*
 * Determines if the argument array contains the -f flag
 *
 * Parameters:
 *  - argv: The argument array
 *
 * Returns:
 *  - 0 if -f is not in the array, 1 if it is in the array
 */
int contains_f_flag(char *argv[]){
    
    // first argv[0] will never be the -f flag, start checking at index = 1
    int count = 1;
    while(argv[count] != NULL){
        if (strstr(argv[count++], "-f") != NULL){
            return 1;
        }
    }
    return 0;
}

/*
 * Determines if the current call to strtok when looping through the input buffer filled by the
 * read() sys call is a redirection symbol. If so and the corresponding IO buffer is not already filled,
 * strtok is called again and the respective IO buffer is filled if the follow up path is not null
 *
 * Parameters:
 *  - file_path: Current call to strtok from the parse function, potential redirection
 *  - input_path: char buffer where each element is initially set to 0
 *  - output_path: char buffer where each element is initially set to 0
 *  - append_path: char buffer where each element is initially set to 0
 *
 * Returns:
 *  - REDIR_IN (int value 0) for redirected input
 *  - REDIR_OUT (int value 1) for redirected output not append
 *  - REDIR_APPEND (int value 2)for redirected as append
 *  - NOPATH (3) for cases where input is a redirection symbol but has null follow up path.
 *  - UNCHANGED (int value 4) for file_path is a not a redirection symbol
 */
int is_redirected(char *file_path, char *input_path, char* output_path, char *append_path){

    
    // Check for redirection tokens, return respective signal
    if (strcmp(file_path, "<") == 0){
        if(input_path[0] != 0){
            fprintf(stderr, "syntax error: multiple input files\n");
            return MULT_IN;
        }
        char *temp = strtok(NULL, " \t\n");
        
        // Check if there is a potential path. Return nopath(int value 3) if not
        if(temp == NULL){
            fprintf(stderr, "syntax error: no input file\n");
            return NOPATH;
        }
        strcpy(input_path, temp);
        return REDIR_IN;
    } else if (strcmp(file_path, ">") == 0 && output_path[0] == 0){
        char *temp = strtok(NULL, " \t\n");
        
        // Check if there is a potential path. Return nopath(int value 3) if not
        if(temp == NULL){
            fprintf(stderr, "syntax error: no output file\n");
            return NOPATH;
        }
        strcpy(output_path, temp);
        return REDIR_OUT;
    } else if (strcmp(file_path, ">>") == 0 && append_path[0] == 0){
        char *temp = strtok(NULL, " \t\n");
        
        // Check if there is a potential path. Return nopath(int value 3) if not
        if(temp == NULL){
            fprintf(stderr, "syntax error: no output file\n");
            return NOPATH;
        }
        strcpy(append_path, temp);
        return REDIR_APPEND;
    }
    return UNCHANGED;
}

/*
 * Makes checks using strcmp to see if the final path component is a built-in command. If so, then a system
 * call is called accordingly. Otherwise, we use fork() and wait to create a child process and halt the current
 * process. In the child process, we check to see if the values of input_path, output_path, or append_path have
 * changed. If so, stdin or stdout are replaced accordingly and execv is called to execute the non-builtin
 *
 *
 * Parameters:
 *  - file_path: Full file path
 *  - argv: arguments to the command
 *  - input_path: input redirection buffer
 *  - output_path: output redirection buffer
 *  - append_path: output redirection buffer
 *
 */
void run_commands(char *file_path, char *argv[], char *input_path,
                  char *output_path, char *append_path, int argc){
    
    // Check if the command is a built in and run accordingly
    if(strcmp(argv[0], "cd") == 0){
        if(argv[1] == 0){
            fprintf(stderr, "cd: syntax error\n");
        }
        else{
            if(chdir(argv[1]) != 0){
                perror("cd");
            }
        }
    }
    else if(strcmp(argv[0], "ln") == 0){
        if(argv[1] == 0){
            fprintf(stderr, "ln: syntax error\n");
        }
        else {
            if(link(argv[1], argv[2]) != 0){
                perror("ln");
            }
        }
    } else if(strcmp(argv[0], "rm") == 0){
        if(argv[1] == 0){
            fprintf(stderr, "rm: syntax error\n");
        }
        else {
            if(unlink(argv[1]) != 0 && !contains_f_flag(argv)){
                perror("rm");
            }
        }
    }
    else if(strcmp(argv[0], "exit") == 0){
        cleanup_job_list(job_list);
        exit(0);
    }
    
    // Handle cases where the commamnd could be a non-built-in
    else{
        pid_t child_pid;
        int status;
        int is_background = strcmp(argv[argc-1], "&") == 0;
        
        if((child_pid = fork()) == 0){
            pid_t c_id = getpid();
            if(setpgid(c_id, c_id) == -1)
                error_exit("setpgid");
            
            // Handle background jobs
            if(!is_background){
                if(tcsetpgrp(STDIN_FILENO, c_id) == -1){
                    error_exit("tcsetpgrp");
                }
            } else{
                argv[argc-1] = NULL;
            }
            
            // Install handlers back to default (exit if fails)
            if(signal(SIGINT, SIG_DFL) == SIG_ERR)
                error_exit("signal");
            if(signal(SIGQUIT, SIG_DFL) == SIG_ERR)
                error_exit("signal");
            if(signal(SIGTTOU, SIG_DFL) == SIG_ERR)
                error_exit("signal");
            if(signal(SIGTSTP, SIG_DFL) == SIG_ERR)
                error_exit("signal");
            
            if(input_path[0] != '\0'){
                if(close(STDIN_FILENO) == -1){
                    error_exit("closing input file");
                }
                if(open(input_path, O_RDONLY, 0400) == -1){
                    perror("open");
                    return;
                }
            }
            if(output_path[0] != '\0'){
                if(close(STDOUT_FILENO) == -1){
                    error_exit("closing output file");
                }
                if(open(output_path, O_RDWR | O_CREAT | O_TRUNC, 0600) == -1){
                    perror("opening output file");
                    return;
                }
            }
            else if(append_path[0] != '\0'){
                if(close(STDOUT_FILENO) == -1){
                    error_exit("closing output file");
                }
                if(open(append_path, O_RDWR | O_CREAT | O_APPEND, 0600) == -1){
                    perror("opening output file");
                    return;
                }
            }
            execv(file_path, argv);
            error_exit("execv");
        }
        
        if(child_pid == -1){
            perror("fork");
            return;
        }
        // Handle foreground signal termination and job adding
        if(!is_background && child_pid > 0){
            if(waitpid(child_pid, &status, WUNTRACED) == -1){
                fprintf(stderr, "error: waitpid()");
            } else{
                reap_foreground(status, child_pid, file_path);
            }
            if(tcsetpgrp(0, getpid()) == -1)
                error_exit("tcsetpgrp");
        } else if(child_pid > 0){
            if(add_job(job_list, job_id, child_pid, RUNNING, file_path) == -1){
                fprintf(stderr, "Failed to add job\n");
                cleanup_job_list(job_list);
                exit(1);
            }
            printf("[%d] (%d)\n", get_job_jid(job_list, child_pid), get_job_pid(job_list, job_id++));
            fflush(stdout);
        }
    }
}

void reap_foreground(int status, int child_pid, char *file_path){
    if(WIFSIGNALED(status)){
        printf("[%d] (%d) terminated by signal %d\n", job_id, child_pid, WTERMSIG(status));
    }
    if (WIFSTOPPED(status)) {
        printf("[%d] (%d) suspended by signal %d\n", job_id, child_pid, WSTOPSIG(status));
        if(add_job(job_list, job_id++, child_pid, STOPPED, file_path) == -1){
            fprintf(stderr, "Failed to add job\n");
            cleanup_job_list(job_list);
            exit(1);
        }
    }
    fflush(stdout);
}

/*
 * Takes no input. Loops through the jobs list
 * and reaps background jobs
 */
void reap_background(){
    int status;
    pid_t curr_pid;
    
    // reset to head
    while(get_next_pid(job_list) != -1){
        ;
    }
    curr_pid = get_next_pid(job_list);
    while(curr_pid != -1 && waitpid(curr_pid, &status, WNOHANG | WUNTRACED | WCONTINUED) > 0){
        reap(status, curr_pid, BACKGROUND);
        curr_pid = get_next_pid(job_list);
    }
}

/*
 * Reaps the current job specified by pid
 *
 * Parameters;
 *  - status: integer representing status updated by waitpid()
 *  - pid: pid of the current job being analyzed
 *  - f_or_b: indicator for printing WIFEXITED status
 */
void reap(int status, pid_t pid, int f_or_b){
    int jid = get_job_jid(job_list, pid);
    if(jid == -1)
        return;
    int err = 0;
    if(WIFEXITED(status)){
        if(f_or_b == BACKGROUND)
            printf("[%d] (%d) terminated with exit status %d\n", jid, pid, WEXITSTATUS(status));
        err = remove_job_pid(job_list, pid);
    } else if(WIFSIGNALED(status)){
        printf("[%d] (%d) terminated by signal %d\n", jid, pid, WTERMSIG(status));
        err = remove_job_pid(job_list, pid);
    } else if (WIFCONTINUED(status)) {
        printf("[%d] (%d) resumed\n", jid, pid);
        err = update_job_jid(job_list, jid, RUNNING);
    } else if (WIFSTOPPED(status)) {
        printf("[%d] (%d) suspended by signal %d\n", jid, pid, WSTOPSIG(status));
        err = update_job_jid(job_list, jid, STOPPED);
    }
    if(err == -1){
        fprintf(stderr, "failed to update or remove job\n");
        cleanup_job_list(job_list);
        exit(1);
    }
    fflush(stdout);
}

// Handle error messages that require exiting the program
void error_exit(char *error_message){
    perror(error_message);
    cleanup_job_list(job_list);
    exit(1);
}
