/* 
Coded by d4rkstat1c.
Use educationally/legally.
*/
#ifndef SAKITO_SERVER_TOOLS_H
#define SAKITO_SERVER_TOOLS_H

#define BACKGROUND_CLIENT -100
#define FILE_NOT_FOUND 1
#define MEM_CHUNK 5

#if OS_WIN
	#include "sakito_swin_utils.h"
#elif OS_LIN
	#include "sakito_slin_utils.h"
#endif

// Typedef for function pointer for console functions.
typedef void (*console_func)(Server_map *s_map);

// Typedef for function pointer for server functions.
typedef int (*server_func)(char*, size_t, SOCKET);


// Function to validate client identifier prior to interaction.
int validate_id(Server_map* const s_map) {
	int client_id;
	client_id = atoi(s_map->buf+9);

	if (!s_map->clients_sz || client_id < 0 || client_id > s_map->clients_sz - 1)
		return FAILURE;
	else
		return client_id;
}


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
void* parse_cmd(char* const buf, size_t *cmd_len, int cmds_len, const char commands[5][11], void** func_array, void* default_func) {
	get_line(buf, cmd_len);

	if (*cmd_len > 1)
		// Parse stdin string and return its corresponding function pointer.
		for (int i = 0; i < cmds_len; i++)
			if (compare(buf, commands[i]))
				return func_array[i];

	// If no command was parsed: send/execute the command string on the client via _popen().
	return default_func;
}

#endif
