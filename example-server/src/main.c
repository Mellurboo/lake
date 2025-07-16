#include <stdio.h>
#include <gt.h>
#ifdef _WIN32
# define NOMINMAX
# include <winsock2.h>
# include <ws2tcpip.h>
#else
# include <unistd.h>
# include <sys/socket.h>
#endif
#include <assert.h>

#ifndef _WIN32
# define closesocket close
#endif
#ifdef _WIN32
void __attribute__((constructor)) _init_wsa() {
    WSADATA wsaDATA;
    if(WSAStartup(MAKEWORD(2, 2), &wsaDATA)) {
        fprintf(stderr, "WSAStartup failure\n");
        exit(EXIT_FAILURE);
    }
}
void __attribute__((destructor)) _deinit_wsa() {
    WSACleanup();
}
#endif
// TODO: neterr() function that returns a string on last networking error
// TODO: error logging on networking error
int main(void) {
    printf("Hello from example-server!\n");
    int server = socket(AF_INET, SOCK_STREAM, 0);
    assert(server >= 0);
    (void)server;
    closesocket(server);
    return 0;
}
