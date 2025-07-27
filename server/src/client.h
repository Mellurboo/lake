#pragma once
#include <list_head.h>
#include <stdint.h>
#include <stdbool.h>
#include "post_quantum_cryptography.h"

typedef struct Client {
    struct list_head list;
    int fd;
    uint32_t userID;
    uint32_t notifyID;

    bool secure;
    struct AES_ctx aes_ctx;
} Client;
intptr_t client_read(Client* client, void* buf, size_t size);
intptr_t client_write(Client* client, void* buf, size_t size);
