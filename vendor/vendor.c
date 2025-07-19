#ifdef _WIN32
# include "wepoll/wepoll.c"
# include <stdio.h>
#endif

#define SNET_IMPLEMENTATION
#include "snet.h"
#define GT_IMPLEMENTATION
#include "gt.h"
