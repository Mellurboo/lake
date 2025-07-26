#pragma once
#include <list_head.h>
#include <stdint.h>
#include <stdbool.h>
#include "post_quantum_cryptography.h"
#include "pb.h"

typedef struct Client {
    struct list_head list;
    int fd;
    uint32_t userID;
    uint32_t notifyID;

    bool has_read_buffer_data;
    uint8_t read_buffer_head;
    uint8_t read_buffer[16];
    PacketBuilder pb;
    bool secure;
    struct AES_ctx aes_ctx;
} Client;
intptr_t client_read_(Client* client, void* buf, size_t size);
static inline void client_discard_read_buf(Client* c) {
    c->has_read_buffer_data = false;
    c->read_buffer_head = 0;
}
