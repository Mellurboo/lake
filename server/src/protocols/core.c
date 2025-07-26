#include "protocol.h"
#include "protocols.h"
#include <stdint.h>
#include "request.h"
#include "response.h"
#include "client.h"
#include "pb.h"
#include <string.h>
#include <arpa/inet.h>

void coreGetProtocols(Client* client, Request* header) {
    for(size_t i = 0; i < protocols_count; ++i) {
        Response res_header;
        res_header.packet_id = header->packet_id;
        res_header.opcode = 0;
        res_header.packet_len = sizeof(uint32_t) + strlen(protocols[i]->name);
        response_hton(&res_header);
        pbwrite(&client->pb, &res_header, sizeof(res_header));
        uint32_t id = htonl(i);
        pbwrite(&client->pb, &id, sizeof(id));
        pbwrite(&client->pb, protocols[i]->name, strlen(protocols[i]->name));
        pbflush(&client->pb, client);
    }
    Response res_header;
    res_header.packet_id = header->packet_id;
    res_header.opcode = 0;
    res_header.packet_len = 0;
    response_hton(&res_header);
    pbwrite(&client->pb, &res_header, sizeof(res_header));
    pbflush(&client->pb, client);
}
protocol_func_t coreProtocolFuncs[] = {
    coreGetProtocols,
};

Protocol coreProtocol = PROTOCOL("CORE", coreProtocolFuncs);
