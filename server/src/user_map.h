#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <list_head.h>

typedef struct UserMapBucket UserMapBucket;
struct UserMapBucket {
    UserMapBucket* next;
    uint32_t user_id;
    struct list_head clients;
};
typedef struct {
    struct {
        UserMapBucket** items;
        size_t len;
    } buckets;
    size_t len;
} UserMap;

bool user_map_reserve(UserMap* map, size_t extra);
UserMapBucket* user_map_insert(UserMap* map, uint32_t user_id);
UserMapBucket* user_map_get(UserMap* map, uint32_t user_id);
UserMapBucket* user_map_get_or_insert(UserMap* map, uint32_t user_id);

