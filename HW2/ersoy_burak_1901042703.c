#include <stdio.h>    // standard input/output functions
#include <stdlib.h>   // standard library functions
#include <string.h>   // string manipulation functions
#include <unistd.h>   // standard symbolic constants and types
#include <fcntl.h>    // file control options
#include <time.h>     // time functions
#include <signal.h>   // signal handling functions
#include <sys/stat.h> // file status functions
#include <sys/types.h>// basic system data types
#include <sys/wait.h> // wait for process termination

// Define constants for the maximum number of commands and command length
#define MAX_COMMANDS 20
#define MAX_COMMAND_LENGTH 1024
#define MAX_CHILDREN 20
pid_t child_pids[MAX_CHILDREN];
int num_children = 0;

void execute_command(char **command_tokens, int input_fd, int output_fd);
void signal_handler(int signal);


int main() 
{
	// Set up signal handlers for interrupt and termination signals
	signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGQUIT, signal_handler);

    // Initialize variables
    char input[MAX_COMMAND_LENGTH];
    char *command_tokens[MAX_COMMANDS];
    int num_commands = 0;
    int input_fd = STDIN_FILENO;
    int output_fd = STDOUT_FILENO;
    pid_t child_pids[MAX_COMMANDS];
    int num_children = 0;
   

    while (1) {
        // Print prompt and read input
        printf("$ ");
        fgets(input, MAX_COMMAND_LENGTH, stdin);

        // Remove newline character from input
        input[strcspn(input, "\n")] = '\0';

        // Tokenize input by space
        char *token = strtok(input, " ");
        
        while (token != NULL) 
		{
            // Check for redirection or pipe characters
            if (strcmp(token, ">") == 0) 
			{
                // Get output file name and open file for writing
                token = strtok(NULL, " ");
                output_fd = open(token, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                token = strtok(NULL, " ");
                num_children++;
            } 
			else if (strcmp(token, "<") == 0) 
			{
                // Get input file name and open file for reading
                token = strtok(NULL, " ");
                input_fd = open(token, O_RDONLY, 0666);
                token = strtok(NULL, " ");
                num_children++;
            } 
			else if (strcmp(token, "|") == 0) 
			{
                // Execute command and set input and output for next command
                command_tokens[num_commands] = NULL;
                execute_command(command_tokens, input_fd, output_fd);
                input_fd = STDIN_FILENO;
                output_fd = STDOUT_FILENO;
                num_commands = 0;
                token = strtok(NULL, " ");
                num_children++;
            } 
			else 
			{
                // Add token to command array and increment command count
                command_tokens[num_commands++] = token;
                token = strtok(NULL, " ");
                num_children++;
            }
            
        }
        // Check for exit command
        if (strcmp(input, ":q") == 0) 
		{
           	// Create log file with timestamp
            time_t t = time(NULL);
            struct tm *tm = localtime(&t);
            char filename[MAX_COMMAND_LENGTH];
            strftime(filename, sizeof(filename), "%Y-%m-%d_%H-%M-%S.log", tm);
            FILE *log_file = fopen(filename, "w");
            for (int i = 0; i < num_commands; i++) 
			{
                fprintf(log_file, "pid: %d, command: %s\n", child_pids[i], command_tokens[i]);
            }
             // Wait for child processes to terminate
            for (int i = 0; i < num_children; i++) 
			{
                waitpid(child_pids[i], NULL, 0);
            }
           
            exit(0);
        }
        // Execute final command
        command_tokens[num_commands] = NULL;
        execute_command(command_tokens, input_fd, output_fd);
        input_fd = STDIN_FILENO;
        output_fd = STDOUT_FILENO;
        num_commands = 0;

        // Reset child pid array and count
        memset(child_pids, 0, sizeof(child_pids));
    }

    return 0;
}
// Function to execute a single command
void execute_command(char **command_tokens, int input_fd, int output_fd) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork failed");
        exit(1);
    } else if (pid == 0) {
        // Child process
        // Redirect input and output file descriptors if necessary
        if (input_fd != STDIN_FILENO) {
            dup2(input_fd, STDIN_FILENO);
            close(input_fd);
        }
        if (output_fd != STDOUT_FILENO) {
            dup2(output_fd, STDOUT_FILENO);
            close(output_fd);
        }
        // Execute command
        execvp(command_tokens[0], command_tokens);
        // If execvp returns, an error has occurred
        perror("execvp failed");
        exit(1);
    } else {
        // Parent process
        // Add child pid to array and increment count
        child_pids[num_children++] = pid;
        // Wait for child to terminate
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            perror("waitpid failed");
            exit(1);
        }
        // Print error message if child exited with non-zero status
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            fprintf(stderr, "Command failed with exit status %d\n", WEXITSTATUS(status));
        }
    }
}

// Signal handler function to handle interrupt signals
void signal_handler(int signal)
{
    printf("Received signal %d\n", signal);
}
