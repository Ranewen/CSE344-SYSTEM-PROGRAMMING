#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024

void print_help() {
    printf("Available commands are:\n");
    printf("help\n");
    printf("list\n");
    printf("readF <file> <line #>\n");
    printf("writeT <file> <line #> <string>\n");
    printf("upload <file>\n");
    printf("download <file>\n");
    printf("quit\n");
    printf("killServer\n");
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Usage: %s <Connect/tryConnect> <ServerPID>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char* connect_option = argv[1];
    int server_pid = atoi(argv[2]);

    // Connect to server
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("connect");
        close(client_socket);
        exit(EXIT_FAILURE);
    }

    // Send server_pid to server
    send(client_socket, &server_pid, sizeof(server_pid), 0);

    // Handle user commands
    char command[BUFFER_SIZE];
    while (1) {
        printf(">> Enter command: ");
        fgets(command, BUFFER_SIZE, stdin);
        command[strcspn(command, "\n")] = '\0'; // Remove newline character

        // Send command to server
        send(client_socket, command, strlen(command), 0);

        // Receive response from server
        char response[BUFFER_SIZE];
        int bytes_received = recv(client_socket, response, BUFFER_SIZE, 0);
        if (bytes_received == -1) {
            perror("recv");
            break;
        }
        else if (bytes_received == 0) {
            printf("Server disconnected\n");
            break;
        }

        printf("Response from server: %s\n", response);

        // Special handling for help command
        if (strncmp(command, "help", 4) == 0) {
            print_help();
        }

        // Special handling for quit and killServer commands
        if (strncmp(command, "quit", 4) == 0 || strncmp(command, "killServer", 10) == 0) {
            break;
        }
    }

    close(client_socket);

    return 0;
}
