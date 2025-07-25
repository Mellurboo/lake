#include "db_context.h"
#include "sqlite3/sqlite3.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "post_quantum_cryptography.h"

static int sqlite_callback(void* data, int argc, char** argv, char** azColName) {
    (void)data;
    for(int i = 0; i < argc; i++){
        printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
    }
    return 0;
}

void execute_sql(sqlite3* db, const char* sql){
    char* err_msg = NULL;

    int rc = sqlite3_exec(
        db,
        sql,
        sqlite_callback,
        0,
        &err_msg
    );

    if(rc != SQLITE_OK){
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
    }
}

struct DbContext{
    sqlite3* db;
};

int DbContext_init(DbContext** dbOut){
   DbContext* db = calloc(sizeof(DbContext), 1);
   int e = sqlite3_open("database.db",&db->db);

   execute_sql(db->db, "create table if not exists public_keys(key blob, user_id INTEGER, PRIMARY KEY(key), FOREIGN KEY(user_id) REFERENCES users(id));");
   execute_sql(db->db, "create table if not exists users(user_id INTEGER UNIQUE PRIMARY KEY AUTOINCREMENT, username text);");

   *dbOut = db;
   return e == SQLITE_OK ? 0 : -1;
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
