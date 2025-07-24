#define NOB_STRIP_PREFIX
#define NOB_IMPLEMENTATION
#include "../../nob.h"

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "sqlite3/sqlite3.h"

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

int main(int argc, char** argv){
    if(argc < 3){
        fprintf(stderr, "Usage %s <key.pub filename> <username>", argv[0]);
    }

    sqlite3* db;

    if (sqlite3_open("database.db",&db) != SQLITE_OK){ 
        fprintf(stderr, "Couldn't open database");
        return 1;
    }

    execute_sql(db, temp_sprintf("insert into users(username) values (\"%s\")", argv[2]));

    size_t userID = sqlite3_last_insert_rowid(db);

    String_Builder sb = {0};

    if(!read_entire_file(argv[1], &sb)) {
        fprintf(stderr, "Invalid public key filename");
        return 1;
    }

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, "insert into public_keys(key, user_id) values (?, ?);", -1, &stmt, 0);
    if(rc != SQLITE_OK){
        printf("error!\n");
        return 1;
    }

    sqlite3_bind_blob(stmt, 1, sb.items, sb.count, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, userID);

    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE){
        printf("error\n");
        return 1;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return 0;
}
