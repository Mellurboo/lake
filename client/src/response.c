#include "response.h"
#include <snet.h>

void response_ntoh(Response* res) {
    res->packet_id = ntohl(res->packet_id);
    res->opcode = ntohl(res->opcode);
    res->packet_len = ntohl(res->packet_len);
}
