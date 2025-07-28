#include "getUserInfoPacket.h"
#include <snet.h>

void getUserInfoPacket_hton(GetUserInfoPacket* packet){
    packet->userID = htonl(packet->userID);
}
