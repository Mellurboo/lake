#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include "post_quantum_cryptography.h"

typedef struct Client {
    int fd;
    uint32_t userID;
    uint32_t notifyID;

    bool secure;
    struct AES_ctx aes_ctx_read;
    struct AES_ctx aes_ctx_write;
} Client;

intptr_t client_read(Client* client, void* buf, size_t size);
intptr_t client_write(Client* client, void* buf, size_t size);
