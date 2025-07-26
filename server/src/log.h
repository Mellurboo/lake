#pragma once
#include <stdio.h>
#ifdef _WIN32
# define NEWLINE "\n\r"
#else
# define NEWLINE "\n"
#endif
#define log(level, ...) (fprintf(stderr, # level ": " __VA_ARGS__), fprintf(stderr, NEWLINE))
#define trace(...) log(TRACE, __VA_ARGS__)
#define info(...) log(INFO, __VA_ARGS__)
#define warn(...) log(WARN, __VA_ARGS__)
#define error(...) log(ERROR, __VA_ARGS__)
#define fatal(...) log(FATAL, __VA_ARGS__)
