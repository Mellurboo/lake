#include <stdio.h>
#include <gt.h>
#include <snet.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stui.h>
#include <darray.h>
#include <signal.h>
#include "fileutils.h"
#include "post_quantum_cryptography.h"
#include <stdbool.h>

#ifdef _WIN32
#else
# include <time.h>
#endif

#define ARRAY_LEN(a) (sizeof(a)/sizeof(*(a)))

uint64_t time_unix_milis(void) {
#ifdef _WIN32
    // TODO: bruvsky pls implement this for binbows opewating system for video james
    return 0;
#else
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
#endif
}

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
typedef struct {
    uint32_t server_id;
    uint32_t channel_id;
} SendMsgRequest;
void sendMsgRequest_hton(SendMsgRequest* packet) {
    packet->server_id = htonl(packet->server_id);
    packet->channel_id = htonl(packet->channel_id);
}

typedef struct Client Client;
typedef intptr_t (*client_read_func_t)(Client* client, void* buf, size_t size);
typedef intptr_t (*client_write_func_t)(Client* client, const void* buf, size_t size);
#define AES_PAD(n) (((n + (AES_BLOCKLEN - 1)) / AES_BLOCKLEN) * AES_BLOCKLEN)
#define WRITE_BUFFER_CAPACITY AES_PAD(4096)
#define READ_BUFFER_CAPACITY AES_PAD(4096)
struct Client {
    int fd;
    client_read_func_t read;
    client_write_func_t write;

    uint32_t write_buffer_head;
    uint8_t write_buffer[WRITE_BUFFER_CAPACITY];
    uint32_t read_buffer_head;
    // TODO: ring buffer for read_buffer
    uint8_t read_buffer[READ_BUFFER_CAPACITY];
    struct AES_ctx aes_ctx;
};
// NOTE: kinda assumed but we read exact amount of bytes.
static intptr_t client_read(Client* client, void* buf, size_t size) {
    intptr_t e;
    while(size) {
        size_t n = size < client->read_buffer_head ? size : client->read_buffer_head;
        memcpy(buf, client->read_buffer, n);
        memcpy(client->read_buffer, client->read_buffer + n, READ_BUFFER_CAPACITY - n);
        buf = ((char*)buf) + (size_t)n;
        size -= (size_t)n;
        client->read_buffer_head -= n;
        if(size) {
            assert(client->read_buffer_head == 0);
            e = client->read(client, client->read_buffer, READ_BUFFER_CAPACITY);
            if(e <= 0) return e;
            client->read_buffer_head += (size_t)e;
        }
    }
    return 1;
}
static intptr_t client_wflush(Client* client) {
    if(client->write_buffer_head == 0) return 1;
    intptr_t e = client->write(client, client->write_buffer, client->write_buffer_head);
    client->write_buffer_head = 0;
    return e;
}
// NOTE: kinda assumed but we write exact amount of bytes
static intptr_t client_write(Client* client, const void* buf, size_t size) {
    intptr_t e;
    while(size) {
        size_t buffer_remain = WRITE_BUFFER_CAPACITY - client->write_buffer_head;
        size_t n = size < buffer_remain ? size : buffer_remain;
        memcpy(client->write_buffer + client->write_buffer_head, buf, n);
        client->write_buffer_head += n;
        size -= n;
        buf = ((char*)buf) + n;
        if(client->write_buffer_head == WRITE_BUFFER_CAPACITY) {
            e = client_wflush(client);
            if(e <= 0) return e;
        }
    }
    return 1;
}
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
static intptr_t unsecure_gtread(Client* client, void* buf, size_t size) {
    fprintf(stderr, "We start blocking...\n");
    gtblockfd(client->fd, GTBLOCKIN);
    fprintf(stderr, "We blocked!\n");
    intptr_t e = recv(client->fd, buf, size, 0);
    fprintf(stderr, "We received: %ld\n", e);
    return e;
}

