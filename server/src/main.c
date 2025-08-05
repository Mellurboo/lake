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
#include "error.h"
// Global variables
DbContext* db = NULL;
UserMap user_map = { 0 };
struct list_head global_client_refs;

void client_thread(void* fd_void) {
    Client client = {.fd = (uintptr_t)fd_void, .userID = ~0, .notifyID = ~0, .secure = false};
    gtmutex_init(&client.write_mutex);
    gtmutex_init(&client.read_mutex);
    ClientRef client_ref = {
        .client = &client
    };
    list_init(&client.list);
    list_init(&client_ref.list);
    list_insert(&global_client_refs, &client_ref.list); 
    Request header;
    for(;;) {
        int n = client_read(&client, &header, sizeof(header));
        if(n < 0) break;
        if(n == 0) break;
        request_ntoh(&header);
        if(header.protocol_id >= protocols_count) {
            error("%d: Invalid protocol_id: %u", client.fd, header.protocol_id);
            client_write_error(&client, header.packet_id, ERROR_INVALID_PROTOCOL_ID);
            continue;
        }
        Protocol* proto = protocols[header.protocol_id];
        if(header.func_id >= proto->funcs_count) {
            error("%d: Invalid func_id: %u",  client.fd, header.func_id);
            client_write_error(&client, header.packet_id, ERROR_INVALID_FUNC_ID);
            continue;
        }

        if (client.userID == (uint32_t)-1 && header.protocol_id >= 2){
            error("%d: Not Authenticated", client.fd);
            client_write_error(&client, header.packet_id, ERROR_NOT_AUTH);
            continue;
        }

        info("%d: %s func_id=%d", client.fd, proto->name, header.func_id);
        proto->funcs[header.func_id](&client, &header);
    }
    list_remove(&client.list);
    list_remove(&client_ref.list);
    closesocket(client.fd);
    info("%d: Disconnected!", client.fd);
}
#define PORT 6969
int main(void) {
    gtinit();
    list_init(&global_client_refs);
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
