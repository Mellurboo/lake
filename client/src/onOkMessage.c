#include "onOkMessage.h"
#include "client.h"
#include "response.h"
#include "incoming_event.h"
#include "redraw.h"
#include "darray.h"
#include <assert.h>

void okOnMessage(Client* client, Response* response, IncomingEvent* event) {
    (void)client;
    (void)response;
    da_push(event->as.onMessage.msgs, event->as.onMessage.msg);
    event->onEvent = NULL;
    redraw();
}
