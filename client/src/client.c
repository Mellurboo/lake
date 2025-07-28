#include "client.h"
#include <snet.h>
#include <gt.h>

static intptr_t gtread_exact(Client* client, void* buf, size_t size) {
    while(size) {
        gtblockfd(client->fd, GTBLOCKIN);
        intptr_t e = recv(client->fd, buf, size, 0);
        if(e < 0) return e;
        if(e == 0) return 0; 
        buf = ((char*)buf) + (size_t)e;
        size -= (size_t)e;
    }
    return 1;
}
static intptr_t gtwrite_exact(int fd, const void* buf, size_t size) {
    while(size) {
        gtblockfd(fd, GTBLOCKOUT);
        intptr_t e = send(fd, buf, size, 0);
        if(e < 0) return e;
        if(e == 0) return 0; 
        buf = ((char*)buf) + (size_t)e;
        size -= (size_t)e;
    }
    return 1;
}

intptr_t client_read(Client* client, void* buf, size_t size) {
    intptr_t e = gtread_exact(client, buf, size);
    if (!client->secure || e <= 0) return e;
    AES_CTR_xcrypt_buffer(&client->aes_ctx_read, buf, size);
    return 1;
}

intptr_t client_write(Client* client, void* buf, size_t size) {
    if (client->secure) AES_CTR_xcrypt_buffer(&client->aes_ctx_write, buf, size);
    return gtwrite_exact(client->fd, buf, size);
}
