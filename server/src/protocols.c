#include "protocols.h"
#include "protocols/protocol.h"
#include "protocols/core.h"
#include "protocols/auth.h"
#include "protocols/echo.h"
#include "protocols/msg.h"
#include "protocols/notify.h"

Protocol* protocols[] = {
    &coreProtocol,
    &authProtocol,
    // CORE and auth need to be first in this order otherwise auth logic wont work

    &echoProtocol,
    &msgProtocol,
    &notifyProtocol,
};
size_t protocols_count = ARRAY_LEN(protocols);

