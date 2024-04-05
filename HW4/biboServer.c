#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

typedef struct {
    int client_socket;
    char client_name[20];
} ClientInfo;

char* dirname;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void* handle_client(void* arg) {
    ClientInfo* client_info = (ClientInfo*)arg;
    int client_socket = client_info->client_socket;
    char client_name[20];
    strcpy(client_name, client_info->client_name);
    free(client_info);

    char buffer[BUFFER_SIZE];

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received == -1) {
            perror("recv");
            break;
        }
        else if (bytes_received == 0) {
            printf("Client %s disconnected\n", client_name);
            break;
        }

        // Process client's command
        char response[BUFFER_SIZE];
        if (strncmp(buffer, "help", 4) == 0) {
            sprintf(response, "Available commands are: list, readF, writeT, upload, download, quit, killServer");
        }
        else if (strncmp(buffer, "list", 4) == 0) {
            DIR* dir;
            struct dirent* ent;
            if ((dir = opendir(dirname)) != NULL) {
                strcpy(response, "Files in server directory:\n");
                while ((ent = readdir(dir)) != NULL) {
                    strcat(response, ent->d_name);
                    strcat(response, "\n");
                }
                closedir(dir);
            }
            else {
                strcpy(response, "Error reading directory");
            }
        }
        else if (strncmp(buffer, "readF", 5) == 0) {
            char* filename = strtok(buffer + 6, " ");
            char* line_str = strtok(NULL, " ");
            int line_num = line_str ? atoi(line_str) : 0;

            char filepath[100];
            sprintf(filepath, "%s/%s", dirname, filename);
            FILE* file = fopen(filepath, "r");
            if (file == NULL) {
                sprintf(response, "File not found");
            }
            else {
                char line[256];
                if (line_num == 0) {
                    strcpy(response, "File content:\n");
                    while (fgets(line, sizeof(line), file) != NULL) {
                        strcat(response, line);
                    }
                }
                else {
                    int i = 0;
                    while (fgets(line, sizeof(line), file) != NULL) {
                        i++;
                        if (i == line_num) {
                            strcpy(response, line);
                            break;
                        }
                    }
                    if (i != line_num) {
                        sprintf(response, "Line %d not found", line_num);
                    }
                }
                fclose(file);
            }
        }
        else if (strncmp(buffer, "writeT", 6) == 0) {
            char* filename = strtok(buffer + 7, " ");
            char* line_str = strtok(NULL, " ");
            char* text = strtok(NULL, "");
            int line_num = line_str ? atoi(line_str) : 0;

            char filepath[100];
            sprintf(filepath, "%s/%s", dirname, filename);
            pthread_mutex_lock(&mutex);
            FILE* file = fopen(filepath, "r");
            if (file == NULL) {
                file = fopen(filepath, "w");
                fprintf(file, "%s", text);
            }
            else {
                fclose(file);
                file = fopen(filepath, "r+");
                if (line_num == 0) {
                    fseek(file, 0, SEEK_END);
                    fprintf(file, "%s", text);
                }
                else {
                    char lines[1000][256];
                    int line_count = 0;
                    while (fgets(lines[line_count], sizeof(lines[0]), file) != NULL) {
                        line_count++;
                    }
                    if (line_num <= line_count) {
                        strcpy(lines[line_num - 1], text);
                        fclose(file);
                        file = fopen(filepath, "w");
                        for (int i = 0; i < line_count; i++) {
                            fprintf(file, "%s", lines[i]);
                        }
                    }
                    else {
                        sprintf(response, "Line %d not found", line_num);
                    }
                }
            }
            fclose(file);
            pthread_mutex_unlock(&mutex);
            if (response[0] == '\0') {
                strcpy(response, "Write successful");
            }
        }
        else if (strncmp(buffer, "upload", 6) == 0) {
            char* filename = strtok(buffer + 7, " ");

            char filepath[100];
            sprintf(filepath, "%s/%s", dirname, filename);

            FILE* file = fopen(filepath, "w");
            if (file == NULL) {
                sprintf(response, "Failed to upload file");
            }
            else {
                fclose(file);
                sprintf(response, "File uploaded successfully");
            }
        }
        else if (strncmp(buffer, "download", 8) == 0) {
            char* filename = strtok(buffer + 9, " ");

            char filepath[100];
            sprintf(filepath, "%s/%s", dirname, filename);

            FILE* file = fopen(filepath, "r");
            if (file == NULL) {
                sprintf(response, "File not found");
            }
            else {
                fclose(file);
                sprintf(response, "File downloaded successfully");
            }
        }
        else if (strncmp(buffer, "quit", 4) == 0) {
            strcpy(response, "Quitting...");
            send(client_socket, response, strlen(response), 0);
            break;
        }
        else if (strncmp(buffer, "killServer", 10) == 0) {
            strcpy(response, "Server is shutting down...");
            send(client_socket, response, strlen(response), 0);
            pthread_kill(pthread_self(), SIGINT);
        }
        else {
            strcpy(response, "Invalid command");
        }

        send(client_socket, response, strlen(response), 0);
    }

    close(client_socket);
    pthread_exit(NULL);
}

void* accept_clients(void* arg) {
    int server_socket = *((int*)arg);
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    while (1) {
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket == -1) {
            perror("accept");
            continue;
        }

        char client_name[20];
        sprintf(client_name, "client%d", client_socket);

        printf("Client %s connected\n", client_name);

        ClientInfo* client_info = (ClientInfo*)malloc(sizeof(ClientInfo));
        client_info->client_socket = client_socket;
        strcpy(client_info->client_name, client_name);

        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, (void*)client_info);
        pthread_detach(tid); // Detach the thread to avoid memory leak
    }

    return NULL;
}

void signal_handler(int sig) {
    printf("\nTerminating server...\n");
    pthread_mutex_destroy(&mutex);
    exit(EXIT_SUCCESS);
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        printf("Usage: %s <dirname> <max # of clients> <pool size>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    dirname = argv[1];
    int max_clients = atoi(argv[2]);
    int pool_size = atoi(argv[3]);

    // Create the directory if it doesn't exist
    mkdir(dirname, 0777);

    // Set up signal handler for SIGINT (Ctrl+C)
    signal(SIGINT, signal_handler);

    // Create a server socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind the server socket
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_socket, max_clients) == -1) {
        perror("listen");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Server started. Listening for clients...\n");

    // Create a thread to accept clients
    pthread_t accept_thread;
    pthread_create(&accept_thread, NULL, accept_clients, &server_socket);

    // Main thread waits until the server is terminated
    pthread_join(accept_thread, NULL);

    return 0;
}
