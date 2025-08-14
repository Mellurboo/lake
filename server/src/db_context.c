#include "db_context.h"
#include "sqlite3/sqlite3.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "post_quantum_cryptography.h"
#include <assert.h>
#include "darray.h"

static int sqlite_callback(void* data, int argc, char** argv, char** azColName) {
    (void)data;
    for(int i = 0; i < argc; i++){
        printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
    }
    return 0;
}

int execute_sql(sqlite3* db, const char* sql){
    char* err_msg = NULL;

    int e = sqlite3_exec(
        db,
        sql,
        sqlite_callback,
        0,
        &err_msg
    );

    if(e != SQLITE_OK){
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
    }

    return e;
}

struct DbContext{
    sqlite3* db;
};

#define STRINGIFY0(x) # x
#define STRINGIFY1(x) STRINGIFY0(x)
int DbContext_init(DbContext** dbOut){
   DbContext* db = calloc(1, sizeof(DbContext));
   int e = sqlite3_open("database.db",&db->db);
   if(e != SQLITE_OK) return -1;
   e = execute_sql(db->db, "create table if not exists public_keys(key blob, user_id INTEGER, PRIMARY KEY(key), FOREIGN KEY(user_id) REFERENCES users(user_id));");
   if(e != SQLITE_OK) return -1;
   e = execute_sql(db->db, "create table if not exists users(user_id INTEGER UNIQUE PRIMARY KEY AUTOINCREMENT, username text);");
   if(e != SQLITE_OK) return -1;
   e = execute_sql(db->db, "create table if not exists dms(min_user_id INTEGER, max_user_id INTEGER)");
   if(e != SQLITE_OK) return -1;
   e = execute_sql(db->db, "create table if not exists user_handles(handle VARCHAR("STRINGIFY1(MAX_HANDLE_SIZE)") UNIQUE PRIMARY KEY, user_id INTEGER)");
   if(e != SQLITE_OK) return -1;
   e = execute_sql(db->db, "create table if not exists servers(server_id INTEGER UNIQUE PRIMARY KEY AUTOINCREMENT, server_name text)");
   if(e != SQLITE_OK) return -1;
   e = execute_sql(db->db, "create table if not exists last_read(user_id INTEGER, server_id INTEGER, channel_id INTEGER, last_milis BIGINT, UNIQUE(user_id, server_id, channel_id))");
   if(e != SQLITE_OK) return -1;

   *dbOut = db;
   return 0; 
}

void DbContext_free(DbContext* db){
    sqlite3_close(db->db);
    free(db);
}

