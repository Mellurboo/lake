#pragma once

typedef struct {
    uint64_t milis;
    uint32_t author_id;
    uint32_t content_len;
    char* content;
} Message;

typedef struct {
    Message* items;
    size_t len, cap;
} Messages;
