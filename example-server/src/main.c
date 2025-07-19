#include <stdio.h>
#include <gt.h>
#include <snet.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
# define NEWLINE "\n\r"
#else
# define NEWLINE "\n"
#endif
#define log(level, ...) (fprintf(stderr, # level ": " __VA_ARGS__), fprintf(stderr, NEWLINE))
#define trace(...) log(TRACE, __VA_ARGS__)
#define info(...) log(INFO, __VA_ARGS__)
#define warn(...) log(WARN, __VA_ARGS__)
#define error(...) log(ERROR, __VA_ARGS__)
#define fatal(...) log(FATAL, __VA_ARGS__)

static intptr_t gtread_exact(uintptr_t fd, void* buf, size_t size) {
    while(size) {
        gtblockfd(fd, GTBLOCKIN);
        intptr_t e = recv(fd, buf, size, 0);
        if(e < 0) return e;
        if(e == 0) return 0; 
        buf = ((char*)buf) + (size_t)e;
        size -= (size_t)e;
    }
    return 1;
}
static intptr_t gtwrite_exact(uintptr_t fd, const void* buf, size_t size) {
    while(size) {
        gtblockfd(fd, GTBLOCKOUT);
        intptr_t e = send(fd, buf, size, 0);
        if(e < 0) return e;
        if(e == 0) return 0; 
        buf = ((char*)buf) + (size_t)e;
        size -= (size_t)e;
    }
    return 1;
}

// Generic request header
typedef struct {
    uint32_t protocol_id;
    uint32_t func_id;
    uint32_t packet_id;
    uint32_t packet_len;
} Request;
void request_ntoh(Request* req) {
    req->protocol_id = ntohl(req->protocol_id);
    req->func_id = ntohl(req->func_id);
    req->packet_id = ntohl(req->packet_id);
    req->packet_len = ntohl(req->packet_len);
}
// Generic response header
typedef struct {
    uint32_t packet_id;
    uint32_t opcode;
    uint32_t packet_len;
} Response;
void response_hton(Response* res) {
    res->packet_id = htonl(res->packet_id);
    res->opcode = htonl(res->opcode);
    res->packet_len = htonl(res->packet_len);
}
enum {
    ERROR_INVALID_PROTOCOL_ID = 1,
    ERROR_INVALID_FUNC_ID,
    ERROR_NOT_AUTH
};

typedef struct{
    int fd;
    uint32_t userID;
} Client;

typedef void (*protocol_func_t)(Client* client, Request* header);
typedef struct {
    const char* name;
    size_t funcs_count;
    protocol_func_t *funcs;
} Protocol;


void coreGetProtocols(Client* client, Request* header);
protocol_func_t coreProtocolFuncs[] = {
    coreGetProtocols,
};

void echoEcho(Client* client, Request* header) {
    char buf[128];
    // TODO: send some error here:
    if(header->packet_len > sizeof(buf)) return;
    gtread_exact(client->fd, buf, header->packet_len);
    Response resp = {
        .packet_id = header->packet_id,
        .opcode = 0,
        .packet_len = header->packet_len
    };
    response_hton(&resp);
    gtwrite_exact(client->fd, &resp, sizeof(resp));
    gtwrite_exact(client->fd, buf, header->packet_len);
}
protocol_func_t echoProtocolFuncs[] = {
    echoEcho,
};
void authAuthenticate(Client* client, Request* header);
protocol_func_t authProtocolFuncs[] = {
    authAuthenticate,
};

