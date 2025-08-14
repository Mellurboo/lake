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

typedef struct{
    uint32_t id;
    uint32_t last_read_milis_low;
    uint32_t last_read_milis_high;
    uint32_t newest_msg_milis_low;
    uint32_t newest_msg_milis_high;
} GetServersResponse;

void getServersResponse_ntoh(GetServersResponse* response){
    response->id = ntohl(response->id);
    response->last_read_milis_low = ntohl(response->last_read_milis_low);
    response->last_read_milis_high = ntohl(response->last_read_milis_high);

    response->newest_msg_milis_low = ntohl(response->newest_msg_milis_low);
    response->newest_msg_milis_high = ntohl(response->newest_msg_milis_high);
}

void onGetServers(Client* client, Response* response, IncomingEvent* event) {
    (void)client;
    (void)response;
    if(response->packet_len == 0) {
        event->onEvent = NULL;
        return;
    }
    GetServersResponse servers_response = {0};
    assert(response->packet_len > sizeof(servers_response));
    client_read(client, &servers_response, sizeof(servers_response));
    getServersResponse_ntoh(&servers_response);
    char* name = calloc(response->packet_len - sizeof(servers_response) + 1, sizeof(char));
    client_read(client, name, response->packet_len - sizeof(servers_response));
    //fprintf(stderr, "We got server %u `%s`\n", id, name);
    Server server = {
        .id = servers_response.id,
        .last_read_milis = (uint64_t)servers_response.last_read_milis_low | ((uint64_t)servers_response.last_read_milis_high) << 32,
        .newest_msg_milis = (uint64_t)servers_response.newest_msg_milis_low | ((uint64_t)servers_response.newest_msg_milis_high) << 32,
        .name = name
    };
    da_push(event->as.onGetServers.servers, server);
    redraw();
}
