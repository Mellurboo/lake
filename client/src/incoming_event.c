#include "incoming_event.h"
#include <gt.h>

size_t allocate_incoming_event(void) {
    for(;;) {
        for(size_t i = 0; i < MAX_INCOMING_EVENTS; ++i) {
            if(!incoming_events[i].onEvent) return i;
        }
        // TODO: we should introduce gtsemaphore
        // that way we can notify on event completion
        // and wait for it in the green threads
        gtyield();
    }
    // unreachable
    // return ~0;
}
