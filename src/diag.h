#ifndef _H_DIAG
#define _H_DIAG

// -DRSC_DIAG only (RSC_DIAG_CFLAGS=-DRSC_DIAG to make): DIAG() writes a trace line to error.log;
// a release build compiles it away
#ifdef RSC_DIAG
#include <stdio.h>
#define DIAG(...)                                                              \
    do {                                                                       \
        fprintf(stderr, "[diag] " __VA_ARGS__);                                \
        fputc('\n', stderr);                                                   \
    } while (0)
#else
#define DIAG(...)                                                              \
    do {                                                                       \
    } while (0)
#endif

#endif
