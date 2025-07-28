#pragma once
#include "client.h"
#include "response.h"
#include "user_map.h"
#include "msg.h"

#define MAX_INCOMING_EVENTS 128
typedef struct IncomingEvent IncomingEvent;
typedef struct { Messages* msgs; Message msg; } OnMessage;
typedef struct { Messages* msgs; } OnNotification;
typedef struct { UserMapBucket* user; } OnUserInfo;
typedef struct { uint32_t bogus_amogus; } OnGetChannels;
typedef void (*event_handler_t)(Client* client, Response* response, IncomingEvent* event);
struct IncomingEvent {
    event_handler_t onEvent;
    union {
        OnMessage onMessage;
        OnNotification onNotification;
        OnUserInfo onUserInfo;
        OnGetChannels onGetChannels;
    } as;
};

extern IncomingEvent incoming_events[MAX_INCOMING_EVENTS];

size_t allocate_incoming_event(void);
