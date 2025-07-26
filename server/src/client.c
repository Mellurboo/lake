#include "client.h"
#include <string.h>
#include "gt.h"
#include <sys/socket.h>
#include <assert.h>
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
// NOTE: must be aligned to 16 (aka AES_BLOCKLEN)
static intptr_t client_do_read(Client* client, void* buf, size_t size) {
    intptr_t e = gtread_exact(client->fd, buf, size);
    if (!client->secure || e <= 0) return e;
    AES_CBC_decrypt_buffer(&client->aes_ctx, buf, size);
    return 1;
}
intptr_t client_read_(Client* client, void* buf, size_t size) {
    intptr_t e;
    if(client->has_read_buffer_data) {
        size_t buf_remaining = sizeof(client->read_buffer) - client->read_buffer_head;
        size_t n = buf_remaining < size ? buf_remaining : size;
        memcpy(buf, client->read_buffer + client->read_buffer_head, n);
        client->read_buffer_head += n;
        client->read_buffer_head %= sizeof(client->read_buffer);
        buf = (char*)buf + n;
        size -= n;
        if(size == 0) return 1;
    }
    // read whole chunks of 16
    {
        size_t n = size / sizeof(client->read_buffer) * sizeof(client->read_buffer);
        e = client_do_read(client, buf, n);
        if(e <= 0) return e;
        buf = (char*)buf + n;
        size -= n;
    }
    client->has_read_buffer_data = false;

    assert(size <= 16);
    // read leftover into read buffer and then copy back to buf.
    if(size) {
        e = client_do_read(client, client->read_buffer, ALIGN16(size));
        if(e <= 0) return e;
        memcpy(buf, client->read_buffer, size);
        client->read_buffer_head = size;
        client->has_read_buffer_data = true;
        buf = (char*)buf + size;
        size = 0;
    }
    return 1;
}
static intptr_t client_do_write(Client* client, void* buf, size_t size) {
    if (client->secure) AES_CBC_encrypt_buffer(&client->aes_ctx, buf, size);
    return gtwrite_exact(client->fd, buf, size);
}
intptr_t pbflush(PacketBuilder* pb, Client* client) {
    intptr_t e = client_do_write(client, pb->items, ALIGN16(pb->len));
    pb->len = 0;
    return e;
}
