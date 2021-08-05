#ifndef SAKITO_WINDOWS_TYPES_H
#define SAKITO_WINDOWS_TYPES_H

#ifndef BUFLEN
#define BUFLEN 8192
#endif
#define CONSOLE_FSTR "sak1to-console-(%s>"
#define INTERACT_FSTR "%d-(%s)-%s>"
#define INVALID_FILE INVALID_HANDLE_VALUE
#define READ 1
#define WRITE 0

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
	char buf[BUFLEN + 9]; // BUFLEN + space for 'command code' + "cmd /C " + '\0'

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

typedef HANDLE s_file;

#endif
