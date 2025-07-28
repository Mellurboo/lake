#pragma once
#include <stdint.h>

typedef struct {
    uint32_t id;
    char name[];
} Protocol;
