#include <stdio.h>
#include <gt.h>
#include <winnet.h>
#include <assert.h>
#include <string.h>

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
};

typedef void (*protocol_func_t)(int fd, Request* header);
typedef struct {
    const char* name;
    size_t funcs_count;
    protocol_func_t *funcs;
} Protocol;


void coreGetProtocols(int fd, Request* header);
protocol_func_t coreProtocolFuncs[] = {
    coreGetProtocols,
};

void echoEcho(int fd, Request* header) {
    (void)fd;
    (void)header;
    char buf[128];
    // TODO: send some error here:
    if(header->packet_len > sizeof(buf)) return;
    gtread_exact(fd, buf, header->packet_len);
    Response resp = {
        .packet_id = header->packet_id,
        .opcode = 0,
        .packet_len = header->packet_len
    };
    response_hton(&resp);
    gtwrite_exact(fd, &resp, sizeof(resp));
    gtwrite_exact(fd, buf, header->packet_len);
}
protocol_func_t echoProtocolFuncs[] = {
    echoEcho,
};

#define ARRAY_LEN(a) (sizeof(a)/sizeof(*(a)))
#define PROTOCOL(__name, __funcs) { .name = __name, .funcs_count = ARRAY_LEN(__funcs),  .funcs = __funcs }
Protocol protocols[] = {
    PROTOCOL("CORE", coreProtocolFuncs),
    PROTOCOL("echo", echoProtocolFuncs),
};
void coreGetProtocols(int fd, Request* header) {
    (void)fd;
    (void)header;
    fprintf(stderr, "GetProtocols\n");
    for(size_t i = 0; i < ARRAY_LEN(protocols); ++i) {
        Response res_header;
        res_header.packet_id = header->packet_id;
        res_header.opcode = 0;
        res_header.packet_len = sizeof(uint32_t) + strlen(protocols[i].name);
        response_hton(&res_header);
        gtwrite_exact(fd, &res_header, sizeof(res_header));
        uint32_t id = htonl(i);
        gtwrite_exact(fd, &id, sizeof(id));
        gtwrite_exact(fd, protocols[i].name, strlen(protocols[i].name));
    }
    Response res_header;
    res_header.packet_id = header->packet_id;
    res_header.opcode = 0;
    res_header.packet_len = 0;
    response_hton(&res_header);
    gtwrite_exact(fd, &res_header, sizeof(res_header));
}
void client_thread(void* fd_void) {

    int fd = (uintptr_t)fd_void;
    Request req_header;
    Response res_header;
    for(;;) {
        int n = gtread_exact(fd, &req_header, sizeof(req_header));
        if(n < 0) break;
        if(n == 0) break;
        request_ntoh(&req_header);
        if(req_header.protocol_id >= ARRAY_LEN(protocols)) {
            fprintf(stderr, "%d: Invalid protocol_id: %u\n", fd, req_header.protocol_id);
            res_header.packet_id = req_header.packet_id;
            res_header.opcode = -ERROR_INVALID_PROTOCOL_ID;
            res_header.packet_len = 0;
            response_hton(&res_header);
            gtwrite_exact(fd, &res_header, sizeof(res_header));
            continue;
        }
        Protocol* proto = &protocols[req_header.protocol_id];
        if(req_header.func_id >= proto->funcs_count) {
            fprintf(stderr, "%d: Invalid func_id: %u\n",  fd, req_header.func_id);
            res_header.packet_id = req_header.packet_id;
            res_header.opcode = -ERROR_INVALID_FUNC_ID;
            res_header.packet_len = 0;
            response_hton(&res_header);
            gtwrite_exact(fd, &res_header, sizeof(res_header));
            continue;
        }
        fprintf(stderr, "INFO: %d: %s func_id=%d\n", fd, proto->name, req_header.func_id);
        proto->funcs[req_header.func_id](fd, &req_header);
    }
    closesocket(fd);
    fprintf(stderr, "Disconnected!\n");
}
#define PORT 6969
// TODO: neterr() function that returns a string on last networking error
// TODO: error logging on networking error
int main(void) {
    gtinit();
    for(size_t i = 0; i < 20; ++i) gtyield();
    int server = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    // TODO: error logging on networking error
    if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        closesocket(server);
        return 1;
    }
    assert(server >= 0);
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    // TODO: error logging on networking error
    if(bind(server, (struct sockaddr*)&address, sizeof(address)) < 0) return 1;
    // TODO: error logging on networking error
    if(listen(server, 50) < 0) return 1;
    fprintf(stderr, "Started listening on: 0.0.0.0:%d\n", PORT);
    for(;;) {
        gtblockfd(server, GTBLOCKIN);
        int client_fd = accept(server, NULL, NULL);
        if(client_fd < 0) break;
        printf("Connected!\n");
        gtgo(client_thread, (void*)(uintptr_t)client_fd);
    }
    (void)server;
    closesocket(server);
    return 0;
}
