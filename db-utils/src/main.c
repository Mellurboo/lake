#define NOB_STRIP_PREFIX
#define NOB_IMPLEMENTATION
#include "../../nob.h"

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "sqlite3/sqlite3.h"

int execute_sql(sqlite3* db, const char* sql){
    char* err_msg = NULL;

    int e = sqlite3_exec(
        db,
        sql,
        NULL,
        0,
        &err_msg
    );

    if(e != SQLITE_OK){
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
    }

    return e;
}

typedef struct{
    const char* public_key_filename;
    const char* username;
} User;

typedef struct{
    User* items;
    size_t count, capacity;
} Users;

typedef struct{
    const char** items;
    size_t count, capacity;
} Servers;

typedef struct{
    const char* server_name;
    const char* channel_name;
} Channel;

typedef struct{
    Channel* items;
    size_t count, capacity;
} Channels;

void usage(const char* program){
    printf("[USAGE] %s [-u <public key filepath> <username> || -s <new server name> || -ch <server name> <new channel name>] || --help\n", program);
}
#define MAX_HANDLE_SIZE 64
#define STRINGIFY0(x) # x
#define STRINGIFY1(x) STRINGIFY0(x)
int main(int argc, char** argv){
    const char* program = shift_args(&argc, &argv);
    (void)program;

    Users users = {0};
    Servers servers = {0};
    Channels channels = {0};

    while(argc){
        const char* arg = shift_args(&argc, &argv);

        if(strcmp(arg, "-u") == 0){
            User user = {0};
            if(!argc){
                usage(program);
                fprintf(stderr, "[ERROR] expected public key filename\n");
                return 1;
            }

            user.public_key_filename = shift_args(&argc, &argv);

            if(!argc){
                usage(program);
                fprintf(stderr, "[ERORR] expected username after public key\n");
                return 1;
            }

            user.username = shift_args(&argc, &argv);
            da_append(&users, user);
            continue;
        }else if(strcmp(arg, "-s") == 0){
            if(!argc){
                usage(program);
                fprintf(stderr, "[ERROR] expected server name\n");
                return 1;
            }
            const char* server_name = shift_args(&argc, &argv);
            da_append(&servers, server_name);
            continue;
        }else if(strcmp(arg, "-ch") == 0){
            Channel channel = {0};
            if(!argc){
                usage(program);
                fprintf(stderr, "[ERROR] expected server name\n");
                return 1;
            }
            channel.server_name = shift_args(&argc, &argv);
            if(!argc){
                usage(program);
                fprintf(stderr, "[ERROR] expected server name\n");
                return 1;
            }
            channel.channel_name = shift_args(&argc, &argv);
            da_append(&channels, channel);
            continue;
        }else if(strcmp(arg, "--help") == 0){
            usage(program);
            return 0;
        }

        usage(program);
        fprintf(stderr, "[ERROR] Unknown flag %s\n", arg);
        return 1;
    }

    sqlite3* db;

    if (sqlite3_open("database.db",&db) != SQLITE_OK){ 
        fprintf(stderr, "[ERROR] Couldn't open database");
        return 1;
    }

    int e = execute_sql(db, "create table if not exists public_keys(key blob, user_id INTEGER, PRIMARY KEY(key), FOREIGN KEY(user_id) REFERENCES users(id));");
    if(e != SQLITE_OK) return -1;
    e = execute_sql(db, "create table if not exists users(user_id INTEGER UNIQUE PRIMARY KEY AUTOINCREMENT, username text);");
    if(e != SQLITE_OK) return -1;
    e = execute_sql(db, "create table if not exists dms(min_user_id INTEGER, max_user_id INTEGER)");
    if(e != SQLITE_OK) return -1;
    e = execute_sql(db, "create table if not exists user_handles(handle VARCHAR("STRINGIFY1(MAX_HANDLE_SIZE)") UNIQUE PRIMARY KEY, user_id INTEGER)");
    if(e != SQLITE_OK) return -1;
   e = execute_sql(db, "create table if not exists servers(server_id INTEGER UNIQUE PRIMARY KEY AUTOINCREMENT, server_name text)");
   if(e != SQLITE_OK) return -1;
   e = execute_sql(db, "create table if not exists last_read(user_id INTEGER, server_id INTEGER, channel_id INTEGER, last_milis BIGINT, UNIQUE(user_id, server_id, channel_id))");
   if(e != SQLITE_OK) return -1;

    String_Builder sb = {0};
    sqlite3_stmt *stmt;
    for(size_t i = 0; i < users.count; i++){
        sb.count = 0;
        User* user = &users.items[i];

        if(!read_entire_file(user->public_key_filename, &sb)) {
            fprintf(stderr, "Invalid public key filename \"%s\"", user->public_key_filename);
            return 1;
        }

        e = execute_sql(db, temp_sprintf("insert into users(username) values (\"%s\")", user->username));
        if(e != SQLITE_OK) return -1;
        size_t userID = sqlite3_last_insert_rowid(db);
        e = execute_sql(db, temp_sprintf("insert into user_handles(handle, user_id) values (\"%s\", %lu)", user->username, userID));
        if(e != SQLITE_OK) return -1;
        e = sqlite3_prepare_v2(db, "insert into public_keys(key, user_id) values (?, ?);", -1, &stmt, 0);
        if(e != SQLITE_OK) return -1;

        e = sqlite3_bind_blob(stmt, 1, sb.items, sb.count, SQLITE_STATIC);
        if(e != SQLITE_OK) return -1;
        e = sqlite3_bind_int(stmt, 2, userID);
        if(e != SQLITE_OK) return -1;

        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    for(size_t i = 0; i < servers.count; i++){
        const char* server_name = servers.items[i];

        e = execute_sql(db, temp_sprintf("insert into servers(server_name) values (\"%s\")", server_name));
        if(e != SQLITE_OK) return -1;
        size_t serverID = sqlite3_last_insert_rowid(db);
        e = execute_sql(db, temp_sprintf("create table if not exists server_%lu(channel_id INTEGER UNIQUE PRIMARY KEY AUTOINCREMENT, channel_name text)", serverID));
        if(e != SQLITE_OK) return -1;
    }

    for(size_t i = 0; i < channels.count; i++){
        Channel* channel = &channels.items[i];

        e = sqlite3_prepare_v2(db, "select server_id from servers where server_name = ?", -1, &stmt, 0);
        if(e != SQLITE_OK) return -1;

        e = sqlite3_bind_text(stmt, 1, channel->server_name, strlen(channel->server_name), SQLITE_STATIC);
        if(e != SQLITE_OK) return -1;

        size_t server_id = ~0u;
        if(sqlite3_step(stmt) == SQLITE_ROW){
           server_id = sqlite3_column_int(stmt, 0); 
        }

        sqlite3_finalize(stmt);
        if(server_id == ~0u){
            fprintf(stderr, "[SQL_ERROR] Provided non existent server name %s", channel->server_name);
            return 1;
        }


        e = execute_sql(db, temp_sprintf("insert into server_%lu(channel_name) values (\"%s\")", server_id, channel->channel_name));
        if(e != SQLITE_OK) return -1;
        size_t channel_id = sqlite3_last_insert_rowid(db);
        e = execute_sql(db, temp_sprintf("create table if not exists server_%lu_%lu(msg_id INTEGER UNIQUE PRIMARY KEY AUTOINCREMENT, author_id INTEGER, milis BIGINT, content TEXT)", server_id, channel_id));
        if(e != SQLITE_OK) return -1;
    }

    sqlite3_close(db);

    return 0;
}
