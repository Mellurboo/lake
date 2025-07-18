#include <stdio.h>
#include <gt.h>
#include <snet.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

typedef struct {
    uint32_t protocol_id;
    uint32_t func_id;
    uint32_t packet_id;
    uint32_t packet_len;
} Request;
void request_hton(Request* req) {
    req->protocol_id = htonl(req->protocol_id);
    req->func_id = htonl(req->func_id);
    req->packet_id = htonl(req->packet_id);
    req->packet_len = htonl(req->packet_len);
}

static intptr_t read_exact(uintptr_t fd, void* buf, size_t size) {
    while(size) {
        intptr_t e = recv(fd, buf, size, 0);
        if(e < 0) return e;
        if(e == 0) return 0; 
        buf = ((char*)buf) + (size_t)e;
        size -= (size_t)e;
    }
    return 1;
}
static intptr_t write_exact(uintptr_t fd, const void* buf, size_t size) {
    while(size) {
        intptr_t e = send(fd, buf, size, 0);
        if(e < 0) return e;
        if(e == 0) return 0; 
        buf = ((char*)buf) + (size_t)e;
        size -= (size_t)e;
    }
    return 1;
}
// Generic response header
typedef struct {
    uint32_t packet_id;
    uint32_t opcode;
    uint32_t packet_len;
} Response;
void response_ntoh(Response* res) {
    res->packet_id = ntohl(res->packet_id);
    res->opcode = ntohl(res->opcode);
    res->packet_len = ntohl(res->packet_len);
}
typedef struct {
    uint32_t id;
    char name[];
} Protocol;
#define PORT 6969
int main(void) {
    int client = socket(AF_INET, SOCK_STREAM, 0); 
    if(client < 0) {
        fprintf(stderr, "FATAL: Could not create server socket: %s\n", sneterr());
        return 1;
    }
    int opt = 1;
    if(setsockopt(client, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        fprintf(stderr, "FATAL: Could not set SO_REUSEADDR: %s\n", sneterr());
        closesocket(client);
        return 1;
    }
    const char* hostname = "localhost";
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    if(inet_pton(AF_INET, hostname, &server_addr.sin_addr) <= 0) {
        struct hostent *he = gethostbyname(hostname);
        if (he == NULL) {
            fprintf(stderr, "FATAL: Couldn't resolve hostname %s: %s\n", hostname, sneterr()); 
            return 1;
        }
        
        memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    if(connect(client, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "FATAL: Couldn't connect to %s %d: %s\n", hostname, PORT, sneterr());
        return 1;
    }

    size_t packet_id = 0;
    size_t get_extensions_id = packet_id++;
    int e = send(client, &(Request) { .protocol_id = 0, .func_id = 0, .packet_id = htonl(get_extensions_id), .packet_len = 0 }, sizeof(Request), 0);
    if(e < 0) {
        fprintf(stderr, "FATAL: Failed to send request: %s\n", sneterr());
        return 1;
    }
    assert(e == sizeof(Request));
    Response resp; 

    uint32_t echo_protocol_id = 0;
    for(;;) {
        e = read_exact(client, &resp, sizeof(resp));
        assert(e == 1);
        response_ntoh(&resp);
        assert(resp.packet_id == get_extensions_id);
        assert(resp.packet_len < 1024);
        if(resp.packet_len == 0) break;

        assert(resp.packet_len >= sizeof(uint32_t));
        Protocol* protocol = malloc(resp.packet_len + 1);
        assert(read_exact(client, protocol, resp.packet_len) == 1);
        protocol->id = ntohl(protocol->id);
        protocol->name[resp.packet_len-sizeof(uint32_t)] = '\0';
        if(((int)resp.opcode) < 0) {
            fprintf(stderr, "FATAL: Error on my response: %d\n", -((int)resp.opcode));
            return 1;
        }
        fprintf(stderr, "INFO: Protocol id=%u name=%s\n", protocol->id, protocol->name);
        if(strcmp(protocol->name, "echo") == 0) echo_protocol_id = protocol->id;
        free(protocol);
    }
    if(!echo_protocol_id) {
        fprintf(stderr, "FATAL: no echo protocol\n");
        return 1;
    } 
    fprintf(stderr, "Sent request successfully!\n");
    for(;;) {
        char buf[128];
        printf("> ");
        fflush(stdout);
        char* _ = fgets(buf, sizeof(buf), stdin);
        (void)_;
        if(strcmp(buf, ":quit\n") == 0) break;
        Request req = {
            .protocol_id = echo_protocol_id,
            .func_id = 0,
            .packet_id = 69,
            .packet_len = strlen(buf)
        };
        request_hton(&req);
        write_exact(client, &req, sizeof(req));
        write_exact(client, buf, strlen(buf));

        e = read_exact(client, &resp, sizeof(resp));
        assert(e == 1);
        response_ntoh(&resp);
        if(resp.packet_id != 69) {
            fprintf(stderr, "We got packet_id = %u -> %u\n", resp.packet_id, ntohl(resp.packet_id));
            return 1;
        }
        assert(resp.packet_len == strlen(buf));

        int n = read_exact(client, buf, resp.packet_len);
        if(n <= 0) {
            fprintf(stderr, "ERROR: failed to read response: %s\n", sneterr());
            break;
        }
        printf("%.*s", resp.packet_len, buf);
    }
    closesocket(client);
    return 0;
}
