#pragma once

#include <stdint.h>

typedef struct {
    uint32_t server_id;
    uint32_t channel_id;
    uint32_t milis_low;
    uint32_t milis_high;
} LastReadRequest;

void lastReadRequest_hton(LastReadRequest* packet);
