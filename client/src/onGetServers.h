#pragma once

struct Client;
struct Response;
struct IncomingEvent;
void onGetServers(struct Client* client, struct Response* response, struct IncomingEvent* event);
