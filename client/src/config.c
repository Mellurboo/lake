#include "config.h"
#include <fileutils.h>
#include "toml.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define toml_expect_defer(...) if(!toml_expect(__VA_ARGS__)) { e = false; goto defer; }
bool configServer_load_from_file(ConfigServer* config, const char* path) {
    bool e = true;
    size_t size;
    const char* src = read_entire_file(path, &size);
    if(!src) return false;
    Toml toml = { 
        .cursor = src, .end = src + size
    };
    TomlToken t = { 0 };
    while(toml_peak(&toml).kind != TOML_EOF) {
        toml_expect_defer(&toml, TOML_ATOM, &t);
        TomlSv name = t.as.atom;
        toml_expect_defer(&toml, '=', &t);
        if(toml_sveq(name, "hostname")) {
            toml_expect_defer(&toml, TOML_STRING, &t);
            // FIXME: we do leak some memory but who cares
            config->hostname = calloc(t.as.str.len + 1, sizeof(char));
            assert(config->hostname && "Just buy more RAM");
            memcpy(config->hostname, t.as.str.data, t.as.str.len);
        } else if(toml_sveq(name, "port")) {
            toml_expect_defer(&toml, TOML_INTEGER, &t);
            config->port = t.as.integer;
        } else toml_next(&toml); // Ignore additional variables
    }
defer:
    free((void*)src);
    return e;
}
