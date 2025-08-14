#include <stddef.h>
#include <stdint.h>

#include "client.h"
#include "incoming_event.h"
#include "request.h"
#include "channel.h"
#include "servers.h"

#include "lastRead.h"
#include "updateLastRead.h"
#include "onOkSetLastRead.h"

extern Client client;
extern uint32_t lastRead_protocol_id;
extern Channels dm_channels;
extern Servers servers;
extern Channels server_channels;

void updateLastRead(uint32_t server_id, uint32_t channel_id, uint64_t milis){
    if(server_id == 0){
        for(size_t i = 0; i < dm_channels.len; i++){
            Channel* channel = &dm_channels.items[i];
            if(channel->id != channel_id) continue;
            channel->last_read_milis = milis;
            break;
        }
    }else{
        for(size_t i = 0; i < servers.len; i++){
            Server* server = &servers.items[i];
            if(server->id != server_id) continue;
            server->last_read_milis = milis;
            break;
        }

        for(size_t i = 0; i < server_channels.len; i++){
            Channel* channel = &server_channels.items[i];
            if(channel->id != channel_id) continue;
            channel->last_read_milis = milis;
            break;
        }
    }

    if(lastRead_protocol_id == 0) return;

    Request request = {
        .protocol_id = lastRead_protocol_id,
        .func_id = 0,
        .packet_id = allocate_incoming_event(),
        .packet_len = sizeof(LastReadRequest)
    };
    incoming_events[request.packet_id].onEvent = onOkSetLastRead;
    request_hton(&request);

    LastReadRequest lastRead_request = {
        .server_id = server_id,
        .channel_id = channel_id,
        .milis_low = milis,
        .milis_high = milis >> 32,
    };
    lastReadRequest_hton(&lastRead_request);
    client_write(&client, &request, sizeof(request));
    client_write(&client, &lastRead_request, sizeof(lastRead_request));
}

void updateNewestMessage(uint32_t server_id, uint32_t channel_id, uint64_t milis){
    if(server_id == 0){
        for(size_t i = 0; i < dm_channels.len; i++){
            Channel* channel = &dm_channels.items[i];
            if(channel->id != channel_id) continue;
            channel->newest_msg_milis = milis;
            break;
        }
    }else{
        for(size_t i = 0; i < servers.len; i++){
            Server* server = &servers.items[i];
            if(server->id != server_id) continue;
            server->newest_msg_milis = milis;
            break;
        }

        for(size_t i = 0; i < server_channels.len; i++){
            Channel* channel = &server_channels.items[i];
            if(channel->id != channel_id) continue;
            channel->newest_msg_milis = milis;
            break;
        }
    }
}
