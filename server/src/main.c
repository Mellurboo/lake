#include <stdio.h>
#include <gt.h>
#include <snet.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <list_head.h>
#include "darray.h"
#include "sqlite3/sqlite3.h"
#include "fileutils.h"
#include "db_context.h"
#include "client.h"
#include "user_map.h"
#include "log.h"
#include "request.h"
#include "response.h"
#include "protocols.h"
#include "protocols/protocol.h"
// Global variables
DbContext* db = NULL;
UserMap user_map = { 0 };

enum {
    ERROR_INVALID_PROTOCOL_ID = 1,
    ERROR_INVALID_FUNC_ID,
    ERROR_NOT_AUTH
};

void client_thread(void* fd_void) {
    Client client = {.fd = (uintptr_t)fd_void, .userID = ~0, .notifyID = ~0, .secure = false};
    list_init(&client.list);
    Request req_header;
    Response res_header;
    for(;;) {
        int n = client_read(&client, &req_header, sizeof(req_header));
        if(n < 0) break;
        if(n == 0) break;
        request_ntoh(&req_header);
        if(req_header.protocol_id >= protocols_count) {
            error("%d: Invalid protocol_id: %u", client.fd, req_header.protocol_id);
            res_header.packet_id = req_header.packet_id;
            res_header.opcode = -ERROR_INVALID_PROTOCOL_ID;
            res_header.packet_len = 0;
            response_hton(&res_header);
            client_write(&client, &res_header, sizeof(res_header));
            continue;
        }
        Protocol* proto = protocols[req_header.protocol_id];
        if(req_header.func_id >= proto->funcs_count) {
            error("%d: Invalid func_id: %u",  client.fd, req_header.func_id);
            res_header.packet_id = req_header.packet_id;
            res_header.opcode = -ERROR_INVALID_FUNC_ID;
            res_header.packet_len = 0;
            response_hton(&res_header);
            client_write(&client, &res_header, sizeof(res_header));
            continue;
        }

        if (client.userID == (uint32_t)-1 && req_header.protocol_id >= 2){
            error("%d: Not Authenticated", client.fd);
            res_header.packet_id = req_header.packet_id;
            res_header.opcode = -ERROR_NOT_AUTH;
            res_header.packet_len = 0;
            response_hton(&res_header);
            client_write(&client, &res_header, sizeof(res_header));
            continue;
        }

        info("%d: %s func_id=%d", client.fd, proto->name, req_header.func_id);
        proto->funcs[req_header.func_id](&client, &req_header);
    }
    list_remove(&client.list);
    closesocket(client.fd);
    info("%d: Disconnected!", client.fd);
}
#define PORT 6969
int main(void) {
    gtinit();
    if(DbContext_init(&db) < 0){
        error("Couldn't initialize database context\n");
        return 1;
    }
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if(server < 0) {
        fatal("Could not create server socket: %s", sneterr());
        return 1;
    }
    int opt = 1;
    if(setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (void*)&opt, sizeof(opt)) < 0) {
        fatal("Could not set SO_REUSEADDR: %s", sneterr());
        closesocket(server);
        return 1;
    }
    assert(server >= 0);
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    if(bind(server, (struct sockaddr*)&address, sizeof(address)) < 0) {
        fatal("Could not bind server: %s", sneterr());
        return 1;
    }
    if(listen(server, 50) < 0) {
        fatal("Could not listen on server: %s", sneterr());
        return 1;
    }
    info("Started listening on: localhost:%d", PORT);
    for(;;) {
        gtblockfd(server, GTBLOCKIN);
        int client_fd = accept(server, NULL, NULL);
        if(client_fd < 0) break;
        info("%d: connected", client_fd);
        gtgo(client_thread, (void*)(uintptr_t)client_fd);
    }
    (void)server;
    closesocket(server);
    return 0;
}
