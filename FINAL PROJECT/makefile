# Makefile

CC = gcc
CFLAGS = -Wall -Wextra -pedantic -Wno-unused-parameter -pthread

# Name of the server executable
SERVER_TARGET = server

# Name of the client executable
CLIENT_TARGET = client

# List of server source files
SERVER_SRCS = BibakBOXServer.c

# List of client source files
CLIENT_SRCS = BibakBOXClient.c

# Object files for server
SERVER_OBJS = $(SERVER_SRCS:.c=.o)

# Object files for client
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)

# Default rule
all: $(SERVER_TARGET) $(CLIENT_TARGET)

# Rule to build the server executable
$(SERVER_TARGET): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Rule to build the client executable
$(CLIENT_TARGET): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Rule to compile the server source files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Rule to clean the object files and executables
clean:
	rm -f $(SERVER_OBJS) $(CLIENT_OBJS) $(SERVER_TARGET) $(CLIENT_TARGET)
