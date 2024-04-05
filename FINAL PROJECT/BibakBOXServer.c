#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <arpa/inet.h>
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10

int threadPoolSize;
char* directory;
int serverSocket;
pthread_mutex_t logMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
int num_clients = 0;

// Struct to hold information about a file
typedef struct 
{
    char name[BUFFER_SIZE];
    time_t accessTime;
} FileEntry;

// Function to write log entry to the client's logfile
void writeLog(const char* clientDir, const char* message) 
{
    char logFile[BUFFER_SIZE];
    sprintf(logFile, "%s/logfile.txt", clientDir);

    FILE* log = fopen(logFile, "a");
    if (log == NULL) 
	{
        perror("Error opening logfile");
        return;
    }

    time_t now;
    time(&now);
    char* timestamp = ctime(&now);
    timestamp[strlen(timestamp) - 1] = '\0'; // Remove newline character from ctime result

    pthread_mutex_lock(&logMutex);
    fprintf(log, "[%s] %s\n", timestamp, message);
    pthread_mutex_unlock(&logMutex);

    fclose(log);
}

// Function to synchronize directory contents with the client
void synchronizeDirectory(int clientSocket, const char* clientDir) 
{
    char buffer[BUFFER_SIZE];

    DIR* dir = opendir(clientDir);
    if (dir == NULL) 
	{
        perror("Error opening directory");
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) 
	{
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) 
		{
            strcpy(buffer, entry->d_name);
            send(clientSocket, buffer, strlen(buffer), 0);
            memset(buffer, 0, sizeof(buffer));
        }
    }

    closedir(dir);
}

// Function to handle a client connection
void* handleClient(void* arg) 
{
    int clientSocket = *((int*)arg);
    char buffer[BUFFER_SIZE];

    // Receive client directory name
    ssize_t numBytes = recv(clientSocket, buffer, sizeof(buffer), 0);
    if (numBytes <= 0) 
	{
        close(clientSocket);
        return NULL;
    }

    buffer[numBytes] = '\0';
    char clientDir[1028];
   	snprintf(clientDir, sizeof(clientDir), "%s/%s", directory, buffer);

    // Synchronize directory with client
    synchronizeDirectory(clientSocket, clientDir);

    printf("Client connected: %s\n", buffer);

    while (1) 
	{
        // Receive client requests
        numBytes = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (numBytes <= 0) 
		{
            break;
        }

        // Handle file operations
        buffer[numBytes] = '\0';
        char* operation = strtok(buffer, ":");
        char* filename = strtok(NULL, ":");

        if (strcmp(operation, "upload") == 0) 
		{
            FILE* file = fopen(filename, "wb");
            if (file == NULL) 
			{
                perror("Error opening file");
                continue;
            }

            while ((numBytes = recv(clientSocket, buffer, sizeof(buffer), 0)) > 0) 
			{
                fwrite(buffer, 1, numBytes, file);
            }

            fclose(file);
            printf("File uploaded: %s\n", filename);
            writeLog(clientDir, filename);
        } 
		else if (strcmp(operation, "delete") == 0) 
		{
            char filePath[1200];
            sprintf(filePath, "%s/%s", clientDir, filename);

            if (remove(filePath) == 0) 
			{
                printf("File deleted: %s\n", filename);
                writeLog(clientDir, filename);
            } else 
			{
                perror("Error deleting file");
            }
        } 
		else if (strcmp(operation, "update") == 0) 
		{
            // Currently, updating a file means deleting the old file and uploading the new one
            char filePath[1200];
            sprintf(filePath, "%s/%s", clientDir, filename);

            if (remove(filePath) == 0) 
			{
                FILE* file = fopen(filename, "wb");
                if (file == NULL) 
				{
                    perror("Error opening file");
                    continue;
                }

                while ((numBytes = recv(clientSocket, buffer, sizeof(buffer), 0)) > 0) 
				{
                    fwrite(buffer, 1, numBytes, file);
                }

                fclose(file);
                printf("File updated: %s\n", filename);
                writeLog(clientDir, filename);
            } 
			else 
			{
                perror("Error updating file");
            }
        }
    }

    printf("Client disconnected: %s\n", buffer);
  
    
    pthread_mutex_unlock(&client_mutex);
    close(clientSocket);
    free(arg);
    return NULL;
}

// Signal handler for SIGINT
void handleSIGINT(int signum) 
{
    printf("\nTerminating server...\n");
    
 	// Close the server socket
    close(serverSocket);

    // Wait for all client threads to finish
    pthread_mutex_lock(&logMutex);
    printf("Waiting for all client threads to finish...\n");
    pthread_mutex_unlock(&logMutex);

    pthread_exit(NULL);
    exit(EXIT_SUCCESS);
}

int main(int argc, char* argv[]) 
{
    if (argc != 4) 
	{
        fprintf(stderr, "Usage: %s [directory] [threadPoolSize] [portnumber]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    directory = argv[1];
    threadPoolSize = atoi(argv[2]);
    int portNumber = atoi(argv[3]);

    // Create server socket
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) 
	{
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(portNumber);

    // Bind the socket to the specified port
    if (bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == -1) 
	{
        perror("Error binding socket");
        exit(EXIT_FAILURE);
    }

    // Listen for client connections
    if (listen(serverSocket, MAX_CLIENTS) == -1) 
	{
        perror("Error listening for connections");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", portNumber);

    // Set up signal handler for SIGINT
    struct sigaction sigint_action;
    sigint_action.sa_handler = handleSIGINT;
    sigemptyset(&sigint_action.sa_mask);
    sigint_action.sa_flags = 0;
    sigaction(SIGINT, &sigint_action, NULL);

    // Create thread pool
    pthread_t threadPool[threadPoolSize];
    int threadIndex = 0;

    while (1) 
	{
        // Accept client connection
        struct sockaddr_in clientAddress;
        socklen_t clientAddressLength = sizeof(clientAddress);
        int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddress, &clientAddressLength);
        
        if (clientSocket == -1) 
		{
            perror("Error accepting client connection");
            continue;
        }

        // Create a new thread to handle the client
        int* newClientSocket = malloc(sizeof(int));
        *newClientSocket = clientSocket;
        if (pthread_create(&threadPool[threadIndex], NULL, handleClient, newClientSocket) != 0) 
		{
            perror("Error creating thread");
            close(clientSocket);
            free(newClientSocket);
        }

        // Increment thread index and wrap around if necessary
        threadIndex = (threadIndex + 1) % threadPoolSize;
        
        // Print client connection information
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(clientAddress.sin_addr), client_ip, INET_ADDRSTRLEN);
        printf("Client connected: %s:%d\n", client_ip, ntohs(clientAddress.sin_port));
    }

    return 0;
}

