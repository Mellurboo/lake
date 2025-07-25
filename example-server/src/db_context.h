#pragma once
#include <stdint.h>

typedef struct DbContext DbContext;

int DbContext_init(DbContext** dbOut);
void DbContext_free(DbContext* db);
int DbContext_get_user_id_from_pub_key(DbContext* db, uint8_t* pk, uint32_t* user_id);
int DbContext_get_pub_key_from_user_id(DbContext* db, uint32_t user_id, uint8_t** pk);
//caller has to clean up username
int DbContext_get_username_from_user_id(DbContext* db, uint32_t user_id, char** username);
