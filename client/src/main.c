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
#include "client.h"
#include "time_unix.h"
#include "user_map.h"
#include "request.h"
#include "response.h"
#include "uibox.h"
#include "protocol.h"
#include "msg.h"
#include "notification.h"
#include "redraw.h"
#include "incoming_event.h"
#include "onUserInfo.h"
#include "onOkMessage.h"
#include "onNotification.h"
#include "onGetChannels.h"
#include "onGetMessageBefore.h"
#include "getUserInfoPacket.h"
#include "sendMsgRequest.h"
#include "messagesBefore.h"
#include "get_author_name.h"
#include "channel.h"

#if defined(__ANDROID__) || defined(_WIN32)
# define DISABLE_ALT_BUFFER 1
#endif

#ifdef _WIN32
#else
# include <time.h>
#endif

#define ALIGN16(n) (((n) + 15) & ~15)
#define ARRAY_LEN(a) (sizeof(a)/sizeof(*(a)))

UserMap user_map = { 0 };
Client client = {0};
// Generic response header
#define PORT 6969

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
        for(size_t j = y_offset_offset_start; j < msg_lines; ++j) {
            if((y_offset_start + (int32_t)j) < 0) {
                content_cursor += box_width - x_offset_start;
                x_offset_start = 0;
                continue;
            }
            for(size_t x_offset = x_offset_start; x_offset < box_width && content_cursor < content_end; ++x_offset) {
                stui_putchar(box.l + x_offset, box.t + y_offset_start + (int32_t)j, *content_cursor++);
            }
            x_offset_start = 0;
        }
    }
}

static stui_term_flag_t prev_term_flags = 0;
void restore_prev_term(void) {
    stui_term_set_flags(prev_term_flags);

#ifndef DISABLE_ALT_BUFFER
    // Alternate buffer.
    // The escape sequence below shouldn't do anything on terminals that don't support it
    printf("\033[?1049l");
    fflush(stdout);
#endif
}
typedef struct {
    char* items;
    size_t len, cap;
} StringBuilder;
IncomingEvent incoming_events[MAX_INCOMING_EVENTS];

uint32_t user_protocol_id = 0;
//TODO: getting info from db doesnt work during MessagesBeforePacket idk what about during notifications

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
            fprintf(stderr, "Bogus amogus packet_id=%u\n", resp.packet_id);
            continue;
        }
        if(!incoming_events[resp.packet_id].onEvent) {
            fprintf(stderr, "Unhandled bogus amogus packet_id=%u\n", resp.packet_id);
            continue;
        }
        incoming_events[resp.packet_id].onEvent(client, &resp, &incoming_events[resp.packet_id]);
    }
    fprintf(stderr, "Some sort of IO error occured. I don't know how to handle this right now. Probably just disconnect\n");
    fprintf(stderr, "ERROR: %s", sneterr());
    exit(1);
}

