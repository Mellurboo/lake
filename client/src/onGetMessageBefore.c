#include "onGetMessageBefore.h"
#include "client.h"
#include "response.h"
#include "incoming_event.h"
#include "redraw.h"
#include "darray.h"
#include <assert.h>
#include <arpa/inet.h>
#include "channel.h"
#include "string.h"
#include "redraw.h"
#include "messagesBefore.h"

void onGetMessageBefore(Client* client, Response* resp, IncomingEvent* event){
    // TODO: ^^verify things
    // TODO: I don't know how to handle such case:
    if(resp->packet_len == 0) {
        redraw();
        event->onEvent = NULL;
        return;
    }
    size_t content_len = resp->packet_len - sizeof(MessagesBeforeResponse);
    char* content = malloc(content_len);
    MessagesBeforeResponse msgs_resp;
    client_read(client, &msgs_resp, sizeof(MessagesBeforeResponse));
    messagesBeforeResponse_ntoh(&msgs_resp);
    uint64_t milis = (((uint64_t)msgs_resp.milis_high) << 32) | (uint64_t)msgs_resp.milis_low;
    client_read(client, content, content_len);

    // TODO: verify all this sheize^
    Message msg = {
        .author_id = msgs_resp.author_id,
        .milis = milis,
        .content_len = content_len,
        .content = content
    };
    da_insert(event->as.onGetMessagesBefore.msgs, 0, msg);
}
