#pragma once
#include <stdarg.h>
#ifdef __GNUC__
# define PRINTFLIKE(n,m) __attribute__((format(printf,n,m)))
#else
# define PRINTFLIKE(...)
#endif
const char* vtmprintf(const char* fmt, va_list args);
const char* tmprintf(const char* fmt, ...) PRINTFLIKE(1, 2);
