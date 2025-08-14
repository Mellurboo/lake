#pragma once
#include <stdint.h>
#include <stdlib.h>

typedef struct {
    uint32_t id;
    uint64_t last_read_milis;
    uint64_t newest_msg_milis;
    char* name;
} Server;

typedef struct Servers {
    Server* items;
    size_t len, cap;
} Servers;
