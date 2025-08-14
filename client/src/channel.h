#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t id;
    uint64_t last_read_milis;
    uint64_t newest_msg_milis;
    char* name;
} Channel;
typedef struct Channels {
    Channel* items;
    size_t len, cap;
} Channels;
