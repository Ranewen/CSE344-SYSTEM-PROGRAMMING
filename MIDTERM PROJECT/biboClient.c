// Client program
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>

#define MAX_LINE 1024 // maximum length of a line in a file or a command
#define MAXNAMLEN 1024
// function prototypes
void handle_signal(int sig); // handle signals from server or user
void connect_server(int pid); // connect to the server queue
void try_connect_server(int pid); // try to connect to the server queue without waiting
void send_request(char *request); // send a request to the server
void receive_response(); // receive a response from the server
void help(); // display the list of possible client requests

// global variables
int connected = 0; // flag to indicate if connected to the server queue
int server_pid; // variable to store the server PID

// main function
int main(int argc, char *argv[]) {
    if (argc != 3) { // if the number of arguments is not 3
        printf("Usage: %s <connect/tryConnect> <server PID>\n", argv[0]); // print usage message
        exit(1); // exit with error code
    }

    int pid = getpid(); // get the client PID

    signal(SIGINT, handle_signal); // handle Ctrl-C signal to terminate gracefully
    signal(SIGUSR1, handle_signal); // handle signal from server to confirm connection
    signal(SIGUSR2, handle_signal); // handle signal from server to reject connection

    if (strcmp(argv[1], "connect") == 0) { // if connect option is given
        connect_server(pid); // connect to the server queue
    }
    else if (strcmp(argv[1], "tryConnect") == 0) { // if tryConnect option is given
        try_connect_server(pid); //
                try_connect_server(pid); // try to connect to the server queue without waiting
    }
    else { // if invalid option is given
        printf("Invalid option. Use connect or tryConnect.\n"); // print error message
        exit(1); // exit with error code
    }

    server_pid = atoi(argv[2]); // convert the server PID argument to integer

    char request[MAX_LINE]; // buffer to store the client request

    while (1) {
        if (connected) { // if connected to the server queue
            printf("Enter comment: "); // prompt the user to enter a request
            fgets(request, MAX_LINE, stdin); // read the request from standard input
            request[strlen(request) - 1] = '\0'; // remove the newline character from the request

            if (strcmp(request, "quit") == 0) { // if quit request is given
                send_request(request); // send the quit request to the server
                receive_response(); // receive the response from the server
                break; // break the loop and exit
            }
            else if (strcmp(request, "killServer") == 0) { // if killServer request is given
                send_request(request); // send the killServer request to the server
                receive_response(); // receive the response from the server
                break; // break the loop and exit
            }
            else { // if any other request is given
                send_request(request); // send the request to the server
                receive_response(); // receive the response from the server
            }
        }
        else { // if not connected to the server queue
            printf("Not connected. Enter connect or tryConnect to connect to server queue.\n"); // print a message to inform the user
            fgets(request, MAX_LINE, stdin); // read the user input from standard input
            request[strlen(request) - 1] = '\0'; // remove the newline character from the input

            if (strcmp(request, "connect") == 0) { // if connect option is given
                connect_server(pid); // connect to the server queue
            }
            else if (strcmp(request, "tryConnect") == 0) { // if tryConnect option is given
                try_connect_server(pid); // try to connect to the server queue without waiting
            }
            else { // if invalid option is given
                printf("Invalid option. Use connect or tryConnect.\n"); // print error message
            }
        }
    }

    return 0;
}

// handle signals from server or user function definition 
void handle_signal(int sig) {
    if (sig == SIGINT) { // if Ctrl-C signal is received 
        printf("Client terminated.\n"); // print a message 
        exit(0); // exit gracefully 
    }
    else if (sig == SIGUSR1) 
	{ // if signal from server is received 
        connected = 1; // set connected flag to true 
        printf("Connection established.\n"); // print a message 
    }
    else if (sig == SIGUSR2) 
	{ // if signal from server is received 
        connected = 0; // set connected flag to false 
        printf("Connection rejected. Queue is full.\n"); //
	}
}
// connect to the server queue function definition 
void connect_server(int pid) {
    kill(server_pid, SIGUSR1); // send a signal to the server to enqueue the client PID
    printf("Waiting for queue...\n"); // print a message
}

// try to connect to the server queue without waiting function definition 
void try_connect_server(int pid) {
    kill(server_pid, SIGUSR1); // send a signal to the server to enqueue the client PID
    printf("Trying to connect...\n"); // print a message
}

// send a request to the server function definition 
void send_request(char *request) {
    char buffer[MAX_LINE]; // buffer to store the request or file content
	pid_t pid = getpid();
    sprintf(buffer, "/tmp/%d", pid); // create a FIFO name based on the client PID
    mkfifo(buffer, 0666); // create a FIFO with read and write permissions
    int fd = open(buffer, O_RDWR); // open the FIFO for reading and writing

    write(fd, request, MAX_LINE); // write the request to the FIFO

    if (sscanf(request, "upload %s", buffer) == 1) { // if upload request is given
        int file_fd = open(buffer, O_RDONLY); // open the file for reading

        if (file_fd < 0) { // if file open error occurs
            perror("open");
            char file_name[64]; // create a separate buffer for the file name
			strncpy(file_name, buffer, MAXNAMLEN); // copy the file name from buffer
			file_name[MAXNAMLEN] = '\0'; // add a null terminator
			snprintf(buffer, sizeof(buffer), "File %s does not exist in client directory\n", file_name); // format an error message
            write(fd, buffer, MAX_LINE); // write the error message to FIFO
            close(fd); // close FIFO
            unlink(buffer); // remove FIFO
            return; // return from the function
        }

        int n; // variable to store the number of bytes read or written

        do {
            n = read(file_fd, buffer, MAX_LINE); // read up to MAX_LINE bytes from file

            if (n > 0) { // if there are bytes read from file
                write(fd, buffer, n); // write bytes to FIFO
            }
        } while (n > 0); // repeat until end of file is reached

        close(file_fd); // close file
    }

    close(fd); // close FIFO
}

// receive a response from the server function definition 
void receive_response() {
    char buffer[MAX_LINE]; // buffer to store the response or file content
	pid_t pid = getpid();
    sprintf(buffer, "/tmp/%d", pid); // create a FIFO name based on the client PID
    mkfifo(buffer, 0666); // create a FIFO with read and write permissions
    int fd = open(buffer, O_RDWR); // open the FIFO for reading and writing

    int n; // variable to store the number of bytes read or written

    do {
        n = read(fd, buffer, MAX_LINE); // read up to MAX_LINE bytes from FIFO

        if (n > 0) { // if there are bytes read from FIFO
            write(1, buffer, n); // write bytes to standard output
        }
    } while (n > 0); // repeat until end of FIFO is reached

    close(fd); // close FIFO
}

// display the list of possible client requests function definition 
void help() {
    printf("Available commands are: help, list, readF, writeT, upload, download, quit, killServer\n"); // print the list of commands
}
