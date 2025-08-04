#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct HandleMapBucket HandleMapBucket;
struct HandleMapBucket {
    HandleMapBucket* next;
    uint32_t user_id;
    bool in_progress;
    size_t handle_hash;
    char handle[];
};
typedef struct {
    struct {
        HandleMapBucket** items;
        size_t len;
    } buckets;
    size_t len;
} HandleMap;

bool handle_map_reserve(HandleMap* map, size_t extra);
HandleMapBucket* handle_map_insert(HandleMap* map, const char* handle);
HandleMapBucket* handle_map_get(HandleMap* map, const char* handle);
HandleMapBucket* handle_map_get_or_insert(HandleMap* map, const char* handle);
HandleMapBucket* handle_map_remove(HandleMap* map, const char* handle);
