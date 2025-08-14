#include "onOkSetLastRead.h"
#include "client.h"
#include "response.h"
#include "incoming_event.h"
#include "darray.h"
#include <assert.h>

void onOkSetLastRead(Client* client, Response* response, IncomingEvent* event) {
    (void)client;
    (void)response;
    event->onEvent = NULL;
}
