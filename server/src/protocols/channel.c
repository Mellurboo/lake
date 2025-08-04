#include "protocol.h"
#include "request.h"
#include "response.h"
#include "client.h"
#include "channel.h"
#include "db_context.h"
#include <arpa/inet.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

typedef struct {
    uint32_t server_id;
} GetChannelsRequest;
void getChannelsRequest_ntoh(GetChannelsRequest* packet) {
    packet->server_id = htonl(packet->server_id);
}
typedef struct {
    uint32_t channel_id;
    /*name[...]*/
} GetChannelsResponse;
extern DbContext* db;
void getChannels(Client* client, Request* header) {
    if(header->packet_len < sizeof(GetChannelsRequest)) {
        char buf[sizeof(GetChannelsRequest)];
        // NOTE: discarding the buffer.
        client_read(client, buf, header->packet_len);
        Response resp = {
            .packet_id = header->packet_id,
            .opcode = 1,
            .packet_len = 0
        };
        response_hton(&resp);
        client_write(client, &resp, sizeof(Response));
        return;
    }
    GetChannelsRequest request = { 0 };
    int e = client_read(client, &request, sizeof(request));
    if(e < 0 || e == 0) goto err_read;
    getChannelsRequest_ntoh(&request);
    Channels channels = { 0 };
    e = DbContext_get_channels(db, request.server_id, client->userID, &channels);
    if(e < 0) goto err_get_channels;
    for(size_t i = 0; i < channels.len; ++i) {
        Channel* channel = &channels.items[i];
        assert(channel->name);
        Response resp_header = {
            .packet_id = header->packet_id,
            .opcode = 0,
            .packet_len = sizeof(GetChannelsResponse) + strlen(channel->name)
        };
        response_hton(&resp_header);
        client_write(client, &resp_header, sizeof(resp_header));
        GetChannelsResponse get_channels_response = {
            .channel_id = channel->id
        };
        get_channels_response.channel_id = htonl(get_channels_response.channel_id);
        client_write(client, &get_channels_response, sizeof(get_channels_response));
        client_write(client, channel->name, strlen(channel->name));
    }
    Response resp_header = {
        .packet_id = header->packet_id,
        .opcode = 0,
        .packet_len = 0 
    };
    response_hton(&resp_header);
    client_write(client, &resp_header, sizeof(resp_header));
    free_channels(&channels);
    return;
    /*Start responding with some shezung*/
err_get_channels:
    free_channels(&channels);
err_read:
    Response resp = {
        .packet_id = header->packet_id,
        .opcode = 1,
        .packet_len = 0
    };
    response_hton(&resp);
    client_write(client, &resp, sizeof(Response));
    return;
}
protocol_func_t channelProtocolFuncs[] = {
    getChannels,
};
Protocol channelProtocol = PROTOCOL("channel", channelProtocolFuncs);
