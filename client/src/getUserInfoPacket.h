#pragma once
#include <stdint.h>

typedef struct {
    uint32_t userID;
} GetUserInfoPacket;

void getUserInfoPacket_hton(GetUserInfoPacket* packet);
