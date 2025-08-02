#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char* hostname;
    uint16_t port;
} ConfigServer;
bool configServer_load_from_file(ConfigServer* server, const char* path);