int DbContext_get_user_id_from_pub_key(DbContext* db, uint8_t* pk, uint32_t* user_id){
    sqlite3_stmt *stmt;
    int e = sqlite3_prepare_v2(db->db, "select user_id from public_keys where key = ?", -1, &stmt, NULL);
    if(e != SQLITE_OK) return -1;

    e = sqlite3_bind_blob(stmt, 1, pk, KYBER_PUBLICKEYBYTES, SQLITE_STATIC);
    if(e != SQLITE_OK) return -1;

    if(sqlite3_step(stmt) == SQLITE_ROW) {
        *user_id = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return 0;
}

int DbContext_get_pub_key_from_user_id(DbContext* db, uint32_t user_id, uint8_t** pk){
    sqlite3_stmt *stmt;
    int e = sqlite3_prepare_v2(db->db, "select key from public_keys where user_id = ?", -1, &stmt, NULL);
    if(e != SQLITE_OK) return -1;

    e = sqlite3_bind_int(stmt, 1, user_id);
    if(e != SQLITE_OK) return -1;

    if(sqlite3_step(stmt) == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(stmt, 0); // this blob lives until next sqlite3_step or sqlite3_finalize
        *pk = calloc(KYBER_PUBLICKEYBYTES, sizeof(uint8_t));
        memcpy(*pk, blob, KYBER_PUBLICKEYBYTES);
    }

    sqlite3_finalize(stmt);
    return 0;
}

// Returns 1 on exists.
// Returns 0 on non exists
// Returns <0 on error
int DbContext_user_exists(DbContext* db, uint32_t user_id) {
    sqlite3_stmt *stmt;
    int e = sqlite3_prepare_v2(db->db, "select user_id from users where user_id = ?", -1, &stmt, NULL);
    if(e != SQLITE_OK) return -1;
    e = sqlite3_bind_int(stmt, 1, user_id);
    if(e != SQLITE_OK) return -1;
    e = 0;
    if(sqlite3_step(stmt) == SQLITE_ROW) {
        e = 1;
    }
    sqlite3_finalize(stmt);
    return e;
}

//caller has to clean up username
int DbContext_get_username_from_user_id(DbContext* db, uint32_t user_id, char** username){
    sqlite3_stmt *stmt;
    int e = sqlite3_prepare_v2(db->db, "select username from users where user_id = ?", -1, &stmt, NULL);
    if(e != SQLITE_OK) return -1;

    e = sqlite3_bind_int(stmt, 1, user_id);
    if(e != SQLITE_OK) return -1;

    if(sqlite3_step(stmt) == SQLITE_ROW) {
        const char* name = (const char*)sqlite3_column_text(stmt, 0); // this name lives only until next sqlite3_step or sqlite3_finalize
        *username = calloc(strlen(name)+1, sizeof(char));
        memcpy(*username, name, strlen(name)+1);
    }

    sqlite3_finalize(stmt);
    return 0;
}

int DbContext_create_dms_if_not_exist(DbContext* db, uint32_t min_user_id, uint32_t max_user_id){
    char buf[255] = {0};
    snprintf(buf, sizeof(buf), "create table if not exists dm_%u_%u(msg_id INTEGER UNIQUE PRIMARY KEY AUTOINCREMENT, author_id INTEGER, milis BIGINT, content TEXT)", min_user_id, max_user_id);
    int e = execute_sql(db->db, buf);

    sqlite3_stmt *stmt;
    e = sqlite3_prepare_v2(db->db, "select * from dms where min_user_id = ? and max_user_id = ?", -1, &stmt, NULL);
    if(e != SQLITE_OK) return -1;

    e = sqlite3_bind_int(stmt, 1, min_user_id);
    if(e != SQLITE_OK) return -1;
    e = sqlite3_bind_int(stmt, 2, max_user_id);
    if(e != SQLITE_OK) return -1;

    bool exists = sqlite3_step(stmt) == SQLITE_ROW;

    sqlite3_finalize(stmt);

    if(!exists){
        snprintf(buf, sizeof(buf), "insert into dms(min_user_id, max_user_id) values(%u, %u)", min_user_id, max_user_id);
        e = execute_sql(db->db, buf);
        if(e != SQLITE_OK) return -1;
    }

    return 0;
}

int DbContext_send_msg(DbContext* db, uint32_t server_id, uint32_t channel_id, uint32_t author_id, const char* content, size_t content_len, uint64_t milis){
    sqlite3_stmt *stmt;
    char buf[255] = {0};
    int e;
    if(server_id == 0){
        uint32_t max_user_id = author_id < channel_id ? channel_id : author_id;
        uint32_t min_user_id = author_id < channel_id ? author_id : channel_id;

        // Check if user exists
        int e = DbContext_user_exists(db, channel_id);
        // TODO: errors
        if(e <= 0) return -1;

        e = DbContext_create_dms_if_not_exist(db, min_user_id, max_user_id);
        // TODO: errors
        if(e < 0) return -1;

        snprintf(buf, sizeof(buf), "insert into dm_%u_%u(author_id, milis, content) values(?, ?, ?)", min_user_id, max_user_id);
        e = sqlite3_prepare_v2(db->db, buf, -1, &stmt, NULL);
        if(e != SQLITE_OK) goto sqlite_bind_err;
    }else{
        snprintf(buf, sizeof(buf), "insert into server_%u_%u(author_id, milis, content) values(?, ?, ?)", server_id, channel_id);
        e = sqlite3_prepare_v2(db->db, buf, -1, &stmt, NULL);
        if(e != SQLITE_OK) goto sqlite_bind_err;
    }
    e = sqlite3_bind_int(stmt, 1, author_id);
    if(e != SQLITE_OK) goto sqlite_bind_err;
    e = sqlite3_bind_int64(stmt, 2, milis);
    if(e != SQLITE_OK) goto sqlite_bind_err;
    e = sqlite3_bind_text(stmt, 3, content, content_len, SQLITE_STATIC);
    if(e != SQLITE_OK) goto sqlite_bind_err;
    e = sqlite3_step(stmt);
    if(e != SQLITE_DONE) goto sqlite_step_err;
    sqlite3_finalize(stmt);
    return 0;
sqlite_step_err:
sqlite_bind_err:
    sqlite3_finalize(stmt);
    return -1;
}
int DbContext_get_msgs_before(DbContext* db, uint32_t server_id, uint32_t channel_id, uint32_t author_id, uint64_t milis, uint32_t limit, Messages* msgs){
    sqlite3_stmt *stmt;
    char buf[255] = {0};
    int e;
    if(server_id == 0){
        uint32_t max_user_id = author_id < channel_id ? channel_id : author_id;
        uint32_t min_user_id = author_id < channel_id ? author_id : channel_id;

        snprintf(buf, sizeof(buf), "select author_id, milis, content from dm_%u_%u where milis < %lu order by milis desc limit %u",min_user_id, max_user_id, milis, limit);
        e = sqlite3_prepare_v2(db->db, buf, -1, &stmt, NULL);
        if(e != SQLITE_OK) return -1;
    }else{
        snprintf(buf, sizeof(buf), "select author_id, milis, content from server_%u_%u where milis < %lu order by milis desc limit %u",server_id, channel_id, milis, limit);
        e = sqlite3_prepare_v2(db->db, buf, -1, &stmt, NULL);
        if(e != SQLITE_OK) return -1;
    }

    msgs->len = 0;
    while(sqlite3_step(stmt) == SQLITE_ROW) {
        Message msg = {0};
        msg.author = sqlite3_column_int(stmt, 0); 
        msg.milis = sqlite3_column_int64(stmt, 1);
        const char* temp_content = (const char*)sqlite3_column_text(stmt, 2);
        msg.content_len = strlen(temp_content);
        msg.content = calloc(msg.content_len, 1);
        memcpy(msg.content, temp_content, msg.content_len);
        da_push(msgs, msg);
    }

    sqlite3_finalize(stmt);
    return 0;
}
int DbContext_get_channels(DbContext* db, uint32_t server_id, uint32_t author_id, Channels* channels) {
    sqlite3_stmt *stmt;
    char buf[255] = { 0 };
    int e;
    if(server_id == 0) {
        snprintf(buf, sizeof(buf), "select * from dms where min_user_id = %u or max_user_id = %u", author_id, author_id);
        e = sqlite3_prepare_v2(db->db, buf, -1, &stmt, NULL);
        if(e != SQLITE_OK) return -1;
        channels->len = 0;
        while(sqlite3_step(stmt) == SQLITE_ROW) {
            Channel channel = { 0 };
            uint32_t min_user_id = sqlite3_column_int(stmt, 0);
            uint32_t max_user_id = sqlite3_column_int(stmt, 1);
            channel.id = min_user_id == author_id ? max_user_id : min_user_id;
            channel.name = NULL;
            da_push(channels, channel);
        }
        sqlite3_finalize(stmt);
        for(size_t i = 0; i < channels->len; ++i) {
            uint32_t id = channels->items[i].id;
            e = DbContext_get_username_from_user_id(db, id, &channels->items[i].name);
            if(e < 0) return -1;
            if(channels->items[i].name == NULL) return -1;
            fprintf(stderr, "Okay got username: `%s`\n", channels->items[i].name);
        }
    }else{
        snprintf(buf, sizeof(buf), "select channel_id, channel_name from server_%u", server_id);
        e = sqlite3_prepare_v2(db->db, buf, -1, &stmt, NULL);
        if(e != SQLITE_OK) return -1;
        channels->len = 0;
        while(sqlite3_step(stmt) == SQLITE_ROW) {
            Channel channel = { 0 };
            channel.id = sqlite3_column_int(stmt, 0);
            channel.name = (char*)sqlite3_column_text(stmt, 1);
            channel.name = strdup(channel.name);
            da_push(channels, channel);
        }
        sqlite3_finalize(stmt);
    }

    for(size_t i = 0; i < channels->len; i++){
        Channel* channel = &channels->items[i];
        channel->last_read_milis = 0;
        channel->newest_msg_milis = 0;

        e = sqlite3_prepare_v2(db->db, "SELECT last_milis FROM last_read WHERE server_id = ? AND user_id = ? AND channel_id = ? ORDER BY last_milis ASC LIMIT 1", -1, &stmt, 0);
        if(e != SQLITE_OK) return -1;

        e = sqlite3_bind_int(stmt, 1, server_id);
        if(e != SQLITE_OK) return -1;

        e = sqlite3_bind_int(stmt, 2, author_id);
        if(e != SQLITE_OK) return -1;

        e = sqlite3_bind_int(stmt, 3, channel->id);
        if(e != SQLITE_OK) return -1;

        if(sqlite3_step(stmt) == SQLITE_ROW) {
            channel->last_read_milis = sqlite3_column_int64(stmt, 0);
        }

        sqlite3_finalize(stmt);

        char buf[255] = {0};

        if(server_id == 0){
            uint32_t max_user_id = author_id < channel->id ? channel->id : author_id;
            uint32_t min_user_id = author_id < channel->id ? author_id : channel->id;
            snprintf(buf, sizeof(buf), "SELECT milis from dm_%u_%u order by milis DESC limit 1", min_user_id, max_user_id);
        }else{
            snprintf(buf, sizeof(buf), "SELECT milis from server_%u_%u order by milis DESC limit 1", server_id, channel->id);
        }
        
        e = sqlite3_prepare_v2(db->db, buf, -1, &stmt, 0);
        if(e != SQLITE_OK) return -1;

        if(sqlite3_step(stmt) == SQLITE_ROW) {
            channel->newest_msg_milis = sqlite3_column_int64(stmt, 0);
        }

        sqlite3_finalize(stmt);
    }
    return 0;
}
int DbContext_get_user_id_from_handle(DbContext* db, const char* handle, size_t handle_len, uint32_t* user_id) {
    sqlite3_stmt *stmt;
    int e = sqlite3_prepare_v2(db->db, "select user_id from user_handles where handle = ?", -1, &stmt, NULL);
    if(e != SQLITE_OK) return -1;
    e = sqlite3_bind_text(stmt, 1, handle, handle_len, SQLITE_STATIC);
    if(e != SQLITE_OK) goto sqlite_bind_err;
    // NOTE: assumes step does not fail
    if(sqlite3_step(stmt) == SQLITE_ROW) {
        *user_id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return 0;
sqlite_bind_err:
    sqlite3_finalize(stmt);
    return -1;
}
void free_channel(Channel* channel) {
    free(channel->name);
    memset(channel, 0, sizeof(*channel));
}
void free_channels(Channels* channels) {
    for(size_t i = 0; i < channels->len; ++i) {
        free_channel(&channels->items[i]);
    }
    free(channels->items);
    memset(channels, 0, sizeof(*channels));
}

int DbContext_get_servers(DbContext* db, uint32_t author_id, Servers* servers){
    sqlite3_stmt *stmt;
    int e = sqlite3_prepare_v2(db->db, "select server_id, server_name from servers", -1, &stmt, 0);
    if(e != SQLITE_OK) return -1;

    servers->len = 0;
    while(sqlite3_step(stmt) == SQLITE_ROW){
        Server server = {0};
        server.id = sqlite3_column_int(stmt, 0);
        server.name = (char*)sqlite3_column_text(stmt, 1);
        server.name = strdup(server.name);
        da_push(servers, server);
    }

    sqlite3_finalize(stmt);

    for(size_t i = 0; i < servers->len; i++){
        Server* server = &servers->items[i];
        server->last_read_milis = 0;
        server->newest_msg_milis = 0;

        int channel_id = 0;

        e = sqlite3_prepare_v2(db->db, "SELECT last_milis, channel_id FROM last_read WHERE server_id = ? AND user_id = ? ORDER BY last_milis ASC LIMIT 1", -1, &stmt, 0);
        if(e != SQLITE_OK) return -1;

        e = sqlite3_bind_int(stmt, 1, server->id);
        if(e != SQLITE_OK) return -1;

        e = sqlite3_bind_int(stmt, 2, author_id);
        if(e != SQLITE_OK) return -1;

        if(sqlite3_step(stmt) == SQLITE_ROW) {
            server->last_read_milis = sqlite3_column_int64(stmt, 0);
            channel_id = sqlite3_column_int(stmt, 1);
        }

        sqlite3_finalize(stmt);

        // TODO: we have undefined behaviour here if a new user who did not interact what so ever with server asks for the servers then in database there wont be any rows in last_read for that server so we wont be able to find a newest_msg_milis (iterating through all channels doesnt seem like a good idea) so for now it will be 0 in that case
        if(channel_id != 0){
            char buf[256];
            snprintf(buf, sizeof(buf), "SELECT milis from server_%u_%u order by milis DESC limit 1", server->id, channel_id);
            e = sqlite3_prepare_v2(db->db, buf, -1, &stmt, 0);
            if(e != SQLITE_OK) return -1;
            
            if(sqlite3_step(stmt) == SQLITE_ROW) {
                server->newest_msg_milis = sqlite3_column_int64(stmt, 0);
            }

            sqlite3_finalize(stmt);
        }
    }
    return 0;
}

void free_server(Server* server){
    free(server->name);
    memset(server, 0, sizeof(*server));
}

void free_servers(Servers* servers){
    for(size_t i = 0; i < servers->len; ++i){
        free_server(&servers->items[i]);
    }
    free(servers->items);
    memset(servers, 0, sizeof(*servers));
}

int DbContext_set_last_read(DbContext* db, uint32_t author_id, uint32_t server_id, uint32_t channel_id, uint64_t milis){
    
    char buf[512];

    snprintf(buf, sizeof(buf), "insert into last_read(user_id, server_id, channel_id, last_milis) values (%u, %u, %u, %lu) ON CONFLICT(user_id, server_id, channel_id) DO UPDATE SET last_milis = excluded.last_milis", author_id, server_id, channel_id, milis);

    int e = execute_sql(db->db, buf);
    if(e != SQLITE_OK) return -1;

    return 0;
}
