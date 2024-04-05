// biboServer program
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <dirent.h>
#include <semaphore.h>

#define MAX_CLIENTS 10 // maximum number of clients in the queue
#define MAX_FILES 100 // maximum number of files in the server directory
#define MAX_LINE 1024 // maximum length of a line in a file
#define LOG_FILE "log.txt" // name of the log file

// structure for a file entry
typedef struct {
    char name[256]; // file name
    int size; // file size in bytes
    int fd; // file descriptor
    int readers; // number of readers accessing the file
    int writers; // number of writers accessing the file
} file_entry;

// structure for a shared memory segment
typedef struct {
    int queue[MAX_CLIENTS]; // queue of client PIDs
    int front; // front index of the queue
    int rear; // rear index of the queue
    int count; // number of clients in the queue
    file_entry files[MAX_FILES]; // array of file entries
    int num_files; // number of files in the server directory
} shm_segment;

// global variables
int shmid; // shared memory ID
shm_segment *shm; // pointer to the shared memory segment
int semid; // semaphore ID

// function prototypes
void init_shm(); // initialize the shared memory segment and semaphore
void cleanup_shm(); // detach and remove the shared memory segment and semaphore
void handle_signal(int sig); // handle signals from clients or children
void enqueue(int pid); // enqueue a client PID to the queue
int dequeue(); // dequeue a client PID from the queue
int is_empty(); // check if the queue is empty
int is_full(); // check if the queue is full
void list_files(); // list the files in the server directory and update the shared memory segment
void handle_client(int pid); // handle a client request in a child process
void read_file(char *file, int line); // read a line or the whole file and send it to the client
void write_file(char *file, int line, char *string); // write a string to a line or the end of the file and send a confirmation to the client
void upload_file(char *file); // receive a file from the client and save it to the server directory and send a confirmation to the client
void download_file(char *file); // send a file from the server directory to the client and send a confirmation to the client
void kill_server(); // kill the server process and all its children

// main function
int main() {
    printf("Server Started PID: %d\n", getpid());
    init_shm(); // initialize the shared memory segment and semaphore
	int fd;
    signal(SIGINT, handle_signal); // handle Ctrl-C signal to terminate gracefully
    signal(SIGUSR1, handle_signal); // handle signal from clients to enqueue their PIDs

    list_files(); // list the files in the server directory and update the shared memory segment

    while (1) {
        if (!is_empty()) { // if there are clients in the queue
            int pid = dequeue(); // get the first client PID from the queue

            int child = fork(); // create a child process to handle the client request

            if (child == 0) { // child process code
                handle_client(pid); // handle the client request
                exit(0); // exit after handling the request
            }
            else if (child > 0) { // parent process code
                signal(SIGCHLD, handle_signal); // handle signal from children to reap them when they terminate
            }
            else { // fork error code
                perror("fork");
            }
        }
        else { // if there are no clients in the queue, sleep for 1 second and check again later 
            sleep(1);
        }
    }

    return 0;
}

// initialize a shared memory segment and a semaphore function definition 
void init_shm() {
	sem_t *sem;
    key_t key = ftok("server.c", 1); // generate a key for IPC

    shmid = shmget(key, sizeof(shm_segment), IPC_CREAT | 0666); // create or get a shared memory segment using shmget()
    if (shmid == -1) { // if shmget() fails
        perror("shmget"); // print an error message
        exit(1); // exit with failure
    }

    shm = (shm_segment *)shmat(shmid, NULL, 0); // attach the shared memory segment to the global pointer shm using shmat()
    if (shm == (void *)-1) { // if shmat() fails
        perror("shmat"); // print an error message
        exit(1); // exit with failure
    }

    sem = (sem_t *)shm; // declare a sem_t inside the shared memory segment
    void *usableSharedMemory = (char *)shm + sizeof(sem_t); // get the usable shared memory after the semaphore

    if (sem_init(sem, 1, 1) == -1) { // initialize the semaphore value to 1 using sem_init()
        perror("sem_init"); // print an error message
        exit(1); // exit with failure
    }
}

void cleanup_shm() {
    shmdt(shm); // detach the shared memory segment
    shmctl(shmid, IPC_RMID, NULL); // remove the shared memory segment
    semctl(semid, 0, IPC_RMID); // remove the semaphore
}

