/* 
Coded by d4rkstat1c.
Use educationally/legally.
*/
#ifndef SAKITO_TOOLS_H
#define SAKITO_TOOLS_H
#define BUFLEN 8192
// Default allocation for conns.clients (to repeat repititive calls to realloc/reduce computations).
#define MEM_CHUNK 5

#if defined(_WIN32) || defined(_WIN64) || (defined(__CYGWIN__) && !defined(_WIN32))
	// Typedef for function pointer.
	typedef int (*func)(char*, size_t, SOCKET);

	typedef struct {
		// Client hostname.
		char* host;

		// Client socket.
		SOCKET sock;

	} Conn;

	typedef struct {
		// Mutex object for race condition checks.
		HANDLE ghMutex;

		// Server socket for accepting connections.
		SOCKET listen_socket;

		// Array of Conn objects/structures.
		Conn* clients;

		// Memory blocks allocated.
		size_t alloc;

		// Amount of memory used.
		size_t size;

	} Conn_map;
#elif defined(__linux__)
	// Typedef for function pointer.
	typedef int (*func)(char*, size_t, int);

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
#endif

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

 // Function to read/store stdin in buffer until \n is detected.
size_t get_line(char* const buf) {
	size_t cmd_len = 0;
	char c;

	while ((c = getchar()) != '\n' && cmd_len < BUFLEN)
		buf[cmd_len++] = c;

	return cmd_len;
}

// Function to compare two strings (combined logic of strcmp and strncmp).
int compare(const char* buf, const char* str) {
	for (int j = 0; str[j] != '\0'; j++)
		if (str[j] != buf[j])
			return 0;

	return 1;
}

// Function to copy int bytes to new memory block/location to abide strict aliasing.
static inline uint32_t ntohl_conv(char* const buf) {
	uint32_t new;
	memcpy(&new, buf, sizeof(new));

	// Return deserialized bytes.
	return ntohl(new);
}

#endif
