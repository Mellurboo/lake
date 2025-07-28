#include "messagesBefore.h"
#include <snet.h>

void messagesBeforeRequest_hton(MessagesBeforeRequest* packet) {
    packet->server_id = htonl(packet->server_id);
    packet->channel_id = htonl(packet->channel_id);
    packet->milis_low = htonl(packet->milis_low);
    packet->milis_high = htonl(packet->milis_high);
    packet->count = htonl(packet->count);
}

void messagesBeforeResponse_ntoh(MessagesBeforeResponse* packet) {
    packet->author_id = ntohl(packet->author_id);
    packet->milis_low = ntohl(packet->milis_low);
    packet->milis_high = ntohl(packet->milis_high);
}
