#pragma once
#include <stdint.h>
// Generic response header
typedef struct Response {
    uint32_t packet_id;
    uint32_t opcode;
    uint32_t packet_len;
} Response;
void response_hton(Response* res);
