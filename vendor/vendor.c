#ifdef _WIN32
# include "wepoll/wepoll.c"
#endif

#include <stdio.h>
#define SNET_IMPLEMENTATION
#include "snet.h"
#define GT_IMPLEMENTATION
#include "gt.h"