// handle signals from clients or children function definition
void handle_signal(int sig) {
    if (sig == SIGINT) { // if Ctrl-C signal is received
        printf("Server terminated.\n");
        cleanup_shm(); // detach and remove the shared memory segment and semaphore
        kill(0, SIGKILL); // kill all child processes
        exit(0); // exit gracefully
    }
    else if (sig == SIGUSR1) { // if signal from a client is received
        int pid = waitpid(-1, NULL, WNOHANG); // get the PID of the client that sent the signal
        enqueue(pid); // enqueue the client PID to the queue
    }
    else if (sig == SIGCHLD) { // if signal from a child is received
        int pid = waitpid(-1, NULL, WNOHANG); // get the PID of the child that terminated
        printf("Child process %d terminated.\n", pid);
    }
}

// enqueue a client PID to the queue function definition
void enqueue(int pid) {
    struct sembuf sb; // semaphore operation structure

    sb.sem_num = 0; // semaphore number
    sb.sem_op = -1; // semaphore operation (decrement)
    sb.sem_flg = 0; // semaphore flag

    semop(semid, &sb, 1); // lock the semaphore

    if (!is_full()) { // if the queue is not full
        shm->queue[shm->rear] = pid; // add the PID to the rear of the queue
        shm->rear = (shm->rear + 1) % MAX_CLIENTS; // update the rear index of the queue
        shm->count++; // increment the number of clients in the queue
        printf("Client %d enqueued.\n", pid);
    }
    else { // if the queue is full
        printf("Queue is full. Client %d rejected.\n", pid);
        kill(pid, SIGUSR2); // send a signal to the client to inform that it is rejected
    }

    sb.sem_op = 1; // semaphore operation (increment)
    semop(semid, &sb, 1); // unlock the semaphore
}

// dequeue a client PID from the queue function definition 
int dequeue() {
    struct sembuf sb; // semaphore operation structure

    sb.sem_num = 0; // semaphore number
    sb.sem_op = -1; // semaphore operation (decrement)
    sb.sem_flg = 0; // semaphore flag

    semop(semid, &sb, 1); // lock the semaphore

    int pid = -1; // variable to store the dequeued PID

    if (!is_empty()) { // if the queue is not empty 
        pid = shm->queue[shm->front]; // get the PID from the front of the queue 
        shm->front = (shm->front + 1) % MAX_CLIENTS; // update the front index of the queue 
        shm->count--; // decrement the number of clients in the queue 
        printf("Client %d dequeued.\n", pid);
    }
    
    sb.sem_op = 1; // semaphore operation (increment)
    semop(semid, &sb, 1); // unlock the semaphore

    return pid; // return the dequeued PID or -1 if empty 
}

// check if the queue is empty function definition 
int is_empty() {
    return shm->count == 0; // return true if count is zero, false otherwise 
}

// check if the queue is full function definition 
int is_full() {
    return shm->count == MAX_CLIENTS; // return true if count is equal to maximum clients, false otherwise 
}
// list the files in the server directory and update the shared memory segment function definition 
void list_files() {
    DIR *d; // pointer to a directory stream
    struct dirent *dir; // pointer to a directory entry
    int index = 0; // variable to store the index of the file in the shared memory segment

    d = opendir("."); // open the current directory

    if (d) { // if the directory is opened successfully
        while ((dir = readdir(d)) != NULL) { // while there are files or directories to read
            if (dir->d_type == DT_REG) { // if the entry is a regular file
                strcpy(shm->files[index].name, dir->d_name); // copy the file name to the shared memory segment
                shm->files[index].fd = open(dir->d_name, O_RDWR); // open the file for reading and writing
                shm->files[index].size = lseek(shm->files[index].fd, 0, SEEK_END); // get the file size by moving the file pointer to the end
                shm->files[index].readers = 0; // initialize the number of readers accessing the file to zero
                shm->files[index].writers = 0; // initialize the number of writers accessing the file to zero
                index++; // increment the index of the file in the shared memory segment
            }
        }
        closedir(d); // close the directory
        shm->num_files = index; // update the number of files in the shared memory segment
    }
    else { // if the directory is not opened successfully
        perror("opendir"); // print an error message
    }
}

