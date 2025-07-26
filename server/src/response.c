#include "response.h"
#include <arpa/inet.h>
void response_hton(Response* res) {
    res->packet_id = htonl(res->packet_id);
    res->opcode = htonl(res->opcode);
    res->packet_len = htonl(res->packet_len);
}
