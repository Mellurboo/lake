#include "lastRead.h"
#include "protocol.h"
#include "request.h"
#include "response.h"
#include "client.h"
#include "db_context.h"
#include <arpa/inet.h>

typedef struct{
    uint32_t server_id;
    uint32_t channel_id;
    uint32_t milis_low;
    uint32_t milis_high;
} LastReadRequest;

void LastReadRequest_ntoh(LastReadRequest* request){
    request->server_id = ntohl(request->server_id);
    request->channel_id = ntohl(request->channel_id);
    request->milis_low = ntohl(request->milis_low);
    request->milis_high = ntohl(request->milis_high);
}
extern DbContext* db;
void lastRead(Client* client, Request* header) {
    if(header->packet_len != sizeof(LastReadRequest)) goto err;
    LastReadRequest request = {0};
    int e = client_read(client, &request, sizeof(request));
    if(e <= 0) goto err;
    LastReadRequest_ntoh(&request);

    uint64_t milis = (uint64_t)request.milis_low | ((uint64_t)request.milis_high) << 32;

    e = DbContext_set_last_read(db, client->userID, request.server_id, request.channel_id, milis);
    if(e < 0) goto err;

    {
        Response resp = {
            .packet_id = header->packet_id,
            .opcode = 0,
            .packet_len = 0,
        };
        response_hton(&resp);
        client_write_scoped(client) client_write(client, &resp, sizeof(resp));
    }
    return;
err:
    {
        Response resp = {
            .packet_id = header->packet_id,
            .opcode = 1,
            .packet_len = 0,
        };
        response_hton(&resp);
        client_write_scoped(client) client_write(client, &resp, sizeof(resp));
    }
    return;
}
protocol_func_t lastReadProtocolFuncs[] = {
    lastRead,
};
Protocol lastReadProtocol = PROTOCOL("lastRead", lastReadProtocolFuncs);
