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
#include <sys/stat.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024

char* serverIP;
int serverPort;
char* clientDir;

// Function to synchronize directory contents with the server
void synchronizeDirectory(int serverSocket) 
{
    char buffer[BUFFER_SIZE];

    DIR* dir = opendir(clientDir);
    if (dir == NULL) {
        perror("Error opening directory");
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL)
	{
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
		{
            strcpy(buffer, entry->d_name);
            send(serverSocket, buffer, strlen(buffer), 0);
            memset(buffer, 0, sizeof(buffer));
        }
    }

    closedir(dir);
}

// Function to send file to the server
void sendFile(int serverSocket, const char* filePath) 
{
    char buffer[BUFFER_SIZE];

    FILE* file = fopen(filePath, "rb");
    if (file == NULL) 
	{
        perror("Error opening file");
        return;
    }

    send(serverSocket, "upload:", strlen("upload:"), 0);
    send(serverSocket, filePath, strlen(filePath), 0);

    while (1) 
	{
        size_t bytesRead = fread(buffer, 1, sizeof(buffer), file);
        if (bytesRead > 0) 
		{
            send(serverSocket, buffer, bytesRead, 0);
        }

        if (bytesRead < sizeof(buffer)) 
		{
            if (feof(file)) {
                break;
            } else if (ferror(file)) {
                perror("Error reading from file");
                break;
            }
        }
    }

    fclose(file);
}

// Function to handle directory synchronization
void* synchronize(void* arg) 
{
    int serverSocket = *((int*)arg);
    char buffer[BUFFER_SIZE];

    // Send client directory name to the server
    send(serverSocket, clientDir, strlen(clientDir), 0);

    // Synchronize directory with server
    synchronizeDirectory(serverSocket);

    while (1) 
	{
        // Monitor local directory for changes
        DIR* dir = opendir(clientDir);
        if (dir == NULL) 
		{
            perror("Error opening directory");
            break;
        }

        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) 
		{
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                char filePath[BUFFER_SIZE];
                sprintf(filePath, "%s/%s", clientDir, entry->d_name);

                // Check if the file exists on the server
                send(serverSocket, "check:", strlen("check:"), 0);
                send(serverSocket, entry->d_name, strlen(entry->d_name), 0);

                ssize_t numBytes = recv(serverSocket, buffer, sizeof(buffer), 0);
                if (numBytes <= 0) 
				{
                    perror("Error receiving data from server");
                    break;
                }

                buffer[numBytes] = '\0';

                if (strcmp(buffer, "exists") != 0) 
				{
                    // File doesn't exist on the server, so send it
                    sendFile(serverSocket, filePath);
                } 
				else 
				{
                    // File exists on the server, check if it's up to date
                    send(serverSocket, "timestamp:", strlen("timestamp:"), 0);
                    send(serverSocket, entry->d_name, strlen(entry->d_name), 0);

                    numBytes = recv(serverSocket, buffer, sizeof(buffer), 0);
                    if (numBytes <= 0) 
					{
                        perror("Error receiving data from server");
                        break;
                    }

                    buffer[numBytes] = '\0';

                    long clientTimestamp = strtol(buffer, NULL, 10);
                    struct stat fileStat;
                    stat(filePath, &fileStat);
                    long serverTimestamp = fileStat.st_mtime;

                    if (clientTimestamp > serverTimestamp) 
					{
                        // Client file is newer, so send it
                        sendFile(serverSocket, filePath);
                    }
                }
            }
        }

        closedir(dir);

        sleep(5); // Check for changes every 5 seconds
    }

    close(serverSocket);
    free(arg);
    return NULL;
}

// Signal handler for SIGINT
void handleSIGINT(int signum) 
{
    printf("\nTerminating client...\n");
    exit(EXIT_SUCCESS);
}

