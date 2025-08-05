#include "protocol.h"
#include "request.h"
#include "response.h"
#include "client.h"
#include "msg.h"
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include "db_context.h"
#include <arpa/inet.h>
#include "time_unix.h"
#include "darray.h"
#include "user_map.h"
#include <string.h>

extern DbContext* db;
extern UserMap user_map;

typedef struct {
    uint32_t server_id;
    uint32_t channel_id;
    uint32_t author_id;
    uint32_t milis_low;
    uint32_t milis_high;
} Notification;

void notification_hton(Notification* packet) {
    packet->server_id = htonl(packet->server_id);
    packet->channel_id = htonl(packet->channel_id);
    packet->author_id = htonl(packet->author_id);
    packet->milis_low = htonl(packet->milis_low);
    packet->milis_high = htonl(packet->milis_high);
}

extern struct list_head global_client_refs;

#define MAX_MESSAGE 10000
typedef struct {
    uint32_t server_id;
    uint32_t channel_id;
    char msg[];
} SendMsgPacket;
void sendMsgPacket_ntoh(SendMsgPacket* packet) {
    packet->server_id = ntohl(packet->server_id);
    packet->channel_id = ntohl(packet->channel_id);
}
typedef struct {
    uint32_t *items;
    size_t len, cap;
} Participants;
void sendMsg(Client* client, Request* header) {
    // NOTE: we hard assert its MORE because you need at least 1 character per message
    // TODO: send some error here:
    if(header->packet_len <= sizeof(SendMsgPacket)) {
        char buf[sizeof(SendMsgPacket)];
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
    SendMsgPacket packet = { 0 };
    int n = client_read(client, &packet, sizeof(packet));
    // TODO: send some error here:
    if(n < 0 || n == 0) goto err_read0;
    sendMsgPacket_ntoh(&packet);
    size_t msg_len = header->packet_len - sizeof(SendMsgPacket);
    // TODO: send some error here:
    if(msg_len > MAX_MESSAGE) return;
    char* msg = malloc(msg_len);
    // TODO: send some error here:
    if(!msg) return;
    n = client_read(client, msg, msg_len);
    // TODO: send some error here:
    if(n < 0 || n == 0) goto err_read;
    // TODO: utf8 and isgraphic verifications
    Message message = {
        .content_len = msg_len,
        .content = msg,
        .milis = time_unix_milis(),
        .author = client->userID,
    };
    int e = DbContext_send_msg(db, packet.server_id, packet.channel_id, client->userID, message.content, message.content_len, message.milis);
    //TODO: send some error here:
    if(e < 0) return;

    Participants participants = { 0 };
    if(packet.server_id == 0) {
        da_push(&participants, client->userID);
        da_push(&participants, packet.channel_id);
    } else {
        list_foreach(client_node, &global_client_refs) {
            ClientRef* other_ref = (ClientRef*)client_node;
            Client* other = other_ref->client;
            if(!other->secure) continue;
            da_push(&participants, other->userID);
        }
    }

    char* tmp_msg_clone = malloc(msg_len);
    for(size_t i = 0; i < participants.len; ++i) {
        UserMapBucket* user = user_map_get(&user_map, participants.items[i]);
        if(!user) continue;
        list_foreach(user_conn_list, &user->clients) {
            Client* user_conn = (Client*)user_conn_list;
            if(user_conn->notifyID == ~0u) continue;
            if(user_conn == client) continue;
            Response resp = {
                .packet_id = user_conn->notifyID,
                .opcode = 0,
                .packet_len = sizeof(Notification) + msg_len
            };
            response_hton(&resp);
            Notification notif = {
                .server_id = packet.server_id,
                .channel_id = packet.server_id == 0 ? participants.items[(i + 1) % 2] : packet.channel_id,
                .author_id = message.author,
                .milis_low = message.milis,
                .milis_high = message.milis >> 32,
            };
            notification_hton(&notif);
            // TODO: don't block here? And/or spawn a gt thread for each user we're notifying
            client_write(user_conn, &resp, sizeof(Response));
            client_write(user_conn, &notif, sizeof(Notification));
            memcpy(tmp_msg_clone, msg, msg_len);
            client_write(user_conn, tmp_msg_clone, msg_len);
        }
    }
    free(tmp_msg_clone);
    free(participants.items);
    /*
    for(size_t i = 0; i < channel->participants.len; ++i) {
        uint32_t id = channel->participants.items[i];
        User* user = &users[id];
        list_foreach(user_conn_list, &user->clients) {
            Client* user_conn = (Client*)user_conn_list;
            if(user_conn->notifyID == ~0u) continue;
            if(user_conn == client) continue;
            Response resp = {
                .packet_id = user_conn->notifyID,
                .opcode = 0,
                .packet_len = sizeof(Notification) + msg_len
            };
            response_hton(&resp);
            Notification notif = {
                .server_id = packet.server_id,
                .channel_id = packet.server_id == 0 ? channel->participants.items[(i + 1) % 2] : packet.channel_id,
                .author_id = message.author,
                .milis_low = message.milis,
                .milis_high = message.milis >> 32,
            };
            notification_hton(&notif);
            // TODO: don't block here? And/or spawn a gt thread for each user we're notifying
            pbwrite(&user_conn->pb, &resp, sizeof(Response));
            pbwrite(&user_conn->pb, &notif, sizeof(Notification));
            pbwrite(&user_conn->pb, msg, msg_len);
        }
    }
    */
    Response resp = {
        .packet_id = header->packet_id,
        .opcode = 0,
        .packet_len = 0
    };
    response_hton(&resp);
    client_write(client, &resp, sizeof(resp));
    return;
err_read:
    free(msg);
err_read0:
    return;
}
typedef struct {
    uint32_t server_id;
    uint32_t channel_id;
    uint32_t milis_low;
    uint32_t milis_high;
    uint32_t count;
} MessagesBeforePacket;
void messagesBeforePacket_ntoh(MessagesBeforePacket* packet) {
    packet->server_id = ntohl(packet->server_id);
    packet->channel_id = ntohl(packet->channel_id);
    packet->milis_low = ntohl(packet->milis_low);
    packet->milis_high = ntohl(packet->milis_high);
    packet->count = ntohl(packet->count);
}
typedef struct {
    uint32_t author_id;
    uint32_t milis_low;
    uint32_t milis_high;
    /*content[packet_len - sizeof(MessagesBeforeResponse)]*/
} MessagesBeforeResponse;
void messagesBeforeResponse_hton(MessagesBeforeResponse* packet) {
    packet->author_id = htonl(packet->author_id);
    packet->milis_low = htonl(packet->milis_low);
    packet->milis_high = htonl(packet->milis_high);
}
void getMsgsBefore(Client* client, Request* header) {
    // TODO: send some error here:
    if(header->packet_len != sizeof(MessagesBeforePacket)) return;
    MessagesBeforePacket packet;
    int n = client_read(client, &packet, sizeof(packet));
    // TODO: send some error here:
    if(n < 0 || n == 0) goto err_read0;
    messagesBeforePacket_ntoh(&packet);
    uint64_t milis = (((uint64_t)packet.milis_high) << 32) | (uint64_t)packet.milis_low;

    Messages msgs = {0};
    int e = DbContext_get_msgs_before(db, packet.server_id, packet.channel_id, client->userID, milis, packet.count, &msgs);
    //TODO: send some error here:
    if(e < 0) goto finish;

    for(size_t i = msgs.len; i > 0; --i) {
        Message* msg = &msgs.items[i - 1];
        Response resp = {
            .packet_id = header->packet_id,
            .opcode = 0,
            .packet_len = msg->content_len + sizeof(MessagesBeforeResponse),
        };
        MessagesBeforeResponse msg_resp = {
            .author_id = msg->author,
            .milis_low = msg->milis,
            .milis_high = msg->milis >> 32,
        };
        response_hton(&resp);
        messagesBeforeResponse_hton(&msg_resp);
        client_write(client, &resp, sizeof(resp));
        client_write(client, &msg_resp, sizeof(msg_resp));
        client_write(client, msg->content, msg->content_len);

        free(msg->content);
    } 
finish:
    {} // apparently labels cannot be near declarations so this is lil hack
    Response resp = {
        .packet_id = header->packet_id,
        .opcode = 0,
        .packet_len = 0,
    };
    response_hton(&resp);
    client_write(client, &resp, sizeof(resp));
err_read0:
    return;
}
protocol_func_t msgProtocolFuncs[] = {
    sendMsg,
    getMsgsBefore,
};
Protocol msgProtocol = PROTOCOL("msg", msgProtocolFuncs);
