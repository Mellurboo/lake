#pragma once
#include <stdint.h>
#include <stddef.h>

#define ALIGN16(n) (((n) + 15) & ~15)
typedef struct Client Client;
typedef struct PacketBuilder {
    uint8_t* items;
    size_t cap, len; // <- how much data is already in here
} PacketBuilder;


void pbwrite(PacketBuilder* pb, const void* buf, size_t size);
// NOTE:
// implemented in client.c
intptr_t pbflush(PacketBuilder* pb, Client* client);
