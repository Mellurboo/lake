#include "client.h"
#include <string.h>
#include "gt.h"
#include <sys/socket.h>
#include <assert.h>
#include "response.h"
// TODO: maybe move htis out but idk
static intptr_t gtread_exact(int fd, void* buf, size_t size) {
    while(size) {
        gtblockfd(fd, GTBLOCKIN);
        intptr_t e = recv(fd, buf, size, 0);
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
    intptr_t e = gtread_exact(client->fd, buf, size);
    if (!client->secure || e <= 0) return e;
    AES_CTR_xcrypt_buffer(&client->aes_ctx_read, buf, size);
    return 1;
}

intptr_t client_write(Client* client, void* buf, size_t size) {
    if (client->secure) AES_CTR_xcrypt_buffer(&client->aes_ctx_write, buf, size);
    return gtwrite_exact(client->fd, buf, size);
}
intptr_t client_write_error(Client* client, uint32_t packet_id, uint32_t error) {
    Response response = {
        .packet_id = packet_id,
        .opcode = (uint32_t)(-(int32_t)error),
        .packet_len = 0,
    };
    response_hton(&response);
    return client_write(client, &response, sizeof(response));
}
void client_discard(Client* client, size_t size) {
    char buf[64];
    while(size) {
        size_t n = size < sizeof(buf) ? size : sizeof(buf);
        intptr_t e = gtread_exact(client->fd, buf, n);
        if(e != 1) return;
        size -= n;
    }
}

