/* time.h - triage shim for the §6a newlibc sweep. */
#ifndef _TIME_H
#define _TIME_H

#include <sys/types.h>

typedef long clock_t;

time_t time(time_t *t);
clock_t clock(void);

#endif /* _TIME_H */
