#include "onGetChannels.h"
#include "client.h"
#include "response.h"
#include "incoming_event.h"
#include "redraw.h"
#include "darray.h"
#include <assert.h>
#include <arpa/inet.h>
#include "channel.h"
#include "servers.h"

void onGetServers(Client* client, Response* response, IncomingEvent* event) {
    (void)client;
    (void)response;
    if(response->packet_len == 0) {
        event->onEvent = NULL;
        return;
    }
    assert(response->packet_len > 4);
    uint32_t id = 0;
    client_read(client, &id, sizeof(id));
    id = ntohl(id);
    char* name = calloc(response->packet_len - sizeof(id) + 1, sizeof(char));
    client_read(client, name, response->packet_len - sizeof(id));
    //fprintf(stderr, "We got server %u `%s`\n", id, name);
    Server server = {
        .id = id,
        .name = name
    };
    da_push(event->as.onGetServers.servers, server);
    redraw();
}
