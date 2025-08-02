#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
// A little TOML lexer
// NOTE: Not a formal TOML parser.
typedef struct {
    const char *cursor, *end;
} Toml;
enum {
    TOML_ATOM=256,
    TOML_INTEGER,
    TOML_STRING,
    TOML_EOF,
    TOML_UNPARSABLE,
    TOML_TOKENS_COUNT,
};
typedef struct {
    const char* data;
    size_t len;
} TomlSv;
typedef struct {
    int kind;
    union {
        TomlSv atom, str;
        int unparsable;
        uint32_t integer;
    } as;
} TomlToken;
TomlToken toml_next(Toml* l);
TomlToken toml_peak(Toml* l);
bool toml_expect(Toml* l, int kind, TomlToken* t);

static bool toml_sveq_sized(TomlSv sv, const char* data, size_t len) {
    return sv.len == len && memcmp(sv.data, data, len) == 0;
}
static bool toml_sveq(TomlSv sv, const char* cstr) {
    return toml_sveq_sized(sv, cstr, strlen(cstr));
}
