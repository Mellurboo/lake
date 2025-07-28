#include "sendMsgRequest.h"
#include <snet.h>

void sendMsgRequest_hton(SendMsgRequest* packet) {
    packet->server_id = htonl(packet->server_id);
    packet->channel_id = htonl(packet->channel_id);
}
