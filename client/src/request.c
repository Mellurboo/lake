#include "request.h"
#include <snet.h>

void request_hton(Request* req) {
    req->protocol_id = htonl(req->protocol_id);
    req->func_id = htonl(req->func_id);
    req->packet_id = htonl(req->packet_id);
    req->packet_len = htonl(req->packet_len);
}
