#include "lastRead.h"
#include <snet.h>

void lastReadRequest_hton(LastReadRequest* packet){
    packet->server_id = htonl(packet->server_id);
    packet->channel_id = htonl(packet->channel_id);
    packet->milis_low = htonl(packet->milis_low);
    packet->milis_high = htonl(packet->milis_high);
}
