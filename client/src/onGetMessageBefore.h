#pragma once
#include "client.h"
#include "response.h"

typedef struct IncomingEvent IncomingEvent;
void onGetMessageBefore(Client* client, Response* response, IncomingEvent* event);
