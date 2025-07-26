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

int DbContext_init(DbContext** dbOut){
   DbContext* db = calloc(sizeof(DbContext), 1);
   int e = sqlite3_open("database.db",&db->db);
   if(e != SQLITE_OK) return -1;
   e = execute_sql(db->db, "create table if not exists public_keys(key blob, user_id INTEGER, PRIMARY KEY(key), FOREIGN KEY(user_id) REFERENCES users(id));");
   if(e != SQLITE_OK) return -1;
   e = execute_sql(db->db, "create table if not exists users(user_id INTEGER UNIQUE PRIMARY KEY AUTOINCREMENT, username text);");
   if(e != SQLITE_OK) return -1;
   e = execute_sql(db->db, "create table if not exists dms(min_user_id INTEGER, max_user_id INTEGER)");
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

        sqlite3_stmt *stmt;
        char buf[255] = {0};
        snprintf(buf, sizeof(buf), "insert into dm_%u_%u(author_id, milis, content) values(?, ?, ?)", min_user_id, max_user_id);
        e = sqlite3_prepare_v2(db->db, buf, -1, &stmt, NULL);
        if(e != SQLITE_OK) goto sqlite_bind_err;
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
    //TODO: we assert its DMs
    assert(false && "TODO: everything other than DMs");
}
int DbContext_get_msgs_before(DbContext* db, uint32_t server_id, uint32_t channel_id, uint32_t author_id, uint64_t milis, Messages* msgs){
    if(server_id == 0){
        uint32_t max_user_id = author_id < channel_id ? channel_id : author_id;
        uint32_t min_user_id = author_id < channel_id ? author_id : channel_id;

        sqlite3_stmt *stmt;
        char buf[255] = {0};
        snprintf(buf, sizeof(buf), "select author_id, milis, content from dm_%u_%u where milis < %lu",min_user_id, max_user_id, milis);
        int e = sqlite3_prepare_v2(db->db, buf, -1, &stmt, NULL);
        if(e != SQLITE_OK) return -1;

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
    //TODO: we assert its DMs
    assert(false && "TODO: everything other than DMs");
    return 0;
}
