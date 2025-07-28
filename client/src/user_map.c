#include "user_map.h"
#include <assert.h>
#include <string.h>

bool user_map_reserve(UserMap* map, size_t extra) {
    if(map->len + extra > map->buckets.len) {
        size_t ncap = map->buckets.len*2 + extra;
        UserMapBucket** newbuckets = malloc(sizeof(*newbuckets)*ncap);
        if(!newbuckets) return false;
        memset(newbuckets, 0, sizeof(*newbuckets) * ncap);
        for(size_t i = 0; i < map->buckets.len; ++i) {
            UserMapBucket* oldbucket = map->buckets.items[i];
            while(oldbucket) {
                UserMapBucket* next = oldbucket->next;
                size_t hash = ((size_t)oldbucket->user_id) % ncap;
                UserMapBucket* newbucket = newbuckets[hash];
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
UserMapBucket* user_map_insert(UserMap* map, uint32_t user_id) {
    if(!user_map_reserve(map, 1)) return NULL;
    size_t hash = ((size_t)user_id) % map->buckets.len;
    UserMapBucket* into = map->buckets.items[hash];
    UserMapBucket* bucket = calloc(sizeof(UserMapBucket), 1);
    if(!bucket) return NULL;
    bucket->next = into;
    bucket->user_id = user_id;
    map->buckets.items[hash] = bucket;
    map->len++;
    return bucket;
}
UserMapBucket* user_map_get(UserMap* map, uint32_t user_id) {
    if(map->len == 0) return NULL;
    assert(map->buckets.len > 0);
    size_t hash = ((size_t)user_id) % map->buckets.len;
    UserMapBucket* bucket = map->buckets.items[hash];
    while(bucket) {
        if(bucket->user_id == user_id) return bucket;
        bucket = bucket->next;
    }
    return NULL;
}
UserMapBucket* user_map_get_or_insert(UserMap* map, uint32_t user_id) {
    UserMapBucket* bucket = user_map_get(map, user_id);
    if(bucket) return bucket;
    return user_map_insert(map, user_id);
}
