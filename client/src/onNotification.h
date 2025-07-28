#pragma once
#include "client.h"
#include "response.h"

typedef struct IncomingEvent IncomingEvent;
void onNotification(Client* client, Response* response, IncomingEvent* event);