int main(int argc, char* argv[])
{
	if (argc != 3) 
	{
    	fprintf(stderr, "Usage: %s [dirName] [portnumber]\n", argv[0]);
    	exit(EXIT_FAILURE);
	}

	clientDir = argv[1];
	serverPort = atoi(argv[2]);


    // Set up SIGINT signal handler
    signal(SIGINT, handleSIGINT);

    // Create client socket
    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == -1) 
	{
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in serverAddress;
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons(serverPort);

    if (inet_pton(AF_INET, "127.0.0.1", &(serverAddress.sin_addr)) <= 0)
	{
        perror("Invalid server address");
        exit(EXIT_FAILURE);
    }

    // Connect to the server
    if (connect(clientSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == -1) 
	{
        perror("Error connecting to server");
        exit(EXIT_FAILURE);
    }

    // Synchronize directory with server in a separate thread
    pthread_t syncThread;
    int* syncSocket = malloc(sizeof(int));
    *syncSocket = clientSocket;

    if (pthread_create(&syncThread, NULL, synchronize, syncSocket) != 0) 
	{
        perror("Error creating thread");
        close(clientSocket);
        free(syncSocket);
        exit(EXIT_FAILURE);
    }

    // Main loop to handle user input
    char buffer[BUFFER_SIZE];

    while (1) 
	{
        printf("Enter command (upload, delete, update, exit): ");
        fgets(buffer, sizeof(buffer), stdin);

        // Remove newline character
        buffer[strcspn(buffer, "\n")] = '\0';

        if (strcmp(buffer, "upload") == 0) {
            printf("Enter filename to upload: ");
            fgets(buffer, sizeof(buffer), stdin);
            buffer[strcspn(buffer, "\n")] = '\0';

            send(clientSocket, "upload:", strlen("upload:"), 0);
            send(clientSocket, buffer, strlen(buffer), 0);

            FILE* file = fopen(buffer, "rb");
            if (file == NULL) {
                perror("Error opening file");
                continue;
            }

            while (1) 
			{
                size_t bytesRead = fread(buffer, 1, sizeof(buffer), file);
                if (bytesRead > 0) {
                    send(clientSocket, buffer, bytesRead, 0);
                }

                if (bytesRead < sizeof(buffer)) 
				{
                    if (feof(file)) 
					{
                        break;
                    } else if (ferror(file)) 
					{
                        perror("Error reading from file");
                        break;
                    }
                }
            }

            fclose(file);
        } 
		else if (strcmp(buffer, "delete") == 0) 
		{
            printf("Enter filename to delete: ");
            fgets(buffer, sizeof(buffer), stdin);
            buffer[strcspn(buffer, "\n")] = '\0';

            send(clientSocket, "delete:", strlen("delete:"), 0);
            send(clientSocket, buffer, strlen(buffer), 0);
        } 
		else if (strcmp(buffer, "update") == 0) 
		{
            printf("Enter filename to update: ");
            fgets(buffer, sizeof(buffer), stdin);
            buffer[strcspn(buffer, "\n")] = '\0';

            send(clientSocket, "update:", strlen("update:"), 0);
            send(clientSocket, buffer, strlen(buffer), 0);

            FILE* file = fopen(buffer, "rb");
            
			if (file == NULL) 
			{
                perror("Error opening file");
                continue;
            }

            while (1) 
			{
                size_t bytesRead = fread(buffer, 1, sizeof(buffer), file);
                if (bytesRead > 0) {
                    send(clientSocket, buffer, bytesRead, 0);
                }

                if (bytesRead < sizeof(buffer)) {
                    if (feof(file)) {
                        break;
                    } else if (ferror(file)) {
                        perror("Error reading from file");
                        break;
                    }
                }
            }

            fclose(file);
        } 
		else if (strcmp(buffer, "exit") == 0) {
            break;
        } 
		else 
		{
            printf("Invalid command\n");
        }
    }

    // Terminate synchronization thread
    pthread_cancel(syncThread);
    pthread_join(syncThread, NULL);

    close(clientSocket);
    free(syncSocket);

    return 0;
}

