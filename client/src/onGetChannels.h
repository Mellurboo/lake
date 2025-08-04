#pragma once

struct Client;
struct Response;
struct IncomingEvent;
void onGetChannels(struct Client* client, struct Response* response, struct IncomingEvent* event);
