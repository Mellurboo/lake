#include "user.h"
#include "protocol.h"
#include "request.h"
#include "response.h"
#include "client.h"
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include "db_context.h"
extern DbContext* db;

typedef struct {
    uint32_t userID;
} GetUserInfoPacket;
void getUserInfoPacket_ntoh(GetUserInfoPacket* packet){
    packet->userID = ntohl(packet->userID);
}
void userGetUserInfo(Client* client, Request* header) {
    // TODO: send some error here:
    if(header->packet_len != sizeof(GetUserInfoPacket)) return;
    GetUserInfoPacket packet;
    int n = client_read(client, &packet, sizeof(packet));
    // TODO: send some error here:
    if(n < 0 || n == 0) return;
    getUserInfoPacket_ntoh(&packet);
    
    char* username = NULL;
    DbContext_get_username_from_user_id(db, packet.userID, &username);
    if(username == NULL){
        Response resp = {
            .packet_id = header->packet_id,
            .opcode = 1,
            .packet_len = 0,
        };
        response_hton(&resp);
        client_write(client, &resp, sizeof(resp));
        return;
    }

    Response resp = {
        .packet_id = header->packet_id,
        .opcode = 0,
        .packet_len = strlen(username),
    };
    response_hton(&resp);
    client_write(client, &resp, sizeof(resp));
    client_write(client, username, strlen(username));
    free(username);
}
protocol_func_t userProtocolFuncs[] = {
    userGetUserInfo,
};
Protocol userProtocol = PROTOCOL("user", userProtocolFuncs);
