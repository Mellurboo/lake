#include "protocol.h"
#include "request.h"
#include "response.h"
#include "client.h"

void echoEcho(Client* client, Request* header) {
    char buf[128];
    // TODO: send some error here:
    if(header->packet_len > sizeof(buf)) return;
    client_read(client, buf, header->packet_len);
    Response resp = {
        .packet_id = header->packet_id,
        .opcode = 0,
        .packet_len = header->packet_len
    };
    response_hton(&resp);
    client_write_scoped(client) {
        client_write(client, &resp, sizeof(resp));
        client_write(client, buf, header->packet_len);
    }
}
protocol_func_t echoProtocolFuncs[] = {
    echoEcho,
};
Protocol echoProtocol = PROTOCOL("echo", echoProtocolFuncs);
