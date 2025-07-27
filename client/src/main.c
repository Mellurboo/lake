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

#define ALIGN16(n) (((n) + 15) & ~15)
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

typedef struct UserMapBucket UserMapBucket;
struct UserMapBucket {
    UserMapBucket* next;
    uint32_t user_id;
    char* username;
    bool in_progress;
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
    uint32_t userID;
} GetUserInfoPacket;
void getUserInfoPacket_hton(GetUserInfoPacket* packet){
    packet->userID = htonl(packet->userID);
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
typedef struct {
    uint8_t* items;
    size_t cap, len; // <- how much data is already in here
} PacketBuilder;

static void pbwrite(PacketBuilder* pb, const void* buf, size_t size) {
    da_reserve(pb, ALIGN16(pb->len + size) - pb->len);
    memcpy(pb->items + pb->len, buf, size);
    pb->len += size;
}
typedef struct {
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
    if(size == 0) return 1;
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
Client client = {0};
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
    uint32_t author_id;
    uint32_t content_len;
    char* content;
} Message;

typedef struct {
    uint16_t l, t, r, b;
} UIBox;
void uibox_draw_border(UIBox box, int tb, int lr, int corner) {
    stui_window_border(box.l, box.t, box.r - box.l, box.b - box.t, tb, lr, corner);
}
// The box that represents the content that would go within the border
inline UIBox uibox_inner(UIBox box) {
    return (UIBox){box.l + 1, box.t + 1, box.r - 1, box.b - 1};
}
inline UIBox uibox_chop_left(UIBox* box, uint16_t n) {
    uint16_t l = box->l;
    box->l += n;
    return (UIBox){l, box->t, box->l, box->b };
}
inline UIBox uibox_chop_bottom(UIBox* box, uint16_t n) {
    uint16_t b = box->b;
    box->b -= n;
    return (UIBox){box->l, box->b, box->r, b };
}


typedef struct {
    Message* items;
    size_t len, cap;
} Messages;

char* get_author_name(Client* client, uint32_t author_id);
void render_messages(Client* client, UIBox box, Messages* msgs) {
    size_t box_width = box.r - box.l;
    int32_t y_offset_start = box.b - box.t + 1;
    for(size_t i = msgs->len; i > 0; --i) {
        Message* msg = &msgs->items[i-1];

        char *content_cursor = msg->content, *content_end = msg->content + msg->content_len;

        char prefix[256];
        time_t msg_time_secs = msg->milis / 1000;
        struct tm* msg_time = localtime(&msg_time_secs);


        char tmp_author_name[20];
        char* author_name = get_author_name(client, msg->author_id);
        if(!author_name) {
            snprintf(tmp_author_name, sizeof(tmp_author_name), "User #%u", msg->author_id);
            author_name = tmp_author_name;
        }
        size_t prefix_len = snprintf(prefix, sizeof(prefix), "<%02d/%02d/%02d> %s: ", msg_time->tm_mday, msg_time->tm_mon + 1, msg_time->tm_year % 100, author_name);
        size_t msg_lines = (prefix_len + msg->content_len + (box_width - 1)) / box_width;
        y_offset_start -= msg_lines;
        char* prefix_cursor = prefix;

        size_t y_offset_offset_start = 0;
        size_t x_offset_start = 0;
        while(*prefix_cursor) {
            if((y_offset_start + (int32_t)y_offset_offset_start) < 0) break;
            if(x_offset_start >= box_width) {
                x_offset_start = 0;
                y_offset_offset_start++;
            }
            stui_putchar(box.l + x_offset_start, box.t + y_offset_start + y_offset_offset_start, *prefix_cursor);
            x_offset_start++;
            prefix_cursor++;
        }
        if(y_offset_start < 0) break;
        for(size_t j = y_offset_offset_start; j < msg_lines; ++j, content_cursor += box_width - x_offset_start) {
            if((y_offset_start + (int32_t)j) < 0) continue;
            for(size_t x_offset = 0; x_offset < box_width - x_offset_start && content_cursor + x_offset < content_end; ++x_offset) {
                stui_putchar(box.l + x_offset_start + x_offset, box.t + y_offset_start + (int32_t)j, content_cursor[x_offset]);
            }
            x_offset_start = 0;
        }
    }
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
        struct { UserMapBucket* user; } onUserInfo;
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

uint32_t user_protocol_id = 0;
void redraw(void);
void onUserInfo(Client* client, Response* response, IncomingEvent* event) {
    event->onEvent = NULL;
    UserMapBucket* user = event->as.onUserInfo.user;
    // NOTE: not entirely necessary but who cares
    // readability I guess
    user->in_progress = false;
    if(response->packet_len == 0) {
        user->username = "BOGUS";
        return;
    }
    if(response->packet_len > 128) {
        user->username = "<Too long>";
        // FIXME: discard packet data on here
        return;
    }
    user->username = calloc(response->packet_len + 1, 1);
    int e = client_read_(client, user->username, response->packet_len);
    (void)e;
    redraw();
}
//TODO: getting info from db doesnt work during MessagesBeforePacket idk what about during notifications
char* get_author_name(Client* client, uint32_t author_id){
    UserMapBucket* user = user_map_get_or_insert(&user_map, author_id);

    if(user->username == NULL && !user->in_progress) {
        if(user_protocol_id == 0) return NULL;
        GetUserInfoPacket packet = {
            .userID = author_id
        };

        getUserInfoPacket_hton(&packet);
        user->in_progress = true;
        Request request = {
            .protocol_id = user_protocol_id,
            .func_id = 0,
            .packet_id = allocate_incoming_event(),
            .packet_len = sizeof(GetUserInfoPacket)
        };
        incoming_events[request.packet_id].as.onUserInfo.user = user;
        incoming_events[request.packet_id].onEvent = onUserInfo;

        request_hton(&request);
        pbwrite(&client->pb, &request, sizeof(Request));
        pbwrite(&client->pb, &packet, sizeof(packet));
        intptr_t e = pbflush(&client->pb, client);
        (void)e;
        (void)client;
        // TODO: error here?
    }

    return user->username;
}

void reader_thread(void* client_void) {
    Client* client = client_void;
    int e;
    for(;;) {
        Response resp; 
        client_discard_read_buf(client);
        e = client_read_(client, &resp, sizeof(resp));
        if(e != 1) break;
        response_ntoh(&resp);
        // TODO: skip resp.len amount maybe?
        if(resp.packet_id >= MAX_INCOMING_EVENTS) {
            continue;
        }
        if(!incoming_events[resp.packet_id].onEvent) {
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
uint32_t dming;
void onNotification(Client* client, Response* response, IncomingEvent* event) {
    // SKIP
    if(response->packet_len == 0) return;
    assert(response->opcode == 0);
    assert(response->packet_len > sizeof(Notification));
    Notification notif;
    // TODO: error check
    client_read_(client, &notif, sizeof(notif));
    notification_ntoh(&notif);
    size_t content_len = response->packet_len - sizeof(Notification);
    char* content = malloc(content_len);
    // TODO: error check
    client_read_(client, content, content_len);

    if(notif.server_id != 0 || notif.channel_id != dming) {
        free(content);
        return;
    }
    uint64_t milis = (((uint64_t)notif.milis_high) << 32) | (uint64_t)notif.milis_low;
    Message msg = {
        .content_len = content_len,
        .content = content,
        .milis = milis,
        .author_id = notif.author_id,
    };
    da_push(event->as.onNotification.msgs, msg);
    redraw();
}
Messages msgs;
size_t term_width, term_height;
StringBuilder prompt = { 0 };
bool tab_list = false;
uint16_t tab_list_selection = 0;

enum {
    TAB_CATEGORY_DMS,
    TAB_CATEGORY_GROUP_CHATS,
    TAB_CATEGORY_SERVERS,
    TAB_CATEGORIES_COUNT
};
static_assert(TAB_CATEGORIES_COUNT == 3);
const char* tab_category_labels[] = {
    [TAB_CATEGORY_DMS] = "DMS",
    [TAB_CATEGORY_GROUP_CHATS] = "Group Chats",
    [TAB_CATEGORY_SERVERS] = "Servers",
};

enum {
    TAB_LIST_STATE_CATEGORY,
    TAB_LIST_STATE_DMS,
} tab_list_state = TAB_LIST_STATE_CATEGORY;
typedef struct {
    const char** items;
    size_t len, cap;
} TabLabels;
TabLabels tab_labels = { 0 };
void redraw(void) {
    for(size_t y = 0; y < term_height; ++y) {
        for(size_t x = 0; x < term_width; ++x) {
            stui_putchar(x, y, ' ');
        }
    }
    UIBox term_box = {
        0, 0, term_width - 1, term_height - 1
    };
    UIBox tab_list_box;
    if(tab_list) {
        tab_labels.len = 0;
        tab_list_box = uibox_chop_left(&term_box, (term_box.r - term_box.l) * 14 / 100);
        uibox_draw_border(tab_list_box, '=', '|', '+');
        switch(tab_list_state) {
        case TAB_LIST_STATE_CATEGORY:
            for(size_t i = 0; i < TAB_CATEGORIES_COUNT; ++i) {
                da_push(&tab_labels, tab_category_labels[i]);
            }
            break;
        case TAB_LIST_STATE_DMS:
            da_push(&tab_labels, "f1l1p");
            da_push(&tab_labels, "bogus");
            da_push(&tab_labels, "amogus");
            break;
        }
        UIBox tab_list_inner = uibox_inner(tab_list_box);
        for(size_t i = 0; i < tab_labels.len; ++i) {
            if(tab_list_inner.t + i > tab_list_inner.b) break;
            const char* label = tab_labels.items[i];
            size_t dx = 0;
            uint32_t fg = 0;
            if(tab_list_selection == i) {
                fg = STUI_RGB(0x00ffff);
                stui_putchar_color(tab_list_inner.l + dx++, tab_list_inner.t + i, '>', fg, 0);
                stui_putchar_color(tab_list_inner.l + dx++, tab_list_inner.t + i, ' ', fg, 0);
            }
            while(*label) {
                if(tab_list_inner.l + dx > tab_list_inner.r) break;
                stui_putchar_color(tab_list_inner.l + dx++, tab_list_inner.t + i, *label, fg, 0);
                label++;
            }
        }
    }
    UIBox input_box = uibox_chop_bottom(&term_box, 1);
    uibox_draw_border(term_box, '=', '|', '+');
    // stui_window_border(0, 0, term_width - 1, term_height - 2, '=', '|', '+');
    char buf[128];
    char tmp_dming_name[20];
    char* dming_name = get_author_name(&client, dming);
    // TODO: remove code duplication somehow.
    if(!dming_name) {
        snprintf(tmp_dming_name, sizeof(tmp_dming_name), "User #%u", dming);
        dming_name = tmp_dming_name;
    }
    snprintf(buf, sizeof(buf), "DMs: %s", dming_name);

    {
        char* str = buf;
        size_t x = term_box.l + 2;
        while(*str) stui_putchar(x++, 0, *str++);
    }
    render_messages(&client, uibox_inner(term_box), &msgs);
    stui_putchar(input_box.l + 0, term_height - 1, '>');
    stui_putchar(input_box.l + 1, term_height - 1, ' ');
    for(size_t i = input_box.l + input_box.l + 2; i < term_width - 1; ++i) {
        stui_putchar(i, term_height - 1, ' ');
    }
    size_t i = 0;
    for(; i < prompt.len; ++i) {
        if(input_box.l + 2 + i >= input_box.r) continue;
        stui_putchar(input_box.l + 2 + i, input_box.b, prompt.items[i]);
    }
    stui_refresh();
    if(tab_list) stui_goto(uibox_inner(tab_list_box).l, uibox_inner(tab_list_box).t + tab_list_selection);
    else stui_goto(input_box.l + 2 + i, input_box.b);
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
    client.fd = socket(AF_INET, SOCK_STREAM, 0); 
    client.secure = false;
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


    const char* exe = shift_args(&argc, &argv);
    (void)exe;
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
        } else {
            fprintf(stderr, "ERROR: unexpected argument `%s`\n", arg);
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
    pbwrite(&client.pb, &(Request) { .protocol_id = 0, .func_id = 0, .packet_id = htonl(get_extensions_id), .packet_len = 0 }, sizeof(Request));
    intptr_t e = pbflush(&client.pb, &client);
    if(e < 0) {
        fprintf(stderr, "FATAL: Failed to send request: %s\n", sneterr());
        return 1;
    }
    Response resp; 

    uint32_t auth_protocol_id = 0;
    // uint32_t echo_protocol_id = 0;
    uint32_t msg_protocol_id = 0;

    uint32_t notify_protocol_id = 0;
    for(;;) {
        client_discard_read_buf(&client);
        e = client_read_(&client, &resp, sizeof(resp));
        assert(e == 1);
        response_ntoh(&resp);
        assert(resp.packet_id == get_extensions_id);
        assert(resp.packet_len < 1024);
        if(resp.packet_len == 0) break;

        assert(resp.packet_len >= sizeof(uint32_t));
        size_t name_len = resp.packet_len - sizeof(uint32_t);
        Protocol* protocol = malloc(resp.packet_len + 1);
        assert(client_read_(&client, protocol, resp.packet_len) == 1);
        protocol->id = ntohl(protocol->id);
        protocol->name[name_len] = '\0';
        if(((int)resp.opcode) < 0) {
            fprintf(stderr, "FATAL: Error on my response: %d\n", -((int)resp.opcode));
            return 1;
        }
        fprintf(stderr, "INFO: Protocol id=%u name=%s\n", protocol->id, protocol->name);
        // if(strcmp(protocol->name, "echo") == 0) echo_protocol_id = protocol->id;
        if(strcmp(protocol->name, "auth") == 0) auth_protocol_id = protocol->id;
        if(strcmp(protocol->name, "msg")  == 0) msg_protocol_id = protocol->id; 
        if(strcmp(protocol->name, "notify") == 0) notify_protocol_id = protocol->id;
        if(strcmp(protocol->name, "user") == 0) user_protocol_id = protocol->id;
        free(protocol);
    }
    if(!msg_protocol_id) {
        fprintf(stderr, "FATAL: no msg protocol\n");
        return 1;
    } 
    uint32_t userID = 0;
    if(auth_protocol_id) {
        printf("Server requires auth\n");
        fflush(stdout);

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

        Request req = {
            .protocol_id = auth_protocol_id,
            .func_id = 0,
            .packet_id = packet_id++,
            .packet_len = KYBER_PUBLICKEYBYTES
        };
        request_hton(&req);
        pbwrite(&client.pb, &req, sizeof(req));
        pbwrite(&client.pb, pk, KYBER_PUBLICKEYBYTES);
        pbflush(&client.pb, &client);

        client_discard_read_buf(&client);
        e = client_read_(&client, &resp, sizeof(resp));
        assert(e == 1);
        response_ntoh(&resp);
        assert(resp.opcode == 0);
        assert(resp.packet_len == KYBER_CIPHERTEXTBYTES + 16);
        uint8_t* cipherData = calloc(KYBER_CIPHERTEXTBYTES + 16, 1);
        
        e = client_read_(&client, cipherData, KYBER_CIPHERTEXTBYTES + 16);
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
        pbwrite(&client.pb, &req, sizeof(req));
        pbwrite(&client.pb, cipherData + KYBER_CIPHERTEXTBYTES, 16);
        pbflush(&client.pb, &client);

        free(cipherData);

        client_discard_read_buf(&client);
        e = client_read_(&client, &resp, sizeof(resp));
        assert(e == 1);
        response_ntoh(&resp);
        assert(resp.opcode == 0);
        assert(resp.packet_len == sizeof(uint32_t));

        e = client_read_(&client, &userID, sizeof(uint32_t));
        assert(e == 1);
        userID = ntohl(userID);

        client.secure = true;
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
        //TODO: make so it works during other requests or refactor it
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
        pbwrite(&client.pb, &request, sizeof(request));
        pbwrite(&client.pb, &msgs_request, sizeof(msgs_request));
        pbflush(&client.pb, &client);
        Response resp;
        // TODO: ^^verify things
        for(;;) {
            client_discard_read_buf(&client);
            client_read_(&client, &resp, sizeof(resp));
            response_ntoh(&resp);
            // TODO: ^^verify things
            // TODO: I don't know how to handle such case:
            if(resp.packet_len == 0) break;
            if(resp.packet_len <= sizeof(MessagesBeforeResponse)) abort();
            size_t content_len = resp.packet_len - sizeof(MessagesBeforeResponse);
            char* content = malloc(content_len);
            MessagesBeforeResponse msgs_resp;
            client_read_(&client, &msgs_resp, sizeof(MessagesBeforeResponse));
            messagesBeforeResponse_ntoh(&msgs_resp);
            uint64_t milis = (((uint64_t)msgs_resp.milis_high) << 32) | (uint64_t)msgs_resp.milis_low;
            client_read_(&client, content, content_len);

            // TODO: verify all this sheize^
            Message msg = {
                .author_id = msgs_resp.author_id,
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
    gtgo(reader_thread, &client);
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
        pbwrite(&client.pb, &req, sizeof(Request));
        pbflush(&client.pb, &client);
    }
    for(;;) {
        redraw();
        gtblockfd(fileno(stdin), GTBLOCKIN);
        int c = getchar();
        if(tab_list) {
            if(c == '\t') {
                tab_list = !tab_list;
                continue;
            }
            switch(tab_list_state) {
            case TAB_LIST_STATE_CATEGORY:
                switch(c) {
                case 'D':
                case 'd':
                    tab_list_selection = TAB_CATEGORY_DMS;
                    break;
                case 'G':
                case 'g':
                    tab_list_selection = TAB_CATEGORY_GROUP_CHATS;
                    break;
                case 'S':
                case 's':
                    tab_list_selection = TAB_CATEGORY_SERVERS;
                    break;
                case '\n':
                    if(tab_list_selection == 0) tab_list_state = TAB_LIST_STATE_DMS;
                    break;
                }
                break;
            case TAB_LIST_STATE_DMS:
                switch(c) {
                case 'b':
                case 'B':
                    tab_list_state = TAB_LIST_STATE_CATEGORY;
                    break;
                }
                break;
            }
        } else {
            switch(c) {
            case '\b':
            case 127:
                if(prompt.len) prompt.len--;
                break;
            case '\t':
                tab_list = !tab_list;
                break;
            case '\n': {
                // FIXME: sending empty message causes it to go bogus amogus
                if(prompt.len == 0) continue;
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
                    .author_id = userID,
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
                pbwrite(&client.pb, &req, sizeof(req));
                pbwrite(&client.pb, &send_msg, sizeof(send_msg));
                pbwrite(&client.pb, prompt.items, prompt.len);
                pbflush(&client.pb, &client);
                prompt.len = 0;
            } break;
            default:
                da_push(&prompt, c);
                break;
            }
        }
    }
end:
    closesocket(client.fd);
    return 0;
}
