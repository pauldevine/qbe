/* sys/time.h - triage shim for the §6a newlibc sweep. */
#ifndef _SYS_TIME_H
#define _SYS_TIME_H

#include <sys/types.h>

struct timeval {
    time_t tv_sec;
    long tv_usec;
};

int gettimeofday(struct timeval *tv, void *tz);

#endif /* _SYS_TIME_H */
