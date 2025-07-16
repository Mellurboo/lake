#include <stdio.h>
#include <gt.h>
#include <winnet.h>
#include <assert.h>

void client_thread(void* fd_void) {
    int fd = (uintptr_t)fd_void;
    char buf[128];
    for(;;) {
        gtblockfd(fd, GTBLOCKIN);
        int n = recv(fd, buf, sizeof(buf), 0);
        if(n == 0) break;
        printf("Got MSG!\n");
        if(n < 0) break;
        gtblockfd(fd, GTBLOCKOUT);
        int e = send(fd, buf, n, 0);
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
        printf("Connected!\n");
        gtgo(client_thread, (void*)(uintptr_t)client_fd);
    }
    (void)server;
    closesocket(server);
    return 0;
}
