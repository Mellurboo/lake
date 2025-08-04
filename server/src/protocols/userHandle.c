#include "protocol.h"
#include "request.h"
#include "response.h"
#include "client.h"
#include <arpa/inet.h>
#include "db_context.h"
#include "error.h"

extern DbContext* db;
void userGetUserIdFromHandle(Client* client, Request* header) {
    char handle[MAX_HANDLE_SIZE];
    if(header->packet_len > MAX_HANDLE_SIZE || header->packet_len == 0) {
        client_discard(client, header->packet_len);
        client_write_error(client, header->packet_id, ERROR_INVALID_PACKET_LEN);
        return;
    }
    intptr_t e = client_read(client, handle, header->packet_len);
    if(e <= 0) return;

    uint32_t id;
    e = DbContext_get_user_id_from_handle(db, handle, header->packet_len, &id);
    if(e < 0) {
        client_write_error(client, header->packet_id, ERROR_DB);
        return;
    }
    Response response = {
        .packet_id = header->packet_id,
        .opcode = 0,
        .packet_len = sizeof(id),
    };
    response_hton(&response);
    e = client_write(client, &response, sizeof(response));
    if(e <= 0) return;
    client_write(client, &id, sizeof(id));
}

protocol_func_t userHandleProtocolFuncs[] = {
    userGetUserIdFromHandle,
};
Protocol userHandleProtocol = PROTOCOL("userHandle", userHandleProtocolFuncs);