static intptr_t gtwrite_exact(int fd, const void* buf, size_t size) {
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
static intptr_t unsecure_gtwrite_exact(Client* client, const void* buf, size_t size) {
    return gtwrite_exact(client->fd, buf, size);
}
typedef struct {
    uint32_t server_id;
    uint32_t channel_id;
    uint32_t milis_low;
    uint32_t milis_high;
    uint32_t count;
} MessagesBeforeRequest;
void messagesBeforeRequest_hton(MessagesBeforeRequest* packet) {
    packet->server_id = htonl(packet->server_id);
    packet->channel_id = htonl(packet->channel_id);
    packet->milis_low = htonl(packet->milis_low);
    packet->milis_high = htonl(packet->milis_high);
    packet->count = htonl(packet->count);
}
typedef struct {
    uint32_t author_id;
    uint32_t milis_low;
    uint32_t milis_high;
    /*content[packet_len - sizeof(MessagesBeforeResponse)]*/
} MessagesBeforeResponse;
void messagesBeforeResponse_ntoh(MessagesBeforeResponse* packet) {
    packet->author_id = ntohl(packet->author_id);
    packet->milis_low = ntohl(packet->milis_low);
    packet->milis_high = ntohl(packet->milis_high);
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
typedef struct {
    uint64_t milis;
    char* author_name;
    uint32_t content_len;
    char* content;
} Message;

typedef struct {
    uint16_t l, t, r, b;
} UIBox;
inline UIBox content_box(size_t term_width, size_t term_height) {
    return (UIBox) { .l = 1, .t = 1, .r = term_width - 1, .b = term_height - 1 };
}


typedef struct {
    Message* items;
    size_t len, cap;
} Messages;
void render_messages(UIBox box, Messages* msgs) {
    size_t box_width = box.r - box.l;
    int32_t y_offset_start = box.b - box.t - 1;
    for(size_t i = msgs->len; i > 0; --i) {
        Message* msg = &msgs->items[i-1];

        char *content_cursor = msg->content, *content_end = msg->content + msg->content_len;

        char prefix[256];
        time_t msg_time_secs = msg->milis / 1000;
        struct tm* msg_time = localtime(&msg_time_secs);
        size_t prefix_len = snprintf(prefix, sizeof(prefix), "<%02d/%02d/%02d> %s: ", msg_time->tm_mday, msg_time->tm_mon + 1, msg_time->tm_year % 100, msg->author_name);
        size_t msg_lines = (prefix_len + msg->content_len + (box_width - 1)) / box_width;
        y_offset_start -= msg_lines;
        char* prefix_cursor = prefix;

        size_t y_offset_offset_start = 0;
        size_t x_offset_start = 0;
        while(*prefix_cursor) {
            if(x_offset_start >= box_width) {
                x_offset_start = 0;
                y_offset_offset_start++;
            }
            stui_putchar(box.l + x_offset_start, box.t + y_offset_start + y_offset_offset_start, *prefix_cursor);
            x_offset_start++;
            prefix_cursor++;
        }
        for(size_t j = y_offset_offset_start; j < msg_lines; ++j, content_cursor += box_width - x_offset_start) {
            if(y_offset_start + (int32_t)j < 0) continue;
            for(size_t x_offset = 0; x_offset < box_width - x_offset_start && content_cursor + x_offset < content_end; ++x_offset) {
                stui_putchar(box.l + x_offset_start + x_offset, box.t + y_offset_start + (int32_t)j, content_cursor[x_offset]);
            }
            x_offset_start = 0;
        }
        if(y_offset_start < 0) break;
    }
}

inline Message cstr_msg(const char* author, const char* content) {
    return (Message){.milis = time_unix_milis(), .author_name = (char*)author, .content_len = strlen(content), .content = (char*)content };
}
static stui_term_flag_t prev_term_flags = 0;
void restore_prev_term(void) {
    stui_term_set_flags(prev_term_flags);
}
typedef struct {
    char* items;
    size_t len, cap;
} StringBuilder;
typedef struct IncomingEvent IncomingEvent;
typedef void (*event_handler_t)(Client* client, Response* response, IncomingEvent* event);
struct IncomingEvent {
    event_handler_t onEvent;
    union {
        struct { Messages* msgs; Message msg; } onMessage;
        struct { Messages* msgs; } onNotification;
    } as;
};
#define MAX_INCOMING_EVENTS 128
static IncomingEvent incoming_events[MAX_INCOMING_EVENTS];
size_t allocate_incoming_event(void) {
    for(;;) {
        for(size_t i = 0; i < MAX_INCOMING_EVENTS; ++i) {
            if(!incoming_events[i].onEvent) return i;
        }
        // TODO: we should introduce gtsemaphore
        // that way we can notify on event completion
        // and wait for it in the green threads
        gtyield();
    }
    // unreachable
    // return ~0;
}
void reader_thread(void* client_void) {
    Client* client = client_void;
    int e;
    for(;;) {
        Response resp; 
        e = client_read(client, &resp, sizeof(resp));
        if(e != 1) break;
        response_ntoh(&resp);
        // TODO: skip resp.len amount maybe?
        if(resp.packet_id >= MAX_INCOMING_EVENTS) {
            fprintf(stderr, "We got some bogus event with packet_id: %u\n", resp.packet_id);
            continue;
        }
        if(!incoming_events[resp.packet_id].onEvent) {
            fprintf(stderr, "We got some unhandled (bogus?) event with packet_id: %u\n", resp.packet_id);
            continue;
        }
        incoming_events[resp.packet_id].onEvent(client, &resp, &incoming_events[resp.packet_id]);
    }
    fprintf(stderr, "Some sort of IO error occured. I don't know how to handle this right now. Probably just disconnect\n");
    fprintf(stderr, "ERROR: %s", sneterr());
    exit(1);
}

void redraw(void);
void okOnMessage(Client* client, Response* response, IncomingEvent* event) {
    (void)client;
    (void)response;
    da_push(event->as.onMessage.msgs, event->as.onMessage.msg);
    event->onEvent = NULL;
    redraw();
}
typedef struct {
    uint32_t server_id;
    uint32_t channel_id;
    uint32_t author_id;
    uint32_t milis_low;
    uint32_t milis_high;
} Notification;
void notification_ntoh(Notification* packet) {
    packet->server_id = ntohl(packet->server_id);
    packet->channel_id = ntohl(packet->channel_id);
    packet->author_id = ntohl(packet->author_id);
    packet->milis_low = ntohl(packet->milis_low);
    packet->milis_high = ntohl(packet->milis_high);
}
// TODO: unhardcode this and local user hashmap.
static const char* author_names[] = {
    "f1l1p",
    "dcraftbg"
};
void onNotification(Client* client, Response* response, IncomingEvent* event) {
    // SKIP
    if(response->packet_len == 0) return;
    assert(response->opcode == 0);
    assert(response->packet_len > sizeof(Notification));
    Notification notif;

    // TODO: error check
    client_read(client, &notif, sizeof(notif));
    notification_ntoh(&notif);
    size_t content_len = response->packet_len - sizeof(Notification);
    char* content = malloc(content_len);
    // TODO: error check
    client_read(client, content, content_len);
    uint64_t milis = (((uint64_t)notif.milis_high) << 32) | (uint64_t)notif.milis_low;
    Message msg = {
        .content_len = content_len,
        .content = content,
        .milis = milis,
        .author_name = (char*)(notif.author_id < ARRAY_LEN(author_names) ? author_names[notif.author_id] : "UNKNOWN")
    };
    da_push(event->as.onNotification.msgs, msg);
    redraw();
}
uint32_t dming;
Messages msgs;
size_t term_width, term_height;
StringBuilder prompt = { 0 };
void redraw(void) {
    for(size_t y = 0; y < term_height; ++y) {
        for(size_t x = 0; x < term_width; ++x) {
            stui_putchar(x, y, ' ');
        }
    }
    stui_window_border(0, 0, term_width - 1, term_height - 2, '=', '|', '+');
    char buf[128];
    snprintf(buf, sizeof(buf), "DMs: %s", dming < ARRAY_LEN(author_names) ? author_names[dming] : "UNKNOWN");
    {
        char* str = buf;
        size_t x = 2;
        while(*str) stui_putchar(x++, 0, *str++);
    }
    render_messages(content_box(term_width, term_height), &msgs);
    stui_putchar(0, term_height - 1, '>');
    stui_putchar(1, term_height - 1, ' ');
    size_t x = 2;
    for(size_t i = 2; i < term_width - 1; ++i) {
        stui_putchar(i, term_height - 1, ' ');
    }
    for(; x < prompt.len + 2; ++x) {
        if(x > term_width - 1) continue;
        stui_putchar(0 + x, term_height - 1, prompt.items[x - 2]);
    }
    stui_refresh();
    stui_goto(x, term_height - 1);
}
const char* shift_args(int *argc, const char ***argv) {
    if((*argc) <= 0) return NULL;
    return ((*argc)--, *((*argv)++));
}
#ifndef _MINOS
void _interrupt_handler_resize(int sig) {
    (void)sig;
    stui_term_get_size(&term_width, &term_height);
    stui_setsize(term_width, term_height);
    stui_clear();
    redraw();
}
#endif
void register_signals(void) {
#ifndef _MINOS
    signal(SIGWINCH, _interrupt_handler_resize);
#endif
}
int main(int argc, const char** argv) {
    register_signals();
    gtinit();
    prev_term_flags = stui_term_get_flags();
    atexit(restore_prev_term);
    static Client client = { 0 };
    client.read = unsecure_gtread;
    client.write = unsecure_gtwrite_exact;
    client.fd = socket(AF_INET, SOCK_STREAM, 0); 
    if(client.fd < 0) {
        fprintf(stderr, "FATAL: Could not create server socket: %s\n", sneterr());
        return 1;
    }
    int opt = 1;
    if(setsockopt(client.fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        fprintf(stderr, "FATAL: Could not set SO_REUSEADDR: %s\n", sneterr());
        closesocket(client.fd);
        return 1;
    }
    const char* hostname = "localhost";
    uint32_t port = 6969;

    char public_key_name[256] = {0};
    char secret_key_name[256] = {0};
    uint8_t* pk = NULL;
    uint8_t* sk = NULL;

    while(argc) {
        const char* arg = shift_args(&argc, &argv);
        if(strcmp(arg, "-p") == 0) {
            assert(argc && "PORT PLS MOTHERFUCKA");
            port = atoi(shift_args(&argc, &argv));
        } else if(strcmp(arg, "-ip") == 0) {
            assert(argc && "IP PLS MOTHERFUCKA");
            hostname = shift_args(&argc, &argv);
        } else if(strcmp(arg, "-key") == 0){
            arg = shift_args(&argc, &argv);
            snprintf(public_key_name, sizeof(public_key_name), "%s.pub", arg);
            snprintf(secret_key_name, sizeof(secret_key_name), "%s.priv", arg);
        }
    }
    if(public_key_name[0] != 0){
        size_t read_size = 0;
        pk = (uint8_t*)read_entire_file(public_key_name, &read_size);
        if(read_size != KYBER_PUBLICKEYBYTES || pk == NULL){
            fprintf(stderr, "Provide valid public key!\n");
            return 1;
        }
    }

    if(secret_key_name[0] != 0){
        size_t read_size = 0;
        sk = (uint8_t*)read_entire_file(secret_key_name, &read_size);
        if(read_size != KYBER_SECRETKEYBYTES || sk == NULL){
            fprintf(stderr, "Provide valid sekret key!\n");
            return 1;
        }
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if(inet_pton(AF_INET, hostname, &server_addr.sin_addr) <= 0) {
        struct hostent *he = gethostbyname(hostname);
        if (he == NULL) {
            fprintf(stderr, "FATAL: Couldn't resolve hostname %s: %s\n", hostname, sneterr()); 
            return 1;
        }
        
        memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    if(connect(client.fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "FATAL: Couldn't connect to %s %d: %s\n", hostname, PORT, sneterr());
        return 1;
    }

    size_t packet_id = 0;
    size_t get_extensions_id = packet_id++;
    int e = client_write(&client, &(Request) { .protocol_id = 0, .func_id = 0, .packet_id = htonl(get_extensions_id), .packet_len = 0 }, sizeof(Request));
    if(e < 0) {
        fprintf(stderr, "FATAL: Failed to send request: %s\n", sneterr());
        return 1;
    }
    client_wflush(&client);
    Response resp; 
    uint32_t auth_protocol_id = 0;
    // uint32_t echo_protocol_id = 0;
    uint32_t msg_protocol_id = 0;
    uint32_t notify_protocol_id = 0;
    for(;;) {
        e = client_read(&client, &resp, sizeof(resp));
        assert(e == 1);
        response_ntoh(&resp);
        assert(resp.packet_id == get_extensions_id);
        assert(resp.packet_len < 1024);
        if(resp.packet_len == 0) break;

        assert(resp.packet_len >= sizeof(uint32_t));
        Protocol* protocol = malloc(resp.packet_len + 1);
        assert(client_read(&client, protocol, resp.packet_len) == 1);
        protocol->id = ntohl(protocol->id);
        protocol->name[resp.packet_len-sizeof(uint32_t)] = '\0';
        if(((int)resp.opcode) < 0) {
            fprintf(stderr, "FATAL: Error on my response: %d\n", -((int)resp.opcode));
            return 1;
        }
        fprintf(stderr, "INFO: Protocol id=%u name=%s\n", protocol->id, protocol->name);
        // if(strcmp(protocol->name, "echo") == 0) echo_protocol_id = protocol->id;
        if(strcmp(protocol->name, "auth") == 0) auth_protocol_id = protocol->id;
        if(strcmp(protocol->name, "msg")  == 0) msg_protocol_id = protocol->id; 
        if(strcmp(protocol->name, "notify") == 0) notify_protocol_id = protocol->id;
        free(protocol);
    }
    if(!msg_protocol_id) {
        fprintf(stderr, "FATAL: no msg protocol\n");
        return 1;
    } 
    fprintf(stderr, "Sent request successfully!\n");
    uint32_t userID = 0;
    if(auth_protocol_id) {
        printf("Server requires auth\n");
        fflush(stdout);

        if(public_key_name[0] == '\0'){
            fprintf(stderr, "Provide keys!\n");
            return 1;
        }

        Request req = {
            .protocol_id = auth_protocol_id,
            .func_id = 0,
            .packet_id = packet_id++,
            .packet_len = KYBER_PUBLICKEYBYTES
        };
        request_hton(&req);
        client_write(&client, &req, sizeof(req));
        client_write(&client, pk, KYBER_PUBLICKEYBYTES);
        client_wflush(&client);

        e = client_read(&client, &resp, sizeof(resp));
        assert(e == 1);
        response_ntoh(&resp);
        assert(resp.opcode == 0);
        assert(resp.packet_len == KYBER_CIPHERTEXTBYTES + 16);
        uint8_t* cipherData = calloc(KYBER_CIPHERTEXTBYTES + 16, 1);
        
        e = client_read(&client, cipherData, KYBER_CIPHERTEXTBYTES + 16);
        assert(e == 1);

        uint8_t* ss = calloc(KYBER_SSBYTES, 1);
        crypto_kem_dec(ss, cipherData, sk);
        AES_init_ctx(&client.aes_ctx, ss);
        free(ss);

        AES_CBC_decrypt_buffer(&client.aes_ctx, cipherData + KYBER_CIPHERTEXTBYTES, 16);

        req = (Request){
            .protocol_id = auth_protocol_id,
            .func_id = 0,
            .packet_id = packet_id++,
            .packet_len = 16
        };
        request_hton(&req);
        client_write(&client, &req, sizeof(req));
        client_write(&client, cipherData + KYBER_CIPHERTEXTBYTES, 16);
        client_wflush(&client);

        free(cipherData);

        e = client_read(&client, &resp, sizeof(resp));
        assert(e == 1);
        response_ntoh(&resp);
        assert(resp.opcode == 0);
        assert(resp.packet_len == sizeof(uint32_t));

        e = client_read(&client, &userID, sizeof(uint32_t));
        assert(e == 1);
        userID = ntohl(userID);
    }
    dming = ~0;
    {
        char buf[128];
        printf("Who you wanna DM?\n> ");
        fflush(stdout);
        char* _ = fgets(buf, sizeof(buf), stdin);
        (void)_;
        dming = atoi(buf);
    }
    {
        Request request = {
            .protocol_id = msg_protocol_id,
            .func_id = 1,
            .packet_id = 69,
            .packet_len = sizeof(MessagesBeforeRequest)
        };
        request_hton(&request);
        uint64_t milis = time_unix_milis();
        MessagesBeforeRequest msgs_request = {
            .server_id = 0,
            .channel_id = dming,
            .milis_low = milis,
            .milis_high = milis >> 32,
            .count = 100,
        };
        messagesBeforeRequest_hton(&msgs_request);
        client_write(&client, &request, sizeof(request));
        client_write(&client, &msgs_request, sizeof(msgs_request));
        client_wflush(&client);

        Response resp;
        // TODO: ^^verify things
        for(;;) {
            client_read(&client, &resp, sizeof(resp));
            response_ntoh(&resp);
            // TODO: ^^verify things
            // TODO: I don't know how to handle such case:
            if(resp.packet_len == 0) break;
            if(resp.packet_len <= sizeof(MessagesBeforeResponse)) abort();
            size_t content_len = resp.packet_len - sizeof(MessagesBeforeResponse);
            char* content = malloc(content_len);
            MessagesBeforeResponse msgs_resp;
            client_read(&client, &msgs_resp, sizeof(MessagesBeforeResponse));
            messagesBeforeResponse_ntoh(&msgs_resp);
            uint64_t milis = (((uint64_t)msgs_resp.milis_high) << 32) | (uint64_t)msgs_resp.milis_low;
            client_read(&client, content, content_len);

            // TODO: verify all this sheize^
            Message msg = {
                .author_name = msgs_resp.author_id < ARRAY_LEN(author_names) ? (char*)author_names[msgs_resp.author_id] : "UNKNOWN",
                .milis = milis,
                .content_len = content_len,
                .content = content
            };
            da_insert(&msgs, 0, msg);
        }
    }

    stui_clear();
    stui_term_get_size(&term_width, &term_height);
    stui_setsize(term_width, term_height);
    stui_term_set_flags(STUI_TERM_FLAG_INSTANT);
    gtgo(reader_thread, (void*)&client);
    size_t notif_event = allocate_incoming_event();
    incoming_events[notif_event].as.onNotification.msgs = &msgs;
    incoming_events[notif_event].onEvent = onNotification;

    if(notify_protocol_id) {
        Request req = {
            .protocol_id = notify_protocol_id,
            .func_id = 0,
            .packet_id = notif_event,
            .packet_len = 0,
        };
        request_hton(&req);
        client_write(&client, &req, sizeof(Request));
        client_wflush(&client);
    }
    for(;;) {
        redraw();
        gtblockfd(fileno(stdin), GTBLOCKIN);
        int c = getchar();
        switch(c) {
        case '\b':
        case 127:
            if(prompt.len) prompt.len--;
            break;
        case '\n': {
            if(prompt.len == 5 && memcmp(prompt.items, ":quit", prompt.len) == 0) goto end;
            Request req = {
                .protocol_id = msg_protocol_id,
                .func_id = 0,
                .packet_id = allocate_incoming_event(),
                .packet_len = sizeof(SendMsgRequest) + prompt.len
            };
            incoming_events[req.packet_id].as.onMessage.msgs = &msgs;
            char* msg = malloc(prompt.len);
            memcpy(msg, prompt.items, prompt.len);
            incoming_events[req.packet_id].as.onMessage.msg = (Message) {
                .milis = time_unix_milis(),
                .author_name = (char*)(userID < ARRAY_LEN(author_names) ? author_names[userID] : "(You. Dumbass)"),
                .content_len = prompt.len,
                .content = msg,
            };
            incoming_events[req.packet_id].onEvent = okOnMessage;
            SendMsgRequest send_msg = {
                .server_id = 0,
                .channel_id = dming,
            };
            request_hton(&req);
            sendMsgRequest_hton(&send_msg);
            client_write(&client, &req, sizeof(req));
            client_write(&client, &send_msg, sizeof(send_msg));
            client_write(&client, prompt.items, prompt.len);
            client_wflush(&client);
            prompt.len = 0;
        } break;
        default:
            da_push(&prompt, c);
            break;
        }
    }
end:
    closesocket(client.fd);
    return 0;
}
