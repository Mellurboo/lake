#pragma once
#include <stdint.h>
#include <stdlib.h>

typedef struct {
    uint32_t id;
    char* name;
} Server;

typedef struct Servers {
    Server* items;
    size_t len, cap;
} Servers;
