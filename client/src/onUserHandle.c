#include "onUserHandle.h"
#include "client.h"
#include "response.h"
#include "incoming_event.h"
#include "darray.h"
#include <assert.h>
#include <arpa/inet.h>
#include "handle_map.h"
#include "gt.h"

void onUserHandle(Client* client, Response* response, IncomingEvent* event) {
    if(response->packet_len == 0 && response->opcode != 0) {
        // TODO: report error.
        event->as.onUserHandle.bucket->user_id = 0;
        gtmutex_unlock(event->as.onUserHandle.mutex);
        return;
    }
    assert(response->packet_len == 4);
    uint32_t id = 0;
    client_read(client, &id, sizeof(id));
    id = ntohl(id);
    event->as.onUserHandle.bucket->user_id = id;
    gtmutex_unlock(event->as.onUserHandle.mutex);
}
