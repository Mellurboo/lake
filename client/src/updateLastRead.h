#pragma once
#include <stdint.h>

void updateLastRead(uint32_t server_id, uint32_t channel_id, uint64_t milis);
void updateNewestMessage(uint32_t server_id, uint32_t channel_id, uint64_t milis);