// handle a client request in a child process function definition 
void handle_client(int pid) {
    char buffer[MAX_LINE]; // buffer to store the client request
    char file[256]; // buffer to store the file name
    int line; // variable to store the line number
    char string[MAX_LINE]; // buffer to store the string to write

    sprintf(buffer, "/tmp/%d", pid); // create a FIFO name based on the client PID
    mkfifo(buffer, 0666); // create a FIFO with read and write permissions
    int fd = open(buffer, O_RDWR); // open the FIFO for reading and writing

    read(fd, buffer, MAX_LINE); // read the client request from the FIFO
    printf("Client %d request: %s\n", pid, buffer);

    if (strcmp(buffer, "help") == 0) { // if the client requests help
        write(fd, "Available commands are: help, list, readF, writeT, upload, download, quit, killServer\n", 
				strlen("Available commands are: help, list, readF, writeT, upload, download, quit, killServer\n")); // write the help message to the FIFO
    }
    else if (strcmp(buffer, "list") == 0) { // if the client requests to list the files
        for (int i = 0; i < shm->num_files; i++) { // for each file in the shared memory segment
            sprintf(buffer, "%s\t%d bytes\n", shm->files[i].name, shm->files[i].size); // format the file name and size
            write(fd, buffer, MAX_LINE); // write the file information to the FIFO
        }
        write(fd, "End of list\n", strlen("End of list\n")); // write an end of list message to the FIFO

    }
    else if (sscanf(buffer, "readF %s %d", file, &line) == 2) { // if the client requests to read a line from a file
        read_file(file, line); // read the line from the file and send it to the client
    }
    else if (sscanf(buffer, "readF %s", file) == 1) { // if the client requests to read a whole file
        read_file(file, 0); // read the whole file and send it to the client
    }
    else if (sscanf(buffer, "writeT %s %d %s", file, &line, string) == 3) { // if the client requests to write a string to a line in a file
        write_file(file, line, string); // write the string to the line in the file and send a confirmation to the client
    }
    else if (sscanf(buffer, "writeT %s %s", file, string) == 2) { // if the client requests to write a string to the end of a file
        write_file(file, 0, string); // write the string to the end of the file and send a confirmation to the client
    }
    else if (sscanf(buffer, "upload %s", file) == 1) { // if the client requests to upload a file
        upload_file(file); // receive and save the file from the client and send a confirmation to the client
    }
    else if (sscanf(buffer, "download %s", file) == 1) { // if the client requests to download a file
        download_file(file); // send and delete the file from the server and send a confirmation to the client
    }
    else if (strcmp(buffer, "quit") == 0) { // if the client requests to quit
        sprintf(buffer, "Client %d quit.\n", pid); // format a quit message for log file 
        write_file(LOG_FILE, 0, buffer); // write the quit message to log file 
        write(fd, "Bye\n", strlen("Bye\n")); // write a bye message to FIFO 
    }
        else if (strcmp(buffer, "killServer") == 0) { // if the client requests to kill the server
        kill_server(); // kill the server process and all its children
    }
    else { // if the client request is invalid
        write(fd, "Invalid command. Type help for available commands.\n", strlen("Invalid command. Type help for available commands.\n")); // write an error message to the FIFO
    }

    close(fd); // close the FIFO
    unlink(buffer); // remove the FIFO
}

// read a line or the whole file and send it to the client function definition 
void read_file(char *file, int line) {
    char buffer[MAX_LINE]; // buffer to store the file content or error message
    int found = 0; // flag to indicate if the file is found in the shared memory segment
    int index = -1; // variable to store the index of the file in the shared memory segment
    pid_t pid;
    int fd;

    for (int i = 0; i < shm->num_files; i++) { // for each file in the shared memory segment
        if (strcmp(shm->files[i].name, file) == 0) { // if the file name matches
            found = 1; // set the flag to true
            index = i; // store the index of the file
            break; // break the loop
        }
    }

    if (found) { // if the file is found
        shm->files[index].readers++; // increment the number of readers accessing the file

        if (line > 0) { // if a specific line is requested
            FILE *fp = fdopen(shm->files[index].fd, "r"); // open the file for reading
            int count = 0; // variable to count the lines read

            while (fgets(buffer, MAX_LINE, fp) != NULL) { // while there are lines to read from the file
                count++; // increment the line count

                if (count == line) { // if the line count matches the requested line number
                    break; // break the loop
                }
            }

            if (count < line) { // if the requested line number is larger than the number of lines in the file
                sprintf(buffer, "Line %d does not exist in %s\n", line, file); // format an error message
            }

            fclose(fp); // close the file
        }
        else { // if the whole file is requested
            int n = read(shm->files[index].fd, buffer, MAX_LINE); // read up to MAX_LINE bytes from the file

            if (n < 0) { // if read error occurs
                perror("read");
                sprintf(buffer, "Read error on %s\n", file); // format an error message
            }
            else if (n == 0) { // if end of file is reached
                sprintf(buffer, "End of %s\n", file); // format an end of file message
            }
        }

        shm->files[index].readers--; // decrement the number of readers accessing the file
    }
    else { // if the file is not found 
        sprintf(buffer, "%s does not exist in server directory\n", file); // format an error message 
    }

    sprintf(buffer, "/tmp/%d", pid); // create a FIFO name based on the client PID 
    mkfifo(buffer, 0666); // create a FIFO with read and write permissions 
    fd = open(buffer, O_RDWR); // open the FIFO for reading and writing 

    write(fd, buffer, MAX_LINE); // write the file content or error message to the FIFO 

    close(fd); // close the FIFO 
    unlink(buffer); // remove the FIFO 
}

