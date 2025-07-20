#include <stdio.h>
#include <gt.h>
#include <snet.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <list_head.h>
#include "darray.h"

#define ARRAY_LEN(a) (sizeof(a)/sizeof(*(a)))
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

#ifdef _WIN32
#else
# include <time.h>
#endif

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

typedef uint64_t userID;
typedef struct {
    size_t content_len;
    char* content;
    uint64_t milis;
    uint32_t author;
} Message;
typedef struct {
    Message* items;
    size_t len, cap;
} Messages;

typedef struct {
    uint32_t* items;
    size_t len, cap;
} UserIds;
typedef struct {
    Messages msgs;
    UserIds participants;
} Channel;
typedef struct {
    Channel channel;
    uint32_t max_user_id;
} DM;
typedef struct {
    DM* items;
    size_t len, cap;
} DMs;
typedef struct {
    const char* username;
    DMs dms;
    // TODO: Mutex this bullsheizung if we do threading
    // List of active connections
    // on that thinger
    struct list_head clients;
} User;
enum {
    USER_F1L1P,
    USER_DCRAFTBG,
    USERS_COUNT
};
static User users[USERS_COUNT] = { 0 };
static DM* get_or_insert_dm(User* user, uint32_t max_user_id) {
    for(size_t i = 0; i < user->dms.len; ++i) {
        DM* dm = &user->dms.items[i];
        if(dm->max_user_id == max_user_id) return dm;
    }
    da_push(&user->dms, ((DM){0}));
    DM* dm = &user->dms.items[user->dms.len-1];
    dm->max_user_id = max_user_id;
    return dm;
}
// TODO: make this failable and return an error (int)
static Channel* get_or_insert_channel(uint32_t server_id, uint32_t channel_id, uint32_t author_id) {
    if(server_id == 0) {
        // TODO: we assert the channel is a valid user ID
        assert(channel_id < USERS_COUNT);
        size_t max_user_id = author_id < channel_id ? channel_id : author_id;
        size_t min_user_id = author_id < channel_id ? author_id : channel_id;
        return &get_or_insert_dm(&users[min_user_id], max_user_id)->channel;
    }
    // TODO: we assert its DMs
    assert(false && "To be done: everything other than DMs");
}
enum {
    ERROR_INVALID_PROTOCOL_ID = 1,
    ERROR_INVALID_FUNC_ID,
    ERROR_NOT_AUTH
};

