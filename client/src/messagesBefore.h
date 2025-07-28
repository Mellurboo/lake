#pragma once
#include <stdint.h>

typedef struct {
    uint32_t server_id;
    uint32_t channel_id;
    uint32_t milis_low;
    uint32_t milis_high;
    uint32_t count;
} MessagesBeforeRequest;
void messagesBeforeRequest_hton(MessagesBeforeRequest* packet);
typedef struct {
    uint32_t author_id;
    uint32_t milis_low;
    uint32_t milis_high;
    /*content[packet_len - sizeof(MessagesBeforeResponse)]*/
} MessagesBeforeResponse;
void messagesBeforeResponse_ntoh(MessagesBeforeResponse* packet);
