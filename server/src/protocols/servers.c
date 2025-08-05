#include "protocol.h"
#include "request.h"
#include "response.h"
#include "client.h"
#include "servers.h"
#include "db_context.h"
#include <arpa/inet.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "error.h"

typedef struct {
    uint32_t server_id;
    /*name[...]*/
} GetServersResponse;
extern DbContext* db;
void getServers(Client* client, Request* header) {
    if(header->packet_len > 0) {
        client_discard(client, header->packet_len);
        client_write_error(client, header->packet_id, ERROR_INVALID_PACKET_LEN);
        return;
    }

    Servers servers = {0};

    int e = DbContext_get_servers(db, &servers);
    if(e < 0) goto err_get_servers;

    for(size_t i = 0; i < servers.len; ++i){
        Server* server = &servers.items[i];
        assert(server->name);
        Response resp = {
            .packet_id = header->packet_id,
            .opcode = 1,
            .packet_len = sizeof(GetServersResponse) + strlen(server->name),
        };
        response_hton(&resp);
        client_write(client, &resp, sizeof(Response));
        GetServersResponse get_servers_response = {
            .server_id = htonl(server->id),
        };
        client_write(client, &get_servers_response, sizeof(get_servers_response));
        client_write(client, server->name, strlen(server->name));
    }

    Response resp = {
        .packet_id = header->packet_id,
        .opcode = 0,
        .packet_len = 0
    };
    response_hton(&resp);
    client_write(client, &resp, sizeof(Response));
    free_servers(&servers);
    return;
err_get_servers:
    free_servers(&servers);
    client_write_error(client, header->packet_id, ERROR_DB);
    return;
}

protocol_func_t serversProtocolFuncs[] = {
    getServers,
};
Protocol serversProtocol = PROTOCOL("servers", serversProtocolFuncs);