typedef struct{
    struct list_head list;
    int fd;
    uint32_t userID;
    uint32_t notifyID;
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
typedef struct {
    uint32_t server_id;
    uint32_t channel_id;
    uint32_t milis_low;
    uint32_t milis_high;
    uint32_t count;
} MessagesBeforePacket;
void messagesBeforePacket_ntoh(MessagesBeforePacket* packet) {
    packet->server_id = ntohl(packet->server_id);
    packet->channel_id = ntohl(packet->channel_id);
    packet->milis_low = ntohl(packet->milis_low);
    packet->milis_high = ntohl(packet->milis_high);
    packet->count = ntohl(packet->count);
}
typedef struct {
    uint32_t author_id;
    uint32_t milis_low;
    uint32_t milis_high;
    /*content[packet_len - sizeof(MessagesBeforeResponse)]*/
} MessagesBeforeResponse;
void messagesBeforeResponse_hton(MessagesBeforeResponse* packet) {
    packet->author_id = htonl(packet->author_id);
    packet->milis_low = htonl(packet->milis_low);
    packet->milis_high = htonl(packet->milis_high);
}
typedef struct {
    uint32_t server_id;
    uint32_t channel_id;
} Notification;

void notification_hton(Notification* packet) {
    packet->server_id = ntohl(packet->server_id);
    packet->channel_id = ntohl(packet->channel_id);
}
#define MAX_MESSAGE 10000
void sendMsg(Client* client, Request* header) {
    // NOTE: we hard assert its MORE because you need at least 1 character per message
    // TODO: send some error here:
    if(header->packet_len <= sizeof(SendMsgPacket)) return;
    SendMsgPacket packet = { 0 };
    int n = gtread_exact(client->fd, &packet, sizeof(packet));
    // TODO: send some error here:
    if(n < 0 || n == 0) goto err_read0;
    sendMsgPacket_ntoh(&packet);
    size_t msg_len = header->packet_len - sizeof(SendMsgPacket);
    // TODO: send some error here:
    if(msg_len > MAX_MESSAGE) return;
    char* msg = malloc(msg_len);
    // TODO: send some error here:
    if(!msg) return;
    n = gtread_exact(client->fd, msg, msg_len);
    // TODO: send some error here:
    if(n < 0 || n == 0) goto err_read;
    // TODO: utf8 and isgraphic verifications
    Channel* channel = get_or_insert_channel(packet.server_id, packet.channel_id, client->userID);
    Message message = {
        .content_len = msg_len,
        .content = msg,
        .milis = time_unix_milis(),
        .author = client->userID,
    };
    da_push(&channel->msgs, message);
    for(size_t i = 0; i < channel->participants.len; ++i) {
        uint32_t id = channel->participants.items[i];
        if(id == client->userID) continue;
        User* user = &users[id];
        list_foreach(user_conn_list, &user->clients) {
            Client* user_conn = (Client*)user_conn_list;
            if(!user_conn->notifyID) continue;
            Response resp = {
                .packet_id = user_conn->notifyID,
                .opcode = 0,
                .packet_len = sizeof(Notification)
            };
            response_hton(&resp);
            Notification notif = {
                .server_id = packet.server_id,
                .channel_id = packet.channel_id,
            };
            // TODO: don't block here? And/or spawn a gt thread for each user we're notifying
            send(user_conn->fd, &resp, sizeof(Response), 0);
            send(user_conn->fd, &notif, sizeof(Notification), 0);
        }
    }
    Response resp = {
        .packet_id = header->packet_id,
        .opcode = 0,
        .packet_len = 0
    };
    response_hton(&resp);
    gtwrite_exact(client->fd, &resp, sizeof(resp));
    return;
err_read:
    free(msg);
err_read0:
    return;
}
void getMsgsBefore(Client* client, Request* header) {
    // TODO: send some error here:
    if(header->packet_len != sizeof(MessagesBeforePacket)) return;
    MessagesBeforePacket packet;
    int n = gtread_exact(client->fd, &packet, sizeof(packet));
    // TODO: send some error here:
    if(n < 0 || n == 0) goto err_read0;
    messagesBeforePacket_ntoh(&packet);
    uint64_t milis = (((uint64_t)packet.milis_high) << 32) | (uint64_t)packet.milis_low;

    size_t max_user_id = client->userID < packet.channel_id ? packet.channel_id : client->userID;
    size_t min_user_id = client->userID < packet.channel_id ? client->userID : packet.channel_id;
    DM* dm = get_or_insert_dm(&users[min_user_id], max_user_id);
    for(size_t i = dm->channel.msgs.len; i > 0 && packet.count > 0; --i) {
        Message* msg = &dm->channel.msgs.items[i - 1];
        if(msg->milis < milis) {
            Response resp = {
                .packet_id = header->packet_id,
                .opcode = 0,
                .packet_len = msg->content_len + sizeof(MessagesBeforeResponse),
            };
            MessagesBeforeResponse msg_resp = {
                .author_id = msg->author,
                .milis_low = msg->milis,
                .milis_high = msg->milis >> 32,
            };
            response_hton(&resp);
            messagesBeforeResponse_hton(&msg_resp);
            gtwrite_exact(client->fd, &resp, sizeof(resp));
            gtwrite_exact(client->fd, &msg_resp, sizeof(msg_resp));
            gtwrite_exact(client->fd, msg->content, msg->content_len);
            packet.count--;
        }
    } 
    Response resp = {
        .packet_id = header->packet_id,
        .opcode = 0,
        .packet_len = 0,
    };
    response_hton(&resp);
    gtwrite_exact(client->fd, &resp, sizeof(resp));
err_read0:
    return;
} 
protocol_func_t msgProtocolFuncs[] = {
    sendMsg,
    getMsgsBefore,
};

void notify(Client* client, Request* header) {
    // TODO: send some error here:
    if(header->packet_len != 0) return;
    // TODO: send some error here:
    if(header->packet_id == 0) return;
    client->notifyID = header->packet_id;
    Response resp = {
        .packet_id = header->packet_id,
        .opcode = 0,
        .packet_len = 0,
    };
    response_hton(&resp);
    gtwrite_exact(client->fd, &resp, sizeof(resp));
}
protocol_func_t notifyProtocolFuncs[] = {
    notify,
};
#define PROTOCOL(__name, __funcs) { .name = __name, .funcs_count = ARRAY_LEN(__funcs),  .funcs = __funcs }
Protocol protocols[] = {
    PROTOCOL("CORE", coreProtocolFuncs),
    PROTOCOL("auth", authProtocolFuncs),

    // CORE and auth need to be first in this order otherwise auth logic wont work

    PROTOCOL("echo", echoProtocolFuncs),
    PROTOCOL("msg", msgProtocolFuncs),
    PROTOCOL("notify", notifyProtocolFuncs),
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
    uint32_t userID = ntohl(client->userID);
    // TODO: send some error here:
    if(userID >= USERS_COUNT) return;
    client->userID = userID;
    // TODO: mutex this sheizung
    list_remove(&client->list);
    list_insert(&users[client->userID].clients, &client->list);
    fprintf(stderr, "Welcome %s!\n", users[client->userID].username);
    Response res_header;
    res_header.packet_id = header->packet_id;
    res_header.opcode = 0;
    res_header.packet_len = 0;
    response_hton(&res_header);
    gtwrite_exact(client->fd, &res_header, sizeof(res_header));
}

void client_thread(void* fd_void) {
    Client client = {.fd = (uintptr_t)fd_void, .userID = (uint32_t)-1};
    list_init(&client.list);
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
    users[USER_F1L1P].username = "f1l1p";
    users[USER_DCRAFTBG].username = "dcraftbg";
    list_init(&users[USER_F1L1P].clients);
    list_init(&users[USER_DCRAFTBG].clients);
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
