/* 
Coded by d4rkstat1c.
Use educationally/legally.
*/
#ifndef SAKITO_SERVER_TOOLS_H
#define SAKITO_SERVER_TOOLS_H

#ifdef OS_WIN
	#include <WS2tcpip.h>
	#include "sakito_swin_utils.h"
#elif defined OS_LIN
	#include <sys/socket.h>
	#include <netdb.h>
	#include <arpa/inet.h>
	#include "sakito_slin_utils.h"
#endif

#include <stdlib.h>
#include <string.h>

/*

Below contains the various header files which link various sakito-API functions which will be compiled conditionally based on the operating system,
currently supports only linux and windows systems.  The APIs have a matching signature allowing for cross-platform compilation.

*/

// Typedef for function pointer for console functions.
typedef void (*console_func)(Server_map *s_map);

// Typedef for function pointer for server functions.
typedef int (*server_func)(char*, size_t, SOCKET);

/*
Below are functions related to string parsing and IO.
*/

// Function to validate client identifier prior to interaction.
int s_validate_id(Server_map* const s_map) 
{
	int client_id;
	client_id = atoi(s_map->buf+9);

	if (!s_map->clients_sz || client_id < 0 || client_id > s_map->clients_sz - 1)
		return INVALID_CLIENT_ID;

	return client_id;
}

// Function to compare two strings (combined logic of strcmp and strncmp).
int s_compare(const char* buf, const char* str) 
{
	while (*str)
		if (*buf++ != *str++)
			return 0;

	return 1;
}

// Function to return function pointer based on parsed command.
void* s_parse_cmd(char* const buf, size_t *cmd_len, int cmds_len, const char commands[5][11], void** func_array, void* default_func) 
{
	s_read_stdin(buf, cmd_len);

	if (*cmd_len > 1)
		// Parse stdin string and return its corresponding function pointer.
		for (int i = 0; i < cmds_len; i++)
			if (s_compare(buf, commands[i]))
				return func_array[i];

	return default_func;
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

void add_client(Server_map* const s_map, char* const host, const SOCKET client_socket) 
{
	// Lock our mutex to prevent race conditions from occurring with s_delete_client()
	s_mutex_lock(s_map);

	if (s_map->clients_sz == s_map->clients_alloc)
		s_map->clients = realloc(s_map->clients, (s_map->clients_alloc += MEM_CHUNK) * sizeof(Conn));

	// Add hostname string and client_socket file descriptor to s_map->clients structure.
	s_map->clients[s_map->clients_sz].host = host;
	s_map->clients[s_map->clients_sz].sock = client_socket;
	s_map->clients_sz++;

	// Unlock our mutex now.
	s_mutex_unlock(s_map);
}

void s_accept_conns(Server_map* const s_map) 
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

		// Get hostname and port to print to stdout.
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
void s_delete_client(Server_map* const s_map, const int client_id) 
{
	// Lock our mutex to prevent race conditions from occurring with accept_conns().
	s_mutex_lock(s_map);

	// If the file descriptor is open: close it.
	if (s_map->clients[client_id].sock)
		s_closesocket(s_map->clients[client_id].sock);

	// Resize clients member values to remove client.
	resize_conns(s_map, client_id);

	// Unlock our mutex now.
	s_mutex_unlock(s_map);
	printf("Client: \"%s\" disconnected.\n\n", s_map->clients[client_id].host);
}

// Function to copy uint16_t bytes to new memory block/location to abide strict aliasing.
static inline uint16_t s_ntohs_conv(char* const buf) 
{
	uint16_t uint16_new;
	memcpy(&uint16_new, buf, sizeof(uint16_t));

	// Return deserialized bytes.
	return ntohs(uint16_new);
}

#endif
