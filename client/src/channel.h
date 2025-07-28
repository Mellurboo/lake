#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t id;
    char* name;
} Channel;
typedef struct Channels {
    Channel* items;
    size_t len, cap;
} Channels;