// write a string to a line or the end of a file and send a confirmation to the client function definition 
void write_file(char *file, int line, char *string) {
    char buffer[MAX_LINE]; // buffer to store the confirmation message or error message
    int found = 0; // flag to indicate if the file is found in the shared memory segment
    int index = -1; // variable to store the index of the file in the shared memory segment
    pid_t pid;
    int fd;

    for (int i = 0; i < shm->num_files; i++) { // for each file in the shared memory segment
        if (strcmp(shm->files[i].name, file) == 0) { // if the file name matches
            found = 1; // set the flag to true
            index = i; // store the index of the file
            break; // break the loop
        }
    }

    if (found) { // if the file is found
        while (shm->files[index].readers > 0 || shm->files[index].writers > 0) { // while there are other readers or writers accessing the file
            sleep(1); // sleep for 1 second and try again later
        }

        shm->files[index].writers++; // increment the number of writers accessing the file

        if (line > 0) { // if a specific line is requested
            FILE *fp = fdopen(shm->files[index].fd, "r+"); // open the file for reading and writing
            int count = 0; // variable to count the lines read

            while (fgets(buffer, MAX_LINE, fp) != NULL) { // while there are lines to read from the file
                count++; // increment the line count

                if (count == line) { // if the line count matches the requested line number
                    fseek(fp, -strlen(buffer), SEEK_CUR); // move the file pointer back to the beginning of the line
                    fprintf(fp, "%s\n", string); // write the string to the line
                    break; // break the loop
                }
            }

            if (count < line) { // if the requested line number is larger than the number of lines in the file
                sprintf(buffer, "Line %d does not exist in %s\n", line, file); // format an error message
                write(fd, buffer, MAX_LINE); // write the error message to FIFO 
                shm->files[index].writers--; // decrement the number of writers accessing the file 
                fclose(fp); // close the file 
                return; // return from the function 
            }

            fclose(fp); // close the file 
        }
        else { // if the end of file is requested 
            lseek(shm->files[index].fd, 0, SEEK_END); // move the file pointer to the end of file 
            write(shm->files[index].fd, string, strlen(string)); // write the string to end of file 
        }

        shm->files[index].writers--; // decrement the number of writers accessing the file 

        sprintf(buffer, "Write successful on %s\n", file); // format a confirmation message 
    }
    else { // if the file is not found 
        sprintf(buffer, "%s does not exist in server directory\n", file); // format an error message 
    }

    sprintf(buffer, "/tmp/%d", pid); // create a FIFO name based on client PID 
    mkfifo(buffer, 0666); // create a FIFO with read and write permissions 
     // open FIFO for reading and writing 
	fd = open(buffer, O_RDWR);
    write(fd, buffer, MAX_LINE); // write confirmation message or error message to FIFO 

    close(fd); // close FIFO 
    unlink(buffer); // remove FIFO 
}

