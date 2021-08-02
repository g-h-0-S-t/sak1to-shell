#ifndef NBO_ENCODING_H
#define NBO_ENCODING_H

#if __BIG_ENDIAN__
    #include <winsock2.h>
    #define htonll(x) (x)
    #define ntohll(x) (x)
#else
    #include <arpa/inet.h>
    #define htonll(x) ((((uint64_t)htonl(x&0xFFFFFFFF)) << 32) + htonl(x >> 32))
    #define ntohll(x) ((((uint64_t)ntohl(x&0xFFFFFFFF)) << 32) + ntohl(x >> 32))
#endif

#endif
