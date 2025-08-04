#include "protocol.h"
#include "request.h"
#include "response.h"
#include "client.h"
#include <arpa/inet.h>
#include "db_context.h"
#include "error.h"

void userGetUserIdFromHandle(Client* client, Request* header) {
    char handle[MAX_HANDLE_SIZE];
    if(header->packet_len > MAX_HANDLE_SIZE || header->packet_len == 0) {
        client_discard(client, header->packet_len);
        client_write_error(client, header->packet_id, ERROR_INVALID_PACKET_LEN);
        return;
    }
    intptr_t e = client_read(client, handle, header->packet_len);
    if(e <= 0) return;


}

protocol_func_t userHandleProtocolFuncs[] = {
    userGetUserIdFromHandle,
};
Protocol userHandleProtocol = PROTOCOL("userHandle", userHandleProtocolFuncs);
