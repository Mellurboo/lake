#include "tmprintf.h"
#include <stdio.h>

const char* vtmprintf(const char* fmt, va_list args) {
    static char tmp[4096];
    vsnprintf(tmp, sizeof(tmp), fmt, args);
    return tmp;
}
const char* tmprintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const char* e = vtmprintf(fmt, args);
    va_end(args);
    return e;
}
