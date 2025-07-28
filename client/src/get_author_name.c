#include "get_author_name.h"
#include "user_map.h"
#include "incoming_event.h"
#include "onUserInfo.h"
#include "request.h"
#include "getUserInfoPacket.h"
#include <stdio.h>

extern UserMap user_map;
extern int user_protocol_id;

char* get_author_name(Client* client, uint32_t author_id){
    UserMapBucket* user = user_map_get_or_insert(&user_map, author_id);

    if(user->username == NULL && !user->in_progress) {
        if(user_protocol_id == 0) return NULL;
        GetUserInfoPacket packet = {
            .userID = author_id
        };

        getUserInfoPacket_hton(&packet);
        user->in_progress = true;
        Request request = {
            .protocol_id = user_protocol_id,
            .func_id = 0,
            .packet_id = allocate_incoming_event(),
            .packet_len = sizeof(GetUserInfoPacket)
        };
        incoming_events[request.packet_id].as.onUserInfo.user = user;
        incoming_events[request.packet_id].onEvent = onUserInfo;
        request_hton(&request);
        client_write(client, &request, sizeof(Request));
        client_write(client, &packet, sizeof(packet));
        // TODO: error here?
    }

    return user->username;
}
