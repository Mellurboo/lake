#include "notification.h"
#include <snet.h>

void notification_ntoh(Notification* packet) {
    packet->server_id = ntohl(packet->server_id);
    packet->channel_id = ntohl(packet->channel_id);
    packet->author_id = ntohl(packet->author_id);
    packet->milis_low = ntohl(packet->milis_low);
    packet->milis_high = ntohl(packet->milis_high);
}
