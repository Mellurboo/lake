#ifdef _WIN32
# include "wepoll/wepoll.c"
#endif

#define SNET_IMPLEMENTATION
#include "snet.h"
#define GT_IMPLEMENTATION
#include "gt.h"

#define STUI_IMPLEMENTATION
#include "stui.h"

#define POST_QUANTUM_CRYPT_IMPLEMENTATION
#include "post_quantum_cryptography.h"

#include "fileutils.c"