#include "time_unix.h"

#ifdef _WIN32
#else
# include <time.h>
#endif

uint64_t time_unix_milis(void) {
#ifdef _WIN32
    // TODO: bruvsky pls implement this for binbows opewating system for video james
    return 0;
#else
    struct timespec now;
    // timespec_get(&now, TIME_UTC);
    clock_gettime(CLOCK_REALTIME, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
#endif
}
