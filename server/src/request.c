#include "request.h"
#include <arpa/inet.h>
void request_ntoh(Request* req) {
    req->protocol_id = ntohl(req->protocol_id);
    req->func_id = ntohl(req->func_id);
    req->packet_id = ntohl(req->packet_id);
    req->packet_len = ntohl(req->packet_len);
}
