// A little wrapper to initialise windows sheize
#ifdef _WIN32
# define NOMINMAX
# include <winsock2.h>
# include <ws2tcpip.h>
#else
# include <unistd.h>
# include <sys/socket.h>
# include <arpa/inet.h>
# include <netdb.h> // gethostbyname
#endif

#ifndef _WIN32
# define closesocket close
#endif


const char* sneterr(void);
#ifdef SNET_IMPLEMENTATION
#ifdef _WIN32
static void __attribute__((constructor)) _init_wsa() {
    WSADATA wsaDATA;
    if(WSAStartup(MAKEWORD(2, 2), &wsaDATA)) {
        fprintf(stderr, "WSAStartup failure\n");
        exit(EXIT_FAILURE);
    }
}
static void __attribute__((destructor)) _deinit_wsa() {
    WSACleanup();
}
#endif
#ifdef _WIN32
#else
# include <string.h>
# include <errno.h>
#endif
const char* sneterr(void) {
#ifdef _WIN32
    // TODO: bruvsky pls implement on binbows opewating system for video james
    return "";
#else
    return strerror(errno);
#endif
}
#endif
