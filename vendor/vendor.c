#ifdef _WIN32
# include "wepoll/wepoll.c"
#endif

#include <stdio.h>
#define WINNET_IMPLEMENTATION
#include "winnet.h"
#define GT_IMPLEMENTATION
#include "gt.h"
