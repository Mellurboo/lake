#include "onGetChannels.h"
#include "client.h"
#include "response.h"
#include "incoming_event.h"
#include "redraw.h"
#include "darray.h"
#include <assert.h>
#include <arpa/inet.h>
#include "channel.h"


typedef struct{
    uint32_t id;
    uint32_t last_read_milis_low;
    uint32_t last_read_milis_high;
    uint32_t newest_msg_milis_low;
    uint32_t newest_msg_milis_high;
} GetChannelsResponse;

void getChannelsResponse_ntoh(GetChannelsResponse* response){
    response->id = ntohl(response->id);
    response->last_read_milis_low = ntohl(response->last_read_milis_low);
    response->last_read_milis_high = ntohl(response->last_read_milis_high);

    response->newest_msg_milis_low = ntohl(response->newest_msg_milis_low);
    response->newest_msg_milis_high = ntohl(response->newest_msg_milis_high);
}

void onGetChannels(Client* client, Response* response, IncomingEvent* event) {
    (void)client;
    (void)response;
    if(response->packet_len == 0) {
        event->onEvent = NULL;
        return;
    }
    GetChannelsResponse channels_response = {0};
    assert(response->packet_len > sizeof(channels_response));
    client_read(client, &channels_response, sizeof(channels_response));
    getChannelsResponse_ntoh(&channels_response);
    char* name = calloc(response->packet_len - sizeof(channels_response) + 1, sizeof(char));
    client_read(client, name, response->packet_len - sizeof(channels_response));
    // fprintf(stderr, "We got channel %u `%s`\n", id, name);
    Channel channel = {
        .id = channels_response.id,
        .last_read_milis = (uint64_t)channels_response.last_read_milis_low | ((uint64_t)channels_response.last_read_milis_high) << 32,
        .newest_msg_milis = (uint32_t)channels_response.newest_msg_milis_low | ((uint64_t)channels_response.newest_msg_milis_high) << 32,
        .name = name
    };
    da_push(event->as.onGetChannels.channels, channel);
    redraw();
}
