#pragma once
#include "client.h"
#include "response.h"
#include "user_map.h"
#include "msg.h"

#define MAX_INCOMING_EVENTS 128
typedef struct Channels Channels;
typedef struct Servers Servers;
typedef struct GTMutex GTMutex;
typedef struct IncomingEvent IncomingEvent;
typedef struct HandleMapBucket HandleMapBucket;
typedef struct { Messages* msgs; uint32_t server_id; uint32_t channel_id; Message msg; } OnMessage;
typedef struct { Messages* msgs; uint32_t* active_server_id; uint32_t* active_channel_id;} OnNotification;
typedef struct { UserMapBucket* user; } OnUserInfo;
typedef struct { Channels* channels; } OnGetChannels;
typedef struct { Servers* servers; } OnGetServers;
typedef struct { Messages* msgs; } OnGetMessagesBefore;
typedef struct { HandleMapBucket* bucket; GTMutex* mutex; } OnUserHandle;
typedef void (*event_handler_t)(Client* client, Response* response, IncomingEvent* event);
struct IncomingEvent {
    event_handler_t onEvent;
    union {
        OnMessage onMessage;
        OnNotification onNotification;
        OnUserInfo onUserInfo;
        OnGetChannels onGetChannels;
        OnGetServers onGetServers;
        OnGetMessagesBefore onGetMessagesBefore;
        OnUserHandle onUserHandle;
    } as;
};

extern IncomingEvent incoming_events[MAX_INCOMING_EVENTS];

size_t allocate_incoming_event(void);
