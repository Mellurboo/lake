#pragma once
#include <list_head.h>
#include "gt.h"
#include <stdint.h>
#include <stdbool.h>
#include "post_quantum_cryptography.h"

typedef struct Client {
    struct list_head list;
    int fd;
    uint32_t userID;
    uint32_t notifyID;

    GTMutex write_mutex;
    GTMutex read_mutex;

    bool secure;
    struct AES_ctx aes_ctx_write;
    struct AES_ctx aes_ctx_read;
} Client;
#define client_lock_read(client) gtmutex_lock(&(client)->read_mutex)
#define client_lock_write(client) gtmutex_lock(&(client)->write_mutex)
#define client_unlock_read(client) gtmutex_unlock(&(client)->read_mutex)
#define client_unlock_write(client) gtmutex_unlock(&(client)->write_mutex)

#define client_write_scoped(__client) gtmutex_scoped(&(client)->write_mutex) 
#define client_read_scoped(__client) gtmutex_scoped(&(client)->read_mutex)
intptr_t client_read(Client* client, void* buf, size_t size);
intptr_t client_write(Client* client, void* buf, size_t size);
// NOTE: does a write lock for convenience
intptr_t client_write_error(Client* client, uint32_t packet_id, uint32_t error);
void client_discard(Client* client, size_t size);


typedef struct{
    struct list_head list;
    Client* client;
} ClientRef;
