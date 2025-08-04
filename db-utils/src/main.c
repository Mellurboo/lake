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

void usage(const char* program){
    printf("[USAGE] %s [-u <public key filepath> <username>] || --help\n", program);
}
#define MAX_HANDLE_SIZE 64
#define STRINGIFY0(x) # x
#define STRINGIFY1(x) STRINGIFY0(x)
int main(int argc, char** argv){
    const char* program = shift_args(&argc, &argv);
    (void)program;

    Users users = {0};

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



    sqlite3_close(db);

    return 0;
}
