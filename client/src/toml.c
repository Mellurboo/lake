#include "toml.h"
#include <stdio.h>
#include <assert.h>
#include <ctype.h>

static void toml_trim(Toml* l) {
    while(l->cursor < l->end && isspace(*l->cursor)) l->cursor++;
}
#define toml_token(__kind, ...) (TomlToken){.kind=__kind, __VA_ARGS__}
TomlToken toml_next(Toml* l) {
    toml_trim(l);
    if(l->cursor == l->end) return toml_token(TOML_EOF);
    if(isalpha(*l->cursor)) {
        const char* start = l->cursor;
        while(l->cursor < l->end && (isalnum(*l->cursor) || *l->cursor == '_' || *l->cursor == '-')) l->cursor++;
        return toml_token(TOML_ATOM, .as.atom = { .data = start, .len = l->cursor - start });
    } else if (isdigit(*l->cursor)) {
        uint32_t integer = 0;
        while(l->cursor < l->end && isdigit(*l->cursor)) {
            integer = integer * 10 + (*l->cursor - '0');
            l->cursor++;
        }
        return toml_token(TOML_INTEGER, .as.integer = integer);
    } else if(*l->cursor == '=') return toml_token(*l->cursor++);
    else if(*l->cursor == '"') {
        const char* start = ++l->cursor;
        while(l->cursor < l->end && *l->cursor != '"') l->cursor++;
        size_t len = l->cursor - start;
        if(l->cursor < l->end && *l->cursor == '"') l->cursor++;
        return toml_token(TOML_STRING, .as.str = { .data = start, .len = len });
    }
    return toml_token(TOML_UNPARSABLE, .as.unparsable = *l->cursor);
}
TomlToken toml_peak(Toml* l) {
    const char* cursor = l->cursor;
    TomlToken t = toml_next(l);
    l->cursor = cursor;
    return t;
}
const char* toml_token_str_temp(TomlToken* t) {
    static_assert(TOML_TOKENS_COUNT - 256 == 5, "Update toml_token_str_temp");
    static char tmp_buf[512];
    switch(t->kind) {
    case TOML_EOF:
        return "End Of File";
    case TOML_ATOM:
        snprintf(tmp_buf, sizeof(tmp_buf), "%.*s", (int)t->as.atom.len, t->as.atom.data);
        return tmp_buf;
    case TOML_STRING:
        snprintf(tmp_buf, sizeof(tmp_buf), "\"%.*s\"", (int)t->as.str.len, t->as.str.data);
        return tmp_buf;
    case TOML_INTEGER:
        snprintf(tmp_buf, sizeof(tmp_buf), "%u", t->as.integer);
        return tmp_buf;
    case TOML_UNPARSABLE:
        snprintf(tmp_buf, sizeof(tmp_buf), "%c", t->as.unparsable);
        return tmp_buf;
    }
    return "BOGUS";
}
#define tkstr(kind, str) [kind - 256] = str
static_assert(TOML_TOKENS_COUNT - 256 == 5, "Update token_map");
static const char* token_map[] = {
    tkstr(TOML_ATOM, "Atom"),
    tkstr(TOML_INTEGER, "Integer"),
    tkstr(TOML_STRING, "String"),
    tkstr(TOML_EOF, "Eof"),
    tkstr(TOML_UNPARSABLE, "Unparsable")
};
bool toml_expect(Toml* l, int kind, TomlToken* t) {
    *t = toml_next(l);
    if(t->kind != kind) {
        fprintf(stderr, "(TOML) Expected ");
        if(kind < 256) fprintf(stderr, "%c", kind);
        else {
            assert(kind < TOML_TOKENS_COUNT);
            fprintf(stderr, "%s", token_map[kind - 256]);
        }
        fprintf(stderr, " but got %s\n", toml_token_str_temp(t));
        return false;
    }
    return true;
}
