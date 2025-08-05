#include "protocol.h"
#include "protocols.h"
#include <stdint.h>
#include "request.h"
#include "response.h"
#include "client.h"
#include <string.h>
#include <arpa/inet.h>
#include "assert.h"

void coreGetProtocols(Client* client, Request* header) {
    // FIXME: check size
    for(size_t i = 0; i < protocols_count; ++i) {
        Response res_header;
        res_header.packet_id = header->packet_id;
        res_header.opcode = 0;
        res_header.packet_len = sizeof(uint32_t) + strlen(protocols[i]->name);
        response_hton(&res_header);
        uint32_t id = htonl(i);
        client_lock_write(client);
            client_write(client, &res_header, sizeof(res_header));
            client_write(client, &id, sizeof(id));
            assert((!client->secure) && "Fix https://github.com/F1L1Pv2/lake/issues/6");
            client_write(client, (void*)protocols[i]->name, strlen(protocols[i]->name));
        client_unlock_write(client);
    }
    Response res_header;
    res_header.packet_id = header->packet_id;
    res_header.opcode = 0;
    res_header.packet_len = 0;
    response_hton(&res_header);
    client_write_scoped(client) {
        client_write(client, &res_header, sizeof(res_header));
    }
}
protocol_func_t coreProtocolFuncs[] = {
    coreGetProtocols,
};

Protocol coreProtocol = PROTOCOL("CORE", coreProtocolFuncs);
