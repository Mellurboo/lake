#include "hash.h"
size_t djb2(const char *data) {
    size_t hash = 5381;
    uint8_t c;

    while (c = *data++) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }

    return hash;
}
