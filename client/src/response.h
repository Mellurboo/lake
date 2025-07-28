#pragma once

#include <stdint.h>

typedef struct {
    uint32_t packet_id;
    uint32_t opcode;
    uint32_t packet_len;
} Response;
void response_ntoh(Response* res);