typedef struct {
    uint32_t server_id;
    uint32_t channel_id;
    char msg[];
} SendMsgPacket;
void sendMsgPacket_ntoh(SendMsgPacket* packet) {
    packet->server_id = ntohl(packet->server_id);
    packet->channel_id = ntohl(packet->channel_id);
}
#define MAX_MESSAGE 10000
void sendMsg(Client* client, Request* header) {
    // NOTE: we hard assert its MORE because you need at least 1 character per message
    // TODO: send some error here:
    if(header->packet_len <= sizeof(SendMsgPacket)) return;
    size_t msg_len = header->packet_len - sizeof(SendMsgPacket);
    // TODO: send some error here:
    if(msg_len > MAX_MESSAGE) return;
    SendMsgPacket* msg = malloc(header->packet_len);
    // TODO: send some error here:
    if(!msg) return;
    int n = gtread_exact(client->fd, msg, header->packet_len);
    // TODO: send some error here:
    if(n < 0 || n == 0) goto err_read;
    fprintf(stderr, "TBD: Send message: %.*s\n", (int)msg_len, msg->msg);
    Response resp = {
        .packet_id = header->packet_id,
        .opcode = 0,
        .packet_len = 0
    };
    response_hton(&resp);
    gtwrite_exact(client->fd, &resp, sizeof(resp));
err_read:
    free(msg);
    return;
}
protocol_func_t msgProtoclFuncs[] = {
    sendMsg,
};
#define ARRAY_LEN(a) (sizeof(a)/sizeof(*(a)))
#define PROTOCOL(__name, __funcs) { .name = __name, .funcs_count = ARRAY_LEN(__funcs),  .funcs = __funcs }
Protocol protocols[] = {
    PROTOCOL("CORE", coreProtocolFuncs),
    PROTOCOL("auth", authProtocolFuncs),

    // CORE and auth need to be first in this order otherwise auth logic wont work

    PROTOCOL("echo", echoProtocolFuncs),
    PROTOCOL("msg", msgProtoclFuncs),
};
void coreGetProtocols(Client* client, Request* header) {
    fprintf(stderr, "GetProtocols\n");
    for(size_t i = 0; i < ARRAY_LEN(protocols); ++i) {
        Response res_header;
        res_header.packet_id = header->packet_id;
        res_header.opcode = 0;
        res_header.packet_len = sizeof(uint32_t) + strlen(protocols[i].name);
        response_hton(&res_header);
        gtwrite_exact(client->fd, &res_header, sizeof(res_header));
        uint32_t id = htonl(i);
        gtwrite_exact(client->fd, &id, sizeof(id));
        gtwrite_exact(client->fd, protocols[i].name, strlen(protocols[i].name));
    }
    Response res_header;
    res_header.packet_id = header->packet_id;
    res_header.opcode = 0;
    res_header.packet_len = 0;
    response_hton(&res_header);
    gtwrite_exact(client->fd, &res_header, sizeof(res_header));
}

void authAuthenticate(Client* client, Request* header){
    fprintf(stderr, "Authenticate\n");

    // TODO: send some error here:
    if(header->packet_len != sizeof(uint32_t)) return;
    gtread_exact(client->fd, &client->userID, sizeof(uint32_t));
    client->userID = ntohl(client->userID);

    Response res_header;
    res_header.packet_id = header->packet_id;
    res_header.opcode = 0;
    res_header.packet_len = 0;
    response_hton(&res_header);
    gtwrite_exact(client->fd, &res_header, sizeof(res_header));
}

void client_thread(void* fd_void) {
    Client client = {.fd = (uintptr_t)fd_void, .userID = (uint32_t)-1};

    Request req_header;
    Response res_header;
    for(;;) {
        int n = gtread_exact(client.fd, &req_header, sizeof(req_header));
        if(n < 0) break;
        if(n == 0) break;
        request_ntoh(&req_header);
        if(req_header.protocol_id >= ARRAY_LEN(protocols)) {
            fprintf(stderr, "%d: Invalid protocol_id: %u\n", client.fd, req_header.protocol_id);
            res_header.packet_id = req_header.packet_id;
            res_header.opcode = -ERROR_INVALID_PROTOCOL_ID;
            res_header.packet_len = 0;
            response_hton(&res_header);
            gtwrite_exact(client.fd, &res_header, sizeof(res_header));
            continue;
        }
        Protocol* proto = &protocols[req_header.protocol_id];
        if(req_header.func_id >= proto->funcs_count) {
            fprintf(stderr, "%d: Invalid func_id: %u\n",  client.fd, req_header.func_id);
            res_header.packet_id = req_header.packet_id;
            res_header.opcode = -ERROR_INVALID_FUNC_ID;
            res_header.packet_len = 0;
            response_hton(&res_header);
            gtwrite_exact(client.fd, &res_header, sizeof(res_header));
            continue;
        }

        if (client.userID == (uint32_t)-1 && req_header.protocol_id >= 2){
            fprintf(stderr, "%d: Not Authenticated\n", client.fd);
            res_header.packet_id = req_header.packet_id;
            res_header.opcode = -ERROR_NOT_AUTH;
            res_header.packet_len = 0;
            response_hton(&res_header);
            gtwrite_exact(client.fd, &res_header, sizeof(res_header));
            continue;
        }

        fprintf(stderr, "INFO: %d: %s func_id=%d\n", client.fd, proto->name, req_header.func_id);
        proto->funcs[req_header.func_id](&client, &req_header);
    }
    closesocket(client.fd);
    fprintf(stderr, "Disconnected!\n");
}
#define PORT 6969
int main(void) {
    gtinit();
    for(size_t i = 0; i < 20; ++i) gtyield();
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
