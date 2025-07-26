#pragma once
#include <stddef.h>
typedef struct Client Client;
typedef struct Request Request;
typedef void (*protocol_func_t)(Client* client, Request* header);
typedef struct Protocol Protocol;
struct Protocol {
    const char* name;
    size_t funcs_count;
    protocol_func_t *funcs;
};
#define ARRAY_LEN(a) (sizeof(a)/sizeof(*(a)))
#define PROTOCOL(__name, __funcs) { .name = __name, .funcs_count = ARRAY_LEN(__funcs),  .funcs = __funcs }

