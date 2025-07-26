#pragma once
#include <stdint.h>
// Generic request header
typedef struct Request {
    uint32_t protocol_id;
    uint32_t func_id;
    uint32_t packet_id;
    uint32_t packet_len;
} Request;
void request_ntoh(Request* req);
