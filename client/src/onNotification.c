#include "onNotification.h"
#include "client.h"
#include "response.h"
#include "darray.h"
#include <assert.h>
#include "incoming_event.h"
#include "redraw.h"
#include "notification.h"

void onNotification(Client* client, Response* response, IncomingEvent* event) {
    // SKIP
    if(response->packet_len == 0) return;
    assert(response->opcode == 0);
    assert(response->packet_len > sizeof(Notification));
    Notification notif;
    // TODO: error check
    client_read(client, &notif, sizeof(notif));
    notification_ntoh(&notif);
    size_t content_len = response->packet_len - sizeof(Notification);
    char* content = malloc(content_len);
    // TODO: error check
    client_read(client, content, content_len);

    if(*event->as.onNotification.active_server_id != notif.server_id || *event->as.onNotification.active_channel_id != notif.channel_id) {
        free(content);
        return;
    }
    uint64_t milis = (((uint64_t)notif.milis_high) << 32) | (uint64_t)notif.milis_low;
    Message msg = {
        .content_len = content_len,
        .content = content,
        .milis = milis,
        .author_id = notif.author_id,
    };
    da_push(event->as.onNotification.msgs, msg);
    redraw();
}