uint32_t dming;
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
static_assert(TAB_CATEGORIES_COUNT == 3, "Update tab category labels");
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
Channels dm_channels = { 0 };
TabLabels tab_labels = { 0 };
void redraw(void) {
    bool notDMING = dming == ~0u || dming == 0;

    for(size_t y = 0; y < term_height; ++y) {
        for(size_t x = 0; x < term_width; ++x) {
            stui_putchar(x, y, ' ');
        }
    }
    if(term_height < 3) {
        const char* too_small = "[Too small]";
        size_t x = term_width < strlen(too_small) ? 0 : (term_width - strlen(too_small)) / 2;
        size_t y = term_height / 2;
        while(*too_small && x < term_width) {
            stui_putchar_color(x++, y, too_small[0], STUI_RGB(0xFF0000), 0);
            too_small++;
        }
        stui_refresh();
        return;
    }
    UIBox term_box = {
        0, 0, term_width - 1, term_height - 1
    };
    UIBox tab_list_box;
    if(tab_list) {
        tab_labels.len = 0;
        if(notDMING) tab_list_box = term_box;
        else tab_list_box = uibox_chop_left(&term_box, (term_box.r - term_box.l) * 14 / 100);
        uibox_draw_border(tab_list_box, '=', '|', '+');
        switch(tab_list_state) {
        case TAB_LIST_STATE_CATEGORY:
            for(size_t i = 0; i < TAB_CATEGORIES_COUNT; ++i) {
                da_push(&tab_labels, tab_category_labels[i]);
            }
            break;
        case TAB_LIST_STATE_DMS:
            da_reserve(&tab_labels, dm_channels.len);
            for(size_t i = 0; i < dm_channels.len; ++i) {
                da_push(&tab_labels, dm_channels.items[i].name);
            }
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
        stui_refresh();
        stui_goto(uibox_inner(tab_list_box).l, uibox_inner(tab_list_box).t + tab_list_selection);
    }

    if(!notDMING){
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

typedef struct {
    uint32_t id;
    char name[];
} Auth;

uint32_t msg_protocol_id = 0;

static void freeMessage(Message* message) {
    free(message->content);
    memset(message, 0, sizeof(*message));
}
static void freeMessagesContents(Messages* msgs) {
    for(size_t i = 0; i < msgs->len; ++i) {
        freeMessage(&msgs->items[i]);
    }
    msgs->len = 0;
}
void loadChannel(Messages* msgs, uint32_t server_id, uint32_t channel_id) {
    //TODO: make so it works during other requests or refactor it
    Request request = {
        .protocol_id = msg_protocol_id,
        .func_id = 1,
        .packet_id = allocate_incoming_event(),
        .packet_len = sizeof(MessagesBeforeRequest)
    };
    incoming_events[request.packet_id].onEvent = onGetMessageBefore;
    incoming_events[request.packet_id].as.onGetMessagesBefore.msgs = msgs;
    request_hton(&request);
    uint64_t milis = time_unix_milis();
    MessagesBeforeRequest msgs_request = {
        .server_id = server_id,
        .channel_id = channel_id,
        .milis_low = milis,
        .milis_high = milis >> 32,
        .count = 100,
    };
    messagesBeforeRequest_hton(&msgs_request);
    client_write(&client, &request, sizeof(request));
    client_write(&client, &msgs_request, sizeof(msgs_request));
}

int main(int argc, const char** argv) {
    register_signals();
    gtinit();
    prev_term_flags = stui_term_get_flags();
#ifndef DISABLE_ALT_BUFFER
    printf("\033[?1049h");
    fflush(stdout);
#endif
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


    const char* key_name = NULL;

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
            assert(argc && "KEY PLS MOTHERFUCKA");
            key_name = shift_args(&argc, &argv);
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
    intptr_t e = client_write(&client, &(Request) { .protocol_id = 0, .func_id = 0, .packet_id = htonl(get_extensions_id), .packet_len = 0 }, sizeof(Request));
    if(e < 0) {
        fprintf(stderr, "FATAL: Failed to send request: %s\n", sneterr());
        return 1;
    }
    Response resp; 

    uint32_t auth_protocol_id = 0;
    // uint32_t echo_protocol_id = 0;

    uint32_t notify_protocol_id = 0;

    uint32_t channel_protocol_id = 0;
    for(;;) {
        e = client_read(&client, &resp, sizeof(resp));
        assert(e == 1);
        response_ntoh(&resp);
        assert(resp.packet_id == get_extensions_id);
        assert(resp.packet_len < 1024);
        if(resp.packet_len == 0) break;

        assert(resp.packet_len >= sizeof(uint32_t));
        size_t name_len = resp.packet_len - sizeof(uint32_t);
        Protocol* protocol = malloc(resp.packet_len + 1);
        assert(client_read(&client, protocol, resp.packet_len) == 1);
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
        if(strcmp(protocol->name, "channel") == 0) channel_protocol_id = protocol->id;
        free(protocol);
    }
    if(!msg_protocol_id) {
        fprintf(stderr, "FATAL: no msg protocol\n");
        return 1;
    } 
    uint32_t userID = 0;
    if(auth_protocol_id) {
        printf("Server requires auth\n");
        // List the supported authentication/encryption modes
        Request req = {
            .protocol_id = auth_protocol_id,
            .func_id = 0,
            .packet_id = 69,
            .packet_len = 0 
        };
        request_hton(&req);
        client_write(&client, &req, sizeof(req));
        uint32_t kyber_id = 0;
        const char* kyber_name = "KB786-AES256CTR";
        for(;;) {
            e = client_read(&client, &resp, sizeof(resp));
            assert(e == 1);
            response_ntoh(&resp);
            assert(resp.packet_id == 69);
            assert(resp.packet_len < 1024);
            if(resp.packet_len == 0) break;

            assert(resp.packet_len >= sizeof(uint32_t));
            size_t name_len = resp.packet_len - sizeof(uint32_t);
            Auth* auth = malloc(resp.packet_len + 1);
            assert(client_read(&client, auth, resp.packet_len) == 1);
            auth->id = ntohl(auth->id);
            auth->name[name_len] = '\0';
            fprintf(stderr, "INFO: Auth algorithm supported %u %s\n", auth->id, auth->name);
            if(strcmp(auth->name, kyber_name) == 0) kyber_id = auth->id;
            free(auth);
        }
        if(!kyber_id) {
            fprintf(stderr, "FATAL: Server does not support %s\n", kyber_name);
            return 1;
        }
        if(!key_name) {
            fprintf(stderr, "Please provide a key as the server requires it!\n");
            return 1;
        }
        char tmp_file_name[256] = {0};
        snprintf(tmp_file_name, sizeof(tmp_file_name), "%s.pub", key_name);
        size_t read_size = 0;
        uint8_t* pk = (uint8_t*)read_entire_file(tmp_file_name, &read_size);
        if(read_size != KYBER_PUBLICKEYBYTES || pk == NULL){
            fprintf(stderr, "Provide valid public key!\n");
            return 1;
        }
        snprintf(tmp_file_name, sizeof(tmp_file_name), "%s.priv", key_name);
        uint8_t* sk = (uint8_t*)read_entire_file(tmp_file_name, &read_size);
        if(read_size != KYBER_SECRETKEYBYTES || sk == NULL){
            fprintf(stderr, "Provide valid secret key!\n");
            return 1;
        }

        req = (Request) {
            .protocol_id = auth_protocol_id,
            .func_id = kyber_id,
            .packet_id = packet_id++,
            .packet_len = KYBER_PUBLICKEYBYTES
        };
        request_hton(&req);
        client_write(&client, &req, sizeof(req));
        client_write(&client, pk, KYBER_PUBLICKEYBYTES);
        e = client_read(&client, &resp, sizeof(resp));

        assert(e == 1);
        response_ntoh(&resp);
        assert(resp.opcode == 0);
        assert(resp.packet_len == KYBER_CIPHERTEXTBYTES + 16);
        uint8_t* cipherData = calloc(KYBER_CIPHERTEXTBYTES + 16, 1);
        assert(cipherData && "Just Buy More RAM");

        e = client_read(&client, cipherData, KYBER_CIPHERTEXTBYTES + 16);
        assert(e == 1);

        uint8_t* ss = calloc(KYBER_SSBYTES, 1);
        assert(ss && "Just Buy More RAM");

        crypto_kem_dec(ss, cipherData, sk);
        AES_init_ctx(&client.aes_ctx_read, ss);
        AES_init_ctx(&client.aes_ctx_write, ss);
        free(ss);

        AES_CTR_xcrypt_buffer(&client.aes_ctx_read, cipherData + KYBER_CIPHERTEXTBYTES, 16);

        req = (Request){
            .protocol_id = auth_protocol_id,
            .func_id = kyber_id,
            .packet_id = packet_id++,
            .packet_len = 16
        };
        request_hton(&req);
        client_write(&client, &req, sizeof(req));
        client_write(&client, cipherData + KYBER_CIPHERTEXTBYTES, 16);

        free(cipherData);
        e = client_read(&client, &resp, sizeof(resp));
        assert(e == 1);
        response_ntoh(&resp);
        assert(resp.opcode == 0);
        assert(resp.packet_len == sizeof(uint32_t));
        e = client_read(&client, &userID, sizeof(uint32_t));
        assert(e == 1);
        userID = ntohl(userID);

        client.secure = true;
        free(pk);
        free(sk);
    }
    dming = ~0;
    tab_list = true;
    /* 
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
        client_write(&client, &request, sizeof(request));
        client_write(&client, &msgs_request, sizeof(msgs_request));
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
                .author_id = msgs_resp.author_id,
                .milis = milis,
                .content_len = content_len,
                .content = content
            };
            da_insert(&msgs, 0, msg);
        }
    }
    */

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
        client_write(&client, &req, sizeof(Request));
    }
    if(channel_protocol_id) {
        uint32_t server_id = 0;
        Request req = {
            .protocol_id = channel_protocol_id,
            .func_id = 0,
            .packet_id = allocate_incoming_event(),
            .packet_len = sizeof(server_id),
        };
        incoming_events[req.packet_id].as.onGetChannels.channels = &dm_channels; 
        incoming_events[req.packet_id].onEvent = onGetChannels; 
        request_hton(&req);
        client_write(&client, &req, sizeof(Request));
        server_id = htonl(server_id);
        client_write(&client, &server_id, sizeof(server_id));
    }
    for(;;) {
        redraw();
        gtblockfd(fileno(stdin), GTBLOCKIN);
        int c = stui_get_key();
        if(tab_list) {
            if (c == '\t') {
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
                    if(tab_list_selection == TAB_CATEGORY_DMS) tab_list_state = TAB_LIST_STATE_DMS;
                    break;
                case STUI_KEY_UP:
                case STUI_KEY_DOWN:
                    uint32_t next_tab_tab_selection = (uint32_t)c == STUI_KEY_UP ? tab_list_selection - 1 : tab_list_selection + 1;
                    if(next_tab_tab_selection < TAB_CATEGORIES_COUNT) tab_list_selection = next_tab_tab_selection;
                    break;
                }
                break;
            case TAB_LIST_STATE_DMS:
                switch(c) {
                case '\n':
                    dming = dm_channels.items[tab_list_selection].id;
                    freeMessagesContents(&msgs);
                    loadChannel(&msgs, 0, dming);
                    break;
                case STUI_KEY_ESC:
                case 'b':
                case 'B':
                    tab_list_state = TAB_LIST_STATE_CATEGORY;
                    break;
                case STUI_KEY_UP:
                case STUI_KEY_DOWN:
                    uint32_t next_tab_tab_selection = (uint32_t)c == STUI_KEY_UP ? tab_list_selection - 1 : tab_list_selection + 1;
                    if(next_tab_tab_selection < dm_channels.len) tab_list_selection = next_tab_tab_selection;
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
                client_write(&client, &req, sizeof(req));
                client_write(&client, &send_msg, sizeof(send_msg));
                client_write(&client, prompt.items, prompt.len);
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
