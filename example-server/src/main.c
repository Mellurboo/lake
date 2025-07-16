#include <stdio.h>
#include <gt.h>
#ifdef _WIN32
# define NOMINMAX
# include <winsock2.h>
# include <ws2tcpip.h>
#else
# include <unistd.h>
# include <sys/socket.h>
# include <arpa/inet.h>
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

void client_thread(void* fd_void) {
    int fd = (uintptr_t)fd_void;
    char buf[128];
    for(;;) {
        gtblockfd(fd, GTBLOCKIN);
        int n = read(fd, buf, sizeof(buf));
        if(n == 0) break;
        if(n < 0) break;
        gtblockfd(fd, GTBLOCKOUT);
        int e = write(fd, buf, n);
        (void)e;
    }
    closesocket(fd);
}
#define PORT 6969
// TODO: neterr() function that returns a string on last networking error
// TODO: error logging on networking error
int main(void) {
    gtinit();
    for(size_t i = 0; i < 20; ++i) gtyield();
    int server = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    // TODO: error logging on networking error
    if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        closesocket(server);
        return 1;
    }
    assert(server >= 0);
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    // TODO: error logging on networking error
    if(bind(server, (struct sockaddr*)&address, sizeof(address)) < 0) return 1;
    // TODO: error logging on networking error
    if(listen(server, 50) < 0) return 1;
    fprintf(stderr, "Started listening on: 0.0.0.0:%d\n", PORT);
    for(;;) {
        gtblockfd(server, GTBLOCKIN);
        int client_fd = accept(server, NULL, NULL);
        if(client_fd < 0) break;
        gtgo(client_thread, (void*)(uintptr_t)client_fd);
    }
    (void)server;
    closesocket(server);
    return 0;
}
