#include <stdio.h>
#include <gt.h>
#include <snet.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <list_head.h>
#include "darray.h"
#include "post_quantum_cryptography.h"
#include "sqlite3/sqlite3.h"
#include "fileutils.h"
#include "db_context.h"

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
#define ALIGN16(n) (((n) + 15) & ~15)

DbContext* db = NULL;

typedef struct {
    uint8_t* items;
    size_t cap, len; // <- how much data is already in here
} PacketBuilder;

static void pbwrite(PacketBuilder* pb, const void* buf, size_t size) {
    da_reserve(pb, ALIGN16(size));
    memcpy(pb->items + pb->len, buf, size);
    pb->len += size;
}

typedef struct {
    struct list_head list;
    int fd;
    uint32_t userID;
    uint32_t notifyID;

    bool has_read_buffer_data;
    uint8_t read_buffer_head;
    uint8_t read_buffer[16];
    PacketBuilder pb;
    bool secure;
    struct AES_ctx aes_ctx;
} Client;

static intptr_t gtread_exact(Client* client, void* buf, size_t size) {
    while(size) {
        gtblockfd(client->fd, GTBLOCKIN);
        intptr_t e = recv(client->fd, buf, size, 0);
        if(e < 0) return e;
        if(e == 0) return 0; 
        buf = ((char*)buf) + (size_t)e;
        size -= (size_t)e;
    }
    return 1;
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

// NOTE: must be aligned to 16 (aka AES_BLOCKLEN)
intptr_t client_do_read(Client* client, void* buf, size_t size) {
    intptr_t e = gtread_exact(client, buf, size);
    if (!client->secure || e <= 0) return e;
    AES_CBC_decrypt_buffer(&client->aes_ctx, buf, size);
    return 1;
}
intptr_t client_read_(Client* client, void* buf, size_t size) {
    intptr_t e;
    if(client->has_read_buffer_data) {
        size_t buf_remaining = sizeof(client->read_buffer) - client->read_buffer_head;
        size_t n = buf_remaining < size ? buf_remaining : size;
        memcpy(buf, client->read_buffer + client->read_buffer_head, n);
        client->read_buffer_head += n;
        client->read_buffer_head %= sizeof(client->read_buffer);
        buf = (char*)buf + n;
        size -= n;
        if(size == 0) return 1;
    }
    // read whole chunks of 16
    {
        size_t n = size / sizeof(client->read_buffer) * sizeof(client->read_buffer);
        e = client_do_read(client, buf, n);
        if(e <= 0) return e;
        buf = (char*)buf + n;
        size -= n;
    }
    client->has_read_buffer_data = false;

    assert(size <= 16);
    // read leftover into read buffer and then copy back to buf.
    if(size) {
        e = client_do_read(client, client->read_buffer, ALIGN16(size));
        if(e <= 0) return e;
        memcpy(buf, client->read_buffer, size);
        client->read_buffer_head = size;
        client->has_read_buffer_data = true;
        buf = (char*)buf + size;
        size = 0;
    }
    return 1;
}
static void client_discard_read_buf(Client* c) {
    c->has_read_buffer_data = false;
    c->read_buffer_head = 0;
}

static intptr_t client_do_write(Client* client, void* buf, size_t size) {
    if (client->secure) AES_CBC_encrypt_buffer(&client->aes_ctx, buf, size);
    return gtwrite_exact(client->fd, buf, size);
}

static intptr_t pbflush(PacketBuilder* pb, Client* client) {
    intptr_t e = client_do_write(client, pb->items, ALIGN16(pb->len));
    pb->len = 0;
    return e;
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

enum {
    ERROR_INVALID_PROTOCOL_ID = 1,
    ERROR_INVALID_FUNC_ID,
    ERROR_NOT_AUTH
};

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
    client_read_(client, buf, header->packet_len);
    Response resp = {
        .packet_id = header->packet_id,
        .opcode = 0,
        .packet_len = header->packet_len
    };
    response_hton(&resp);
    pbwrite(&client->pb, &resp, sizeof(resp));
    pbwrite(&client->pb, buf, header->packet_len);
    pbflush(&client->pb, client);
}
protocol_func_t echoProtocolFuncs[] = {
    echoEcho,
};
void authAuthenticate(Client* client, Request* header);
protocol_func_t authProtocolFuncs[] = {
    authAuthenticate,
};

typedef struct {
    uint32_t userID;
} GetUserInfoPacket;
void getUserInfoPacket_ntoh(GetUserInfoPacket* packet){
    packet->userID = ntohl(packet->userID);
}

void userGetUserInfo(Client* client, Request* header);
protocol_func_t userProtocolFuncs[] = {
    userGetUserInfo,
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
    uint32_t author_id;
    uint32_t milis_low;
    uint32_t milis_high;
} Notification;

void notification_hton(Notification* packet) {
    packet->server_id = htonl(packet->server_id);
    packet->channel_id = htonl(packet->channel_id);
    packet->author_id = htonl(packet->author_id);
    packet->milis_low = htonl(packet->milis_low);
    packet->milis_high = htonl(packet->milis_high);
}
#define MAX_MESSAGE 10000
typedef struct UserMapBucket UserMapBucket;
struct UserMapBucket {
    UserMapBucket* next;
    uint32_t user_id;
    struct list_head clients;
};
typedef struct {
    struct {
        UserMapBucket** items;
        size_t len;
    } buckets;
    size_t len;
} UserMap;
bool user_map_reserve(UserMap* map, size_t extra) {
    if(map->len + extra > map->buckets.len) {
        size_t ncap = map->buckets.len*2 + extra;
        UserMapBucket** newbuckets = malloc(sizeof(*newbuckets)*ncap);
        if(!newbuckets) return false;
        memset(newbuckets, 0, sizeof(*newbuckets) * ncap);
        for(size_t i = 0; i < map->buckets.len; ++i) {
            UserMapBucket* oldbucket = map->buckets.items[i];
            while(oldbucket) {
                UserMapBucket* next = oldbucket->next;
                size_t hash = ((size_t)oldbucket->user_id) % ncap;
                UserMapBucket* newbucket = newbuckets[hash];
                oldbucket->next = newbucket;
                newbuckets[hash] = oldbucket;
                oldbucket = next;
            }
        }
        free(map->buckets.items);
        map->buckets.items = newbuckets;
        map->buckets.len = ncap;
    }
    return true;
}
UserMapBucket* user_map_insert(UserMap* map, uint32_t user_id) {
    if(!user_map_reserve(map, 1)) return NULL;
    size_t hash = ((size_t)user_id) % map->buckets.len;
    UserMapBucket* into = map->buckets.items[hash];
    UserMapBucket* bucket = calloc(sizeof(UserMapBucket), 1);
    if(!bucket) return NULL;
    bucket->next = into;
    bucket->user_id = user_id;
    list_init(&bucket->clients);
    map->buckets.items[hash] = bucket;
    map->len++;
    return bucket;
}
UserMapBucket* user_map_get(UserMap* map, uint32_t user_id) {
    if(map->len == 0) return NULL;
    assert(map->buckets.len > 0);
    size_t hash = ((size_t)user_id) % map->buckets.len;
    UserMapBucket* bucket = map->buckets.items[hash];
    while(bucket) {
        if(bucket->user_id == user_id) return bucket;
        bucket = bucket->next;
    }
    return NULL;
}
UserMapBucket* user_map_get_or_insert(UserMap* map, uint32_t user_id) {
    UserMapBucket* bucket = user_map_get(map, user_id);
    if(bucket) return bucket;
    return user_map_insert(map, user_id);
}
static UserMap user_map = { 0 };
typedef struct {
    uint32_t *items;
    size_t len, cap;
} Participants;
void sendMsg(Client* client, Request* header) {
    // NOTE: we hard assert its MORE because you need at least 1 character per message
    // TODO: send some error here:
    if(header->packet_len <= sizeof(SendMsgPacket)) return;
    SendMsgPacket packet = { 0 };
    int n = client_read_(client, &packet, sizeof(packet));
    // TODO: send some error here:
    if(n < 0 || n == 0) goto err_read0;
    sendMsgPacket_ntoh(&packet);
    size_t msg_len = header->packet_len - sizeof(SendMsgPacket);
    // TODO: send some error here:
    if(msg_len > MAX_MESSAGE) return;
    char* msg = malloc(msg_len);
    // TODO: send some error here:
    if(!msg) return;
    n = client_read_(client, msg, msg_len);
    // TODO: send some error here:
    if(n < 0 || n == 0) goto err_read;
    // TODO: utf8 and isgraphic verifications
    Message message = {
        .content_len = msg_len,
        .content = msg,
        .milis = time_unix_milis(),
        .author = client->userID,
    };
    int e = DbContext_send_msg(db, packet.server_id, packet.channel_id, client->userID, message.content, message.content_len, message.milis);
    //TODO: send some error here:
    if(e < 0) return;

    Participants participants = { 0 };
    if(packet.server_id == 0){
        da_push(&participants, client->userID);
        da_push(&participants, packet.channel_id);
        //TODO: notifications       
    }else{
        //TODO: we assert its DMs
        assert(false && "TODO: everything other than DMs");
    }
    for(size_t i = 0; i < participants.len; ++i) {
        UserMapBucket* user = user_map_get(&user_map, participants.items[i]);
        if(!user) continue;
        list_foreach(user_conn_list, &user->clients) {
            Client* user_conn = (Client*)user_conn_list;
            if(user_conn->notifyID == ~0u) continue;
            if(user_conn == client) continue;
            Response resp = {
                .packet_id = user_conn->notifyID,
                .opcode = 0,
                .packet_len = sizeof(Notification) + msg_len
            };
            response_hton(&resp);
            Notification notif = {
                .server_id = packet.server_id,
                .channel_id = packet.server_id == 0 ? participants.items[(i + 1) % 2] : packet.channel_id,
                .author_id = message.author,
                .milis_low = message.milis,
                .milis_high = message.milis >> 32,
            };
            notification_hton(&notif);
            // TODO: don't block here? And/or spawn a gt thread for each user we're notifying
            pbwrite(&user_conn->pb, &resp, sizeof(Response));
            pbwrite(&user_conn->pb, &notif, sizeof(Notification));
            pbwrite(&user_conn->pb, msg, msg_len);
            pbflush(&user_conn->pb, user_conn);
        }
    }
    free(participants.items);
    /*
    for(size_t i = 0; i < channel->participants.len; ++i) {
        uint32_t id = channel->participants.items[i];
        User* user = &users[id];
        list_foreach(user_conn_list, &user->clients) {
            Client* user_conn = (Client*)user_conn_list;
            if(user_conn->notifyID == ~0u) continue;
            if(user_conn == client) continue;
            Response resp = {
                .packet_id = user_conn->notifyID,
                .opcode = 0,
                .packet_len = sizeof(Notification) + msg_len
            };
            response_hton(&resp);
            Notification notif = {
                .server_id = packet.server_id,
                .channel_id = packet.server_id == 0 ? channel->participants.items[(i + 1) % 2] : packet.channel_id,
                .author_id = message.author,
                .milis_low = message.milis,
                .milis_high = message.milis >> 32,
            };
            notification_hton(&notif);
            // TODO: don't block here? And/or spawn a gt thread for each user we're notifying
            pbwrite(&user_conn->pb, &resp, sizeof(Response));
            pbwrite(&user_conn->pb, &notif, sizeof(Notification));
            pbwrite(&user_conn->pb, msg, msg_len);
            pbflush(&user_conn->pb, user_conn);
        }
    }
    */
    Response resp = {
        .packet_id = header->packet_id,
        .opcode = 0,
        .packet_len = 0
    };
    response_hton(&resp);
    pbwrite(&client->pb, &resp, sizeof(resp));
    pbflush(&client->pb, client);
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
    int n = client_read_(client, &packet, sizeof(packet));
    // TODO: send some error here:
    if(n < 0 || n == 0) goto err_read0;
    messagesBeforePacket_ntoh(&packet);
    uint64_t milis = (((uint64_t)packet.milis_high) << 32) | (uint64_t)packet.milis_low;

    Messages msgs = {0};
    int e = DbContext_get_msgs_before(db, packet.server_id, packet.channel_id, client->userID, milis, &msgs);
    //TODO: send some error here:
    if(e < 0) goto finish;

    for(size_t i = msgs.len; i > 0; --i) {
        Message* msg = &msgs.items[i - 1];
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
        pbwrite(&client->pb, &resp, sizeof(resp));
        pbwrite(&client->pb, &msg_resp, sizeof(msg_resp));
        pbwrite(&client->pb, msg->content, msg->content_len);
        pbflush(&client->pb, client);

        free(msg->content);
    } 
finish:
    {} // apparently labels cannot be near declarations so this is lil hack
    Response resp = {
        .packet_id = header->packet_id,
        .opcode = 0,
        .packet_len = 0,
    };
    response_hton(&resp);
    pbwrite(&client->pb, &resp, sizeof(resp));
    pbflush(&client->pb, client);
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
    client->notifyID = header->packet_id;
    Response resp = {
        .packet_id = header->packet_id,
        .opcode = 0,
        .packet_len = 0,
    };
    response_hton(&resp);
    pbwrite(&client->pb, &resp, sizeof(resp));
    pbflush(&client->pb, client);
}
protocol_func_t notifyProtocolFuncs[] = {
    notify,
};

void userGetUserInfo(Client* client, Request* header) {
    // TODO: send some error here:
    if(header->packet_len != sizeof(GetUserInfoPacket)) return;
    GetUserInfoPacket packet;
    int n = client_read_(client, &packet, sizeof(packet));
    // TODO: send some error here:
    if(n < 0 || n == 0) return;
    getUserInfoPacket_ntoh(&packet);
    
    char* username = NULL;
    DbContext_get_username_from_user_id(db, packet.userID, &username);
    if(username == NULL){
        Response resp = {
            .packet_id = header->packet_id,
            .opcode = 1,
            .packet_len = 0,
        };
        response_hton(&resp);
        pbwrite(&client->pb, &resp, sizeof(resp));
        pbflush(&client->pb, client);
        return;
    }

    Response resp = {
        .packet_id = header->packet_id,
        .opcode = 0,
        .packet_len = strlen(username),
    };
    response_hton(&resp);
    pbwrite(&client->pb, &resp, sizeof(resp));
    pbwrite(&client->pb, username, strlen(username));
    pbflush(&client->pb, client);
    free(username);
}

#define PROTOCOL(__name, __funcs) { .name = __name, .funcs_count = ARRAY_LEN(__funcs),  .funcs = __funcs }
Protocol protocols[] = {
    PROTOCOL("CORE", coreProtocolFuncs),
    PROTOCOL("auth", authProtocolFuncs),

    // CORE and auth need to be first in this order otherwise auth logic wont work

    PROTOCOL("echo", echoProtocolFuncs),
    PROTOCOL("msg", msgProtocolFuncs),
    PROTOCOL("notify", notifyProtocolFuncs),
    PROTOCOL("user", userProtocolFuncs),
};
void coreGetProtocols(Client* client, Request* header) {
    for(size_t i = 0; i < ARRAY_LEN(protocols); ++i) {
        Response res_header;
        res_header.packet_id = header->packet_id;
        res_header.opcode = 0;
        res_header.packet_len = sizeof(uint32_t) + strlen(protocols[i].name);
        response_hton(&res_header);
        pbwrite(&client->pb, &res_header, sizeof(res_header));
        uint32_t id = htonl(i);
        pbwrite(&client->pb, &id, sizeof(id));
        pbwrite(&client->pb, protocols[i].name, strlen(protocols[i].name));
        pbflush(&client->pb, client);
    }
    Response res_header;
    res_header.packet_id = header->packet_id;
    res_header.opcode = 0;
    res_header.packet_len = 0;
    response_hton(&res_header);
    pbwrite(&client->pb, &res_header, sizeof(res_header));
    pbflush(&client->pb, client);
}

// FIXME: all these fucking memory leaks :(((
void authAuthenticate(Client* client, Request* header){
    // TODO: send some error here:
    if(header->packet_len != KYBER_PUBLICKEYBYTES) return;
    uint8_t* pk = calloc(KYBER_PUBLICKEYBYTES, 1);
    client_read_(client, pk, KYBER_PUBLICKEYBYTES);

    uint32_t userID = ~0u;
    int e = DbContext_get_user_id_from_pub_key(db, pk, &userID);
    if(e < 0) {
        error("%d: Couldn't find user", client->fd);
        return;
    } 
    info("%d: Someone is trying to log in as %d", client->fd, userID);
    #define RAND_COUNT 16
    uint8_t* ct = calloc(KYBER_CIPHERTEXTBYTES + RAND_COUNT, 1);

    uint8_t* ss = calloc(KYBER_SSBYTES, 1);
    
    
    crypto_kem_enc(ct, ss, pk);

    AES_init_ctx(&client->aes_ctx, ss);
    free(ss);

    uint8_t* randBytes = calloc(RAND_COUNT, 1);
    randombytes(randBytes, RAND_COUNT);
    memcpy(ct + KYBER_CIPHERTEXTBYTES, randBytes, RAND_COUNT);
    AES_CBC_encrypt_buffer(&client->aes_ctx, ct + KYBER_CIPHERTEXTBYTES, RAND_COUNT);

    Response test = {
        .packet_id = header->packet_id,
        .opcode = 0,
        .packet_len = KYBER_CIPHERTEXTBYTES + RAND_COUNT
    };
    response_hton(&test);
    pbwrite(&client->pb, &test, sizeof(test));
    pbwrite(&client->pb, ct, KYBER_CIPHERTEXTBYTES + RAND_COUNT);
    pbflush(&client->pb, client);
    free(ct);

    Request req;
    client_discard_read_buf(client);
    // FIXME: REMOVE asserts
    assert(client_read_(client, &req, sizeof(req)) == 1);
    request_ntoh(&req);
    assert(req.packet_len == RAND_COUNT);
    uint8_t* userRandBytes = calloc(RAND_COUNT, 1);
    assert(client_read_(client, userRandBytes, RAND_COUNT));

    if(memcmp(randBytes, userRandBytes, RAND_COUNT) != 0){
        free(randBytes);
        free(userRandBytes);
        //TODO: send some error here
        return;
    }

    free(randBytes);
    free(userRandBytes);

    client->userID = userID;

    UserMapBucket* user = user_map_get_or_insert(&user_map, userID);
    // TODO: mutex this sheizung
    list_remove(&client->list);
    list_insert(&user->clients, &client->list);
    info("%d: Welcome %d!", client->fd, userID);
    Response res_header;
    res_header.packet_id = header->packet_id;
    res_header.opcode = 0;
    res_header.packet_len = sizeof(uint32_t);
    response_hton(&res_header);
    pbwrite(&client->pb, &res_header, sizeof(res_header));
    userID = htonl(userID);
    pbwrite(&client->pb, &userID, sizeof(userID));
    pbflush(&client->pb, client);

    client->secure = true;
}

void client_thread(void* fd_void) {
    Client client = {.fd = (uintptr_t)fd_void, .userID = ~0, .notifyID = ~0, .secure = false};
    list_init(&client.list);
    Request req_header;
    Response res_header;
    for(;;) {
        client_discard_read_buf(&client);
        int n = client_read_(&client, &req_header, sizeof(req_header));
        if(n < 0) break;
        if(n == 0) break;
        request_ntoh(&req_header);
        if(req_header.protocol_id >= ARRAY_LEN(protocols)) {
            error("%d: Invalid protocol_id: %u", client.fd, req_header.protocol_id);
            res_header.packet_id = req_header.packet_id;
            res_header.opcode = -ERROR_INVALID_PROTOCOL_ID;
            res_header.packet_len = 0;
            response_hton(&res_header);
            pbwrite(&client.pb, &res_header, sizeof(res_header));
            pbflush(&client.pb, &client);
            continue;
        }
        Protocol* proto = &protocols[req_header.protocol_id];
        if(req_header.func_id >= proto->funcs_count) {
            error("%d: Invalid func_id: %u",  client.fd, req_header.func_id);
            res_header.packet_id = req_header.packet_id;
            res_header.opcode = -ERROR_INVALID_FUNC_ID;
            res_header.packet_len = 0;
            response_hton(&res_header);
            pbwrite(&client.pb, &res_header, sizeof(res_header));
            pbflush(&client.pb, &client);
            continue;
        }

        if (client.userID == (uint32_t)-1 && req_header.protocol_id >= 2){
            error("%d: Not Authenticated", client.fd);
            res_header.packet_id = req_header.packet_id;
            res_header.opcode = -ERROR_NOT_AUTH;
            res_header.packet_len = 0;
            response_hton(&res_header);
            pbwrite(&client.pb, &res_header, sizeof(res_header));
            pbflush(&client.pb, &client);
            continue;
        }

        info("%d: %s func_id=%d", client.fd, proto->name, req_header.func_id);
        proto->funcs[req_header.func_id](&client, &req_header);
    }
    list_remove(&client.list);
    closesocket(client.fd);
    free(client.pb.items);
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
