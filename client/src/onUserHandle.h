#pragma once
struct Client;
struct Response;
struct IncomingEvent;
void onUserHandle(struct Client* client, struct Response* response, struct IncomingEvent* event);