// receive and save a file from client and send a confirmation to client function definition 
void upload_file(char *file) {
	
    char buffer[MAX_LINE]; // buffer to store the confirmation message or error message
    int found = 0; // flag to indicate if the file already exists in the shared memory segment
    int index = -1; // variable to store the index of the file in the shared memory segment
    pid_t pid;
	int fd;
    for (int i = 0; i < shm->num_files; i++) { // for each file in the shared memory segment
        if (strcmp(shm->files[i].name, file) == 0) { // if the file name matches
            found = 1; // set the flag to true
            index = i; // store the index of the file
            break; // break the loop
        }
    }

    if (found) { // if the file already exists
        sprintf(buffer, "%s already exists in server directory\n", file); // format an error message
    }
    else { // if the file does not exist
        index = shm->num_files; // set the index to the next available slot in the shared memory segment
        strcpy(shm->files[index].name, file); // copy the file name to the shared memory segment
        shm->files[index].fd = open(file, O_CREAT | O_RDWR | O_TRUNC, 0666); // create and open a new file with read and write permissions
        shm->files[index].readers = 0; // initialize the number of readers accessing the file to zero
        shm->files[index].writers = 0; // initialize the number of writers accessing the file to zero

        int n; // variable to store the number of bytes read or written

        do {
            n = read(fd, buffer, MAX_LINE); // read up to MAX_LINE bytes from FIFO

            if (n > 0) { // if there are bytes read from FIFO
                write(shm->files[index].fd, buffer, n); // write the bytes to file
                shm->files[index].size += n; // update the file size in the shared memory segment
            }
        } while (n > 0); // repeat until end of FIFO is reached

        shm->num_files++; // increment the number of files in the shared memory segment

        sprintf(buffer, "Upload successful on %s\n", file); // format a confirmation message
    }

    sprintf(buffer, "/tmp/%d", pid); // create a FIFO name based on client PID 
    mkfifo(buffer, 0666); // create a FIFO with read and write permissions 
    fd = open(buffer, O_RDWR); // open FIFO for reading and writing 

    write(fd, buffer, MAX_LINE); // write confirmation message or error message to FIFO 

    close(fd); // close FIFO 
    unlink(buffer); // remove FIFO 
}

// send and delete a file from server and send a confirmation to client function definition 
void download_file(char *file) {
    char buffer[MAX_LINE]; // buffer to store the confirmation message or error message
    int found = 0; // flag to indicate if the file exists in the shared memory segment
    int index = -1; // variable to store the index of the file in the shared memory segment
	pid_t pid;
	int fd;
    for (int i = 0; i < shm->num_files; i++) { // for each file in the shared memory segment
        if (strcmp(shm->files[i].name, file) == 0) { // if the file name matches
            found = 1; // set the flag to true
            index = i; // store the index of the file
            break; // break the loop
        }
    }

    if (found) // if the file exists
	{ 
        while (shm->files[index].readers > 0 || shm->files[index].writers > 0) 
		{ // while there are other readers or writers accessing the file
            sleep(1); // sleep for 1 second and try again later
        }

        shm->files[index].writers++; // increment the number of writers accessing the file

        int n; // variable to store the number of bytes read or written

        do 
		{
            n = read(shm->files[index].fd, buffer, MAX_LINE); // read up to MAX_LINE bytes from file

            if (n > 0) 
			{ // if there are bytes read from file 
                write(fd, buffer, n); // write bytes to FIFO 
            }
        } while (n > 0); // repeat until end of file is reached

        shm->files[index].writers--; // decrement the number of writers accessing the file 

        close(shm->files[index].fd); // close the file 
        unlink(shm->files[index].name); // delete the file 

        for (int i = index; i < shm->num_files - 1; i++) 
		{ // for each file after the deleted file in the shared memory segment 
            shm->files[i] = shm->files[i + 1]; // shift the file entry to the left 
        }

        shm->num_files--; // decrement the number of files in the shared memory segment 

        sprintf(buffer, "Download successful on %s\n", file); // format a confirmation message 
    }
    else // if the file does not exist
	{  
        sprintf(buffer, "%s does not exist in server directory\n", file); // format an error message 
    }

    sprintf(buffer, "/tmp/%d", pid); // create a FIFO name based on client PID 
    mkfifo(buffer, 0666); // create a FIFO with read and write permissions 
    fd = open(buffer, O_RDWR); // open FIFO for reading and writing 

    write(fd, buffer, MAX_LINE); // write confirmation message or error message to FIFO 

    close(fd); // close FIFO 
    unlink(buffer); // remove FIFO 
}

// kill the server process and all its children function definition 
void kill_server() 
{
    char buffer[MAX_LINE]; // buffer to store the confirmation message
	pid_t pid;
    sprintf(buffer, "Server killed.\n"); // format a confirmation message

    sprintf(buffer, "/tmp/%d", pid); // create a FIFO name based on client PID 
    mkfifo(buffer, 0666); // create a FIFO with read and write permissions 
    int fd = open(buffer, O_RDWR); // open FIFO for reading and writing 

    write(fd, buffer, MAX_LINE); // write confirmation message to FIFO 

    close(fd); // close FIFO 
    unlink(buffer); // remove FIFO 

    kill(getppid(), SIGINT); // send a signal to the parent process (server) to terminate gracefully
}


