#include <stdio.h>
#include <gt.h>
#include <winnet.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

#define PORT 6969
int main(void) {
    int client = socket(AF_INET, SOCK_STREAM, 0); 
    int opt = 1;
    // TODO: error logging on networking error
    if (setsockopt(client, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        closesocket(client);
        return 1;
    }
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    // TODO: error logging on networking error
    if(connect(client, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) return 1;

    for(;;) {
        char buf[128];
        printf("> ");
        fflush(stdout);
        char* e = fgets(buf, sizeof(buf), stdin);
        (void)e;
        if(strcmp(buf, ":quit\n") == 0) break;
        send(client, buf, strlen(buf), 0);
        int n = recv(client, buf, sizeof(buf)-1, 0);
        if(n <= 0) {
            fprintf(stderr, "Thingy: %s\n", strerror(errno));
            break;
        }
        buf[n] = '\0';
        printf("%s", buf);
    }
    closesocket(client);
    return 0;
}
