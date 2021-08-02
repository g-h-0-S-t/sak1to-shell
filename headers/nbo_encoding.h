#ifndef NBO_ENCODING_H
#define NBO_ENCODING_H

#include "os_check.h"

#if !__BIG_ENDIAN__
    #ifdef OS_LIN
        #include <arpa/inet.h>
    #endif
    #define htonll(x) ((((uint64_t)htonl(x&0xFFFFFFFF)) << 32) + htonl(x >> 32))
    #define ntohll(x) ((((uint64_t)ntohl(x&0xFFFFFFFF)) << 32) + ntohl(x >> 32))
#endif

#endif
