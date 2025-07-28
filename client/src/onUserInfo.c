#include "onUserInfo.h"
#include "incoming_event.h"
#include "redraw.h"
#include "user_map.h"
#include "msg.h"

void onUserInfo(Client* client, Response* response, IncomingEvent* event) {
    event->onEvent = NULL;
    UserMapBucket* user = event->as.onUserInfo.user;
    // NOTE: not entirely necessary but who cares
    // readability I guess
    user->in_progress = false;
    if(response->packet_len == 0) {
        user->username = "BOGUS";
        return;
    }
    if(response->packet_len > 128) {
        user->username = "<Too long>";
        // FIXME: discard packet data on here
        return;
    }
    user->username = calloc(response->packet_len + 1, 1);
    int e = client_read(client, user->username, response->packet_len);
    (void)e;
    redraw();
}
