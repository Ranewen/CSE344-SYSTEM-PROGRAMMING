#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>

#define BUFFER_SIZE 10

typedef struct {
    int in;
    int out;
    int count;
    int done;
    pthread_mutex_t mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
    struct {
        int source_fd;
        int destination_fd;
        char source_file[512];
        char destination_file[512];
    } buffer[BUFFER_SIZE];
} Buffer;

Buffer buf;
int done = 0;

void init_buffer() {
    buf.in = 0;
    buf.out = 0;
    buf.count = 0;
    buf.done = 0;
    pthread_mutex_init(&buf.mutex, NULL);
    pthread_cond_init(&buf.not_full, NULL);
    pthread_cond_init(&buf.not_empty, NULL);
}

void cleanup_buffer() {
    pthread_mutex_destroy(&buf.mutex);
    pthread_cond_destroy(&buf.not_full);
    pthread_cond_destroy(&buf.not_empty);
}

void produce_file_descriptor_pair(const char* source_file, const char* destination_file) {
    pthread_mutex_lock(&buf.mutex);

    // Wait while buffer is full
    while (buf.count == BUFFER_SIZE) {
        pthread_cond_wait(&buf.not_full, &buf.mutex);
    }

    // Open source file for reading
    int source_fd = open(source_file, O_RDONLY);
    if (source_fd == -1) {
        fprintf(stderr, "Error opening file: %s\n", strerror(errno));
        pthread_mutex_unlock(&buf.mutex);
        return;
    }

    // Create or truncate destination file
    int destination_fd = open(destination_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (destination_fd == -1) {
        fprintf(stderr, "Error opening file: %s\n", strerror(errno));
        close(source_fd);
        pthread_mutex_unlock(&buf.mutex);
        return;
    }

    // Copy file descriptors and file names to buffer
    buf.buffer[buf.in].source_fd = source_fd;
    buf.buffer[buf.in].destination_fd = destination_fd;
    strcpy(buf.buffer[buf.in].source_file, source_file);
    strcpy(buf.buffer[buf.in].destination_file, destination_file);
    buf.in = (buf.in + 1) % BUFFER_SIZE;
    buf.count++;

    pthread_cond_signal(&buf.not_empty);
    pthread_mutex_unlock(&buf.mutex);
}

void consume_file_descriptor_pair() {
    pthread_mutex_lock(&buf.mutex);

    // Wait while buffer is empty and producer is not done
    while (buf.count == 0 && !buf.done) {
        pthread_cond_wait(&buf.not_empty, &buf.mutex);
    }

    // Return if buffer is empty and producer is done
    if (buf.count == 0 && buf.done) {
        pthread_mutex_unlock(&buf.mutex);
        return;
    }

    // Get file descriptors and file names from buffer
    int source_fd = buf.buffer[buf.out].source_fd;
    int destination_fd = buf.buffer[buf.out].destination_fd;

    // Copy file content from source to destination
    ssize_t n;
    char buffer[4096];
    while ((n = read(source_fd, buffer, sizeof(buffer))) > 0) {
        write(destination_fd, buffer, n);
    }

    // Close files and print completion status
    close(source_fd);
    close(destination_fd);
    printf("Copied file: %s\n", buf.buffer[buf.out].source_file);

    buf.out = (buf.out + 1) % BUFFER_SIZE;
    buf.count--;

    pthread_cond_signal(&buf.not_full);
    pthread_mutex_unlock(&buf.mutex);
}

void* producer_thread(void* arg) {
    char** directories = (char**)arg;
    char* source_dir = directories[0];
    char* destination_dir = directories[1];

    DIR* dir = opendir(source_dir);
    if (dir == NULL) {
        fprintf(stderr, "Error opening directory: %s\n", strerror(errno));
        return NULL;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            char source_file[512];
            snprintf(source_file, sizeof(source_file), "%s/%s", source_dir, entry->d_name);

            char destination_file[512];
            snprintf(destination_file, sizeof(destination_file), "%s/%s", destination_dir, entry->d_name);

            produce_file_descriptor_pair(source_file, destination_file);
        }
    }

    closedir(dir);

    pthread_mutex_lock(&buf.mutex);
    buf.done = 1;
    pthread_cond_broadcast(&buf.not_empty);
    pthread_mutex_unlock(&buf.mutex);

    return NULL;
}

void* consumer_thread(void* arg) {
    (void)arg; // Unused parameter
    while (1) {
        consume_file_descriptor_pair();

        pthread_mutex_lock(&buf.mutex);
        if (buf.count == 0 && buf.done) {
            pthread_mutex_unlock(&buf.mutex);
            break;
        }
        pthread_mutex_unlock(&buf.mutex);
    }

    return NULL;
}

void handle_signal(int signal)
{
    if (signal == SIGINT) {
        fprintf(stderr, "Received SIGINT signal. Terminating...\n");
        done = 1;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <buffer size> <number of consumers> <source directory> <destination directory>\n", argv[0]);
        return 1;
    }

    int num_consumers = atoi(argv[2]);
    char* source_dir = argv[3];
    char* destination_dir = argv[4];

    // Initialize buffer
    init_buffer();

    // Create producer thread
    pthread_t producer_tid;
    char* directories[] = { source_dir, destination_dir };
    pthread_create(&producer_tid, NULL, producer_thread, directories);

    // Create consumer threads
    pthread_t consumer_tids[num_consumers];
    for (int i = 0; i < num_consumers; i++) {
        pthread_create(&consumer_tids[i], NULL, consumer_thread, NULL);
    }

    // Get start time
    struct timeval start_time;
    gettimeofday(&start_time, NULL);

    // Wait for producer thread to finish
    pthread_join(producer_tid, NULL);

    // Wait for consumer threads to finish
    for (int i = 0; i < num_consumers; i++) {
        pthread_join(consumer_tids[i], NULL);
    }

    // Get end time
    struct timeval end_time;
    gettimeofday(&end_time, NULL);

    // Calculate total time
    double start_seconds = start_time.tv_sec + start_time.tv_usec / 1000000.0;
    double end_seconds = end_time.tv_sec + end_time.tv_usec / 1000000.0;
    double total_time = end_seconds - start_seconds;

    printf("Total time: %.2f seconds\n", total_time);

    // Cleanup buffer
    cleanup_buffer();

    return 0;
}

