#pragma once

#include <stdint.h>

typedef struct Response {
    uint32_t packet_id;
    uint32_t opcode;
    uint32_t packet_len;
} Response;
void response_ntoh(Response* res);
