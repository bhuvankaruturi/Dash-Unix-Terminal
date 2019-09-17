#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

// flush the file stream and reset to default std streams
void flushstream(int fd_out, int fd_err, int save_out, int save_err) {
	fflush(stdout); close(fd_out);
	fflush(stderr); close(fd_err);

	dup2(save_out, fileno(stdout));
	dup2(save_err, fileno(stderr));
	
	close(save_out);
	close(save_err);
}

void display_header() {
	// header on launch - begin
	printf("\n");
	printf("############################################\n");
	printf("##                                        ##\n");
	printf("##                                        ##\n");
	printf("##        **     DAllas SHell     **      ##\n");
	printf("##                                        ##\n");
	printf("##                                        ##\n");
	printf("############################################\n");
	printf("\n");
	// header on launch - end
}

// function to write error message to stderr
void write_error(char* error_message) {
	write(fileno(stderr), error_message, strlen(error_message));
}

//begin execution
int main(int argc, char *argv[]) {
	
	// buffer to read in the input
	char *buffer;
	size_t bufsize = 100; //buffer size for getline 
	int characters = 0;

	// searchpaths
	uint max_paths = 10;
	char *searchpath[max_paths];
	searchpath[0] = "";
	uint initial_paths = 1;
	uint num_paths = initial_paths;

	// process return code
	pid_t proc_rc = -1;

	// standard error for the shell
	char error_message[30] = "An error has occurred\n";
	FILE *fp;
	
	// display header of the shell
	display_header();

	buffer = (char *)malloc(bufsize * sizeof(char));
	// exit if dash was called with more than one argument
	if (argc > 2) {
		write_error(error_message);		
		exit(1); // exit with a error message
	}	
	// if a filename is provided, open the file
	if (argc == 2) { 
		fp = fopen(argv[1], "r");
		if (!fp) {
			write_error(error_message);		
			exit(1); // exit with a error message
		} 
	}
	// loop until exit is entered or the end of file is encountered
	while(strcmp(buffer, "exit") != 0 && (characters >= 0 || argc == 1)) {
		// save stdout and stderr streams
		int save_out = dup(fileno(stdout));
		int save_err = dup(fileno(stderr));
		// interactive mode
		if (argc == 1) {
			// display CLI prompt
			printf("dash> ");
			characters = getline(&buffer, &bufsize, stdin);
		}
		// batch mode
		if (argc == 2) {
			characters = getline(&buffer, &bufsize, fp);
			if (characters < 0) { exit(0); }
			// use write method so that we can have consistent behavior while redirecting to file
			write(fileno(stdout), "dash> ", 7);
			write(fileno(stdout), buffer, strlen(buffer));
		}	
		// strip the input string of new line character
		if ((buffer)[characters-1] == '\n') {
			(buffer)[characters-1] = '\0';
			characters--;
		}
		// Handle parallel commands - Start
		char* par_token;
		char* par_rest = strdup(buffer);
		char* par_commands[10];
		int par_i = 0;
		// split the commands based on & symbol
		while((par_token = strtok_r(par_rest, "&", &par_rest)) && par_i < 10){
			par_commands[par_i] = strdup(par_token);	
			// split each parallel command string into tokens, with space as delimiter
			char* token;
			char* command;
			char* rest = strdup(par_commands[par_i]);
			int i = 0; 
			int redirection_idx = -1;
			bool parse_error = false;
			char *args[10];
			// set executed to false for the current command
			bool executed = false;
			while((token = strtok_r(rest, " \t", &rest)) && i < 10) {
				if (i==0) {
					command = strdup(token);
				}
				// check for redirection character in between other characters
				// example : ls>filename
				char* redir; 
				char redirection_op = '>';
				redir = strchr(token, redirection_op);
				if (redir && strcmp(redir, ">")!=0) {
					char* redir_r;
					redir_r = strrchr(token, redirection_op);
					// multiple redirections
					if (strcmp(redir, redir_r) != 0) {
						parse_error = true;
						break;
					} else {
						char* temp_token = strdup(token);
						strtok(temp_token, ">");
						// string before the redirection character
						args[i] = strdup(temp_token);
						if (i == 0) {
							command = strdup(temp_token);
						}
						i++;
						if (redirection_idx == -1) {
							redirection_idx = i;
						} else {
							// multiple redirections
							parse_error = true;
							break;
						}
						// string after the redirection character
						args[i] = redir + 1;
						i++;
						// since args are already set for this token continue
						continue;
					}
				}
				// check for standlone redirection character in the token end
				if (strcmp(token, ">")==0) {
					if (redirection_idx == -1) {
						redirection_idx = i;
						continue;
					} else {
						// multiple redirection operators
						parse_error = true;
						break;
					}
				}
				// if there are mutliple filenames after redirection operator break
				if (redirection_idx != -1 && i > redirection_idx + 1) {
					parse_error = true;
					break;
				}
				args[i] = strdup(token);
				i++;
			}
			// continue to next command if there was error parsing the current command
			if (parse_error) {
				write_error(error_message);
				continue;
			}
			// set the element after all the input tokens to NULL
			// to indicate the end of input tokens
			args[i] = NULL;

			// handle builtin functions - begin
			// path command
			if (strcmp(command, "path") == 0) {
				executed = true;
				// throw error if no paths where specified
				if (args[1] == NULL) { 
					write_error(error_message);
					break;
				}
				// user can use -o option to overwrite existing path
				// except the empty path
				if (strcmp(args[1], "-o") == 0) {
					num_paths = initial_paths;
				}
				for (i = 0; i < sizeof(args)/sizeof(args[0]); i++) {
					if (num_paths >= max_paths) { break; } // if used up all available space for searchpaths break
					if (args[i] == NULL) { break; } // end of paths break
					if (i == 0 || strcmp(args[i],"-o")==0) { continue; } // if command argument skip
					else {
						searchpath[num_paths] = malloc(strlen(args[i]) + 1);
						searchpath[num_paths] = strdup(args[i]);
						num_paths++;
					}
				}
			}
			// exit command
			if (strcmp(command, "exit") == 0) { 
				executed = true;
				exit(0); 
			}
			// cd command
			if (strcmp(command, "cd") == 0) {
				executed = true;
				if (args[1] == NULL) { //if cd was not given a path as argument it an error
					write_error(error_message);
					continue;
				}
				// check if more than two args where given to cd command
				for(i = 0; i < sizeof(args)/sizeof(args[0]); i++) {
					if (args[i] == NULL) { break; }
					if (i > 1) { // if number of arguments to cd command are more than 1 break	
						write_error(error_message);
						break;
					}
				}
				// continue if the args to cd are > 1 and args[i] is not null
				if (i > 1 && args[i] != NULL) { continue; }
				else {	
					int rc = chdir(args[1]); // execute cd command
					if (rc == -1) { // if chdir fails display error and continue
						write_error(error_message);
					}
				}
			}
			// handle builtin functions - end
	
			// external commands-begin
			// if the command is not a built-in command, create a child process and execute it
			if (!executed) {
				char *path;	
				bool pathFound = false;
				//handle redirection//
				int fd_out;
				int fd_err;
				// open the file specified if there is a redirection in the input command
				// set the stdout and stderr streams to the opened file descriptor
				if (redirection_idx != -1) {
					fd_out = open(args[redirection_idx], O_CREAT | O_WRONLY | O_TRUNC, 0777);
					fd_err = dup(fd_out);
					if (fd_out == -1) { write_error(error_message); }	
					if (dup2(fd_out, fileno(stdout))== -1) { write_error(error_message); }
					if (dup2(fd_err, fileno(stderr))== -1) { write_error(error_message); }
					args[redirection_idx] = NULL;
				}
				// search for the executable file in all the searchpaths
				for (i = 0; i < num_paths; i++){	
					path = (char*) malloc(strlen(searchpath[i]) + strlen(command) + strlen("/") + 1);
					path = strdup(searchpath[i]);
					// append "/" only when the searchpath is not empty
					// useful when user enters the complete path along with command
					if(strcmp(searchpath[i], "") != 0) { 
						strcat(path, "/"); 
					}
					strcat(path, command);
					// check if file is present in path and that it is a executable
					if (access(path, X_OK)==0) { 
						pathFound = true;
						break;
					}
				}
				// if the path of the executable is not found,
				// print error message and skip to next input
				if(!pathFound) {
					write_error(error_message);
					if(redirection_idx != -1) { flushstream(fd_out, fd_out, save_out, save_err);}
					continue;
				}
				args[0] = strdup(path);
				proc_rc = fork();
				// child process
				if (proc_rc == 0) {
					// use execv to execute commands with child process rc = 0
					execv(args[0], args);
					// if execv returned, it means that it failed
					write_error(error_message);
					// exit manually if execv failed to avoid forkbomb
					exit(0);
				}
				else {
					// incase of redirection wait for the child to terminate 
					// then reset the output and error streams to stderr and stdout
					if (redirection_idx != -1) {
						int status;
						waitpid(proc_rc, &status, WUNTRACED);
						// if the child has terminated then flush the stream 
						// and reset the stdout and stderr back to save_out and save_err
						if (WIFEXITED(status)) {
							flushstream(fd_out, fd_err, save_out, save_err);
						}
					}
				}
			} // handle external commands - end
			par_i++; // increment parallel command counter
		} // handle parallel commands - end
		if (proc_rc > 0) { 
			int status;
			pid_t wpid;
			// wait for all the child processes to terminate
			while((wpid = wait(&status)) > 0);
		}
	} // end of loop to get input
	return 0;
} // end of main
