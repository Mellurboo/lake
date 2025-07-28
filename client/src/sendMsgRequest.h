#pragma once
#include <stdint.h>

typedef struct {
    uint32_t server_id;
    uint32_t channel_id;
} SendMsgRequest;

void sendMsgRequest_hton(SendMsgRequest* packet);
