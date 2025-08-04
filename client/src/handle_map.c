#include "handle_map.h"
#include <assert.h>
#include <string.h>
#include "hash.h"

bool handle_map_reserve(HandleMap* map, size_t extra) {
    if(map->len + extra > map->buckets.len) {
        size_t ncap = map->buckets.len*2 + extra;
        HandleMapBucket** newbuckets = malloc(sizeof(*newbuckets)*ncap);
        if(!newbuckets) return false;
        memset(newbuckets, 0, sizeof(*newbuckets) * ncap);
        for(size_t i = 0; i < map->buckets.len; ++i) {
            HandleMapBucket* oldbucket = map->buckets.items[i];
            while(oldbucket) {
                HandleMapBucket* next = oldbucket->next;
                size_t hash = oldbucket->handle_hash % ncap;
                HandleMapBucket* newbucket = newbuckets[hash];
                oldbucket->next = newbucket;
                newbuckets[hash] = oldbucket;
                oldbucket = next;
            }
        }
        free(map->buckets.items);
        map->buckets.items = newbuckets;
        map->buckets.len = ncap;
    }
    return true;
}
HandleMapBucket* handle_map_insert(HandleMap* map, const char* handle) {
    if(!handle_map_reserve(map, 1)) return NULL;
    size_t hash = djb2(handle);
    size_t i = hash % map->buckets.len;
    HandleMapBucket* into = map->buckets.items[i];
    HandleMapBucket* bucket = calloc(1, sizeof(HandleMapBucket) + strlen(handle) + 1);
    if(!bucket) return NULL;
    bucket->next = into;
    bucket->handle_hash = hash;
    memcpy(bucket->handle, handle, strlen(handle));
    map->buckets.items[i] = bucket;
    map->len++;
    return bucket;
}
HandleMapBucket* handle_map_get(HandleMap* map, const char* handle) {
    if(map->len == 0) return NULL;
    assert(map->buckets.len > 0);
    size_t hash = djb2(handle);
    HandleMapBucket* bucket = map->buckets.items[hash % map->buckets.len];
    while(bucket) {
        if(bucket->handle_hash == hash && strcmp(bucket->handle, handle) == 0) return bucket;
        bucket = bucket->next;
    }
    return NULL;
}
HandleMapBucket* handle_map_get_or_insert(HandleMap* map, const char* handle) {
    HandleMapBucket* bucket = handle_map_get(map, handle);
    if(bucket) return bucket;
    return handle_map_insert(map, handle);
}
HandleMapBucket* handle_map_remove(HandleMap* map, const char* handle) {
    if(map->len == 0) return NULL;
    assert(map->buckets.len > 0);
    size_t hash = djb2(handle);
    HandleMapBucket* bucket = map->buckets.items[hash % map->buckets.len];
    HandleMapBucket** prev_next = &map->buckets.items[hash % map->buckets.len];
    while(bucket) {
        if(bucket->handle_hash == hash && strcmp(bucket->handle, handle) == 0) {
            *prev_next = bucket->next;
            return bucket;
        }
        prev_next = &bucket->next;
        bucket = bucket->next;
    }
    return NULL;
}
