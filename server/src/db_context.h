#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MAX_HANDLE_SIZE 64
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
    uint32_t id;
    char* name;
} Channel;
void free_channel(Channel* channel);
typedef struct {
    Channel* items;
    size_t len, cap;
} Channels;
void free_channels(Channels* channels);


typedef struct DbContext DbContext;

int DbContext_init(DbContext** dbOut);
void DbContext_free(DbContext* db);
int DbContext_get_user_id_from_pub_key(DbContext* db, uint8_t* pk, uint32_t* user_id);
int DbContext_get_pub_key_from_user_id(DbContext* db, uint32_t user_id, uint8_t** pk);
//caller has to clean up username
//It returns 0 on successful querry but it may not have found it.
//if no user was found wiht ID, username is NULL
int DbContext_get_username_from_user_id(DbContext* db, uint32_t user_id, char** username);

int DbContext_send_msg(DbContext* db, uint32_t server_id, uint32_t channel_id, uint32_t author_id, const char* content, size_t content_len, uint64_t milis);
int DbContext_get_msgs_before(DbContext* db, uint32_t server_id, uint32_t channel_id, uint32_t author_id, uint64_t milis, uint32_t limit, Messages* msgs);
int DbContext_get_channels(DbContext* db, uint32_t server_id, uint32_t author_id, Channels* channels);
int DbContext_get_user_id_from_handle(DbContext* db, const char* handle, size_t handle_len, uint32_t* user_id);
