#include "onOkMessage.h"
#include "client.h"
#include "response.h"
#include "incoming_event.h"
#include "redraw.h"
#include "darray.h"
#include <assert.h>
#include "updateLastRead.h"
#include "time_unix.h"

void okOnMessage(Client* client, Response* response, IncomingEvent* event) {
    (void)client;
    (void)response;
    da_push(event->as.onMessage.msgs, event->as.onMessage.msg);

    uint64_t milis = time_unix_milis();
    updateLastRead(event->as.onMessage.server_id, event->as.onMessage.channel_id, milis);
    updateNewestMessage(event->as.onMessage.server_id, event->as.onMessage.channel_id, milis);
    event->onEvent = NULL;
    redraw();
}
