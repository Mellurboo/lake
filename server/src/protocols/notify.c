#include "notify.h"
#include "protocol.h"
#include "request.h"
#include "response.h"
#include "client.h"
#include "pb.h"

void notify(Client* client, Request* header) {
    // TODO: send some error here:
    if(header->packet_len != 0) return;
    client->notifyID = header->packet_id;
    Response resp = {
        .packet_id = header->packet_id,
        .opcode = 0,
        .packet_len = 0,
    };
    response_hton(&resp);
    pbwrite(&client->pb, &resp, sizeof(resp));
    pbflush(&client->pb, client);
}
protocol_func_t notifyProtocolFuncs[] = {
    notify,
};
Protocol notifyProtocol = PROTOCOL("notify", notifyProtocolFuncs);
