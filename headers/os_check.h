#ifndef OS_CHECK_H
#define OS_CHECK_H

#if defined(_WIN32) || defined(_WIN64) || (defined(__CYGWIN__) && !defined(_WIN32))
	#define OS_WIN 1
#elif defined(__linux__)
	#define OS_LIN 1

#endif

#endif
