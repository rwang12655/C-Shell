Shell Readme:


Overview:
My implementation of a shell which parses command line input, handles system calls using fork() to take advantage of
multitasking protocols, allows for input/output file redirection, handles terminating signals (SIGINT, SIGTSTP,
and SIGQUIT) according to the job type, and is capable of creating, manipulating, and terminating multiple
jobs (one foreground and multiple background) in a memory safe way.


How to compile and run:
Once you're in the directory with the Makefile and sh.c, simply run "make clean all" to erase any previously
compiled programs and compile the up-to-date code. If you want to use the shell with the prompt "33sh>",
run ""./33sh". If you want to use the shell without the prompt, type "./33noprompt".


How the pieces fit together:

Parse:
This function takes in the input buffer and is responsible for tokenizing the buffer and separating out the
redirection symbols, the full file path, the final path component, and the args. On a macro level, this parse
function loops through the input buffer and uses strtok to separate it (by whitespaces). For each call to strtok,
the function checks to see if the returned char pointer is a redirection symbol using the is_redirected function
(which also stores the path into appropriate path if possible). Once all the looping has finished, strtok is called
once again on the first input of the input buffer (stored in file_path). Using "/" as delimiters, we're able to
separate out the final path component (stripped of /bin/ etc). Finally, once all the components are in place,
run_commands is called.

is_redirected:
This is the second major helper function. It takes in the current char * returned by the most recent call to strtok and
then uses strcmp to check if this returned character pointer is equal to <, >, or >>. If so, then the function uses
strcpy to copy the next call to strtok (should be a char * to the file path because it comes after a redirection
symbol) into the corresponding path: input, output, append. The value returned from this function is either
UNCHANGED (4) for non-redirection symbol inputs, REDIR_IN (0) for redirected input, REDIR_OUT (1) for redirected
output not append, REDIR_APPEND (2) for redirected as append, and NOPATH (3) for cases where input is a redirection
symbol but has null follow up path.

run_commands:
This helper function is divided into two parts. The first part makes checks using strcmp to see if the final path
component is a built-in command. If so, then a system call is called accordingly. Otherwise, we use fork() and wait
to create a child process and halt the current process. In the child process, we check to see if the values of
input_path, output_path, or append_path have changed. If so, stdin or stdout are replaced accordingly and execv is
called to execute the non-built in command. In the case of rm, there is a call to the contains_f_flag which checks if
"-f" is contained in the args. If so, then rm will continue regardless of whether or not the file already exists.

is_job_command: 
Called after the input string has been tokenized. The function checks if the first input is bg, fg, or jobs and handles
them appropriately

reap_background: 
Called immediately before the prompt is printed out in the main repl loop. Loops through the jobs list and calls the
reap function to reap the jobs.

reap:
Called within any function that requires the reaping of an already established job. For example, reap is called in the
loop of the reap_background function as well as during instances where fg or bg are called.

reap_foreground:
Called within the run commands function if the current command is not specified to run in the background (&) and fork()
specifies that we're currently in the parent process.

error_exit:
Small helper function to reduce redundant error handling code.


Other: No known bugs, no additional features.