/* time.h -- C-Kermit header set. */
#ifndef _TIME_H
#define _TIME_H
#include <sys/types.h>
#define CLOCKS_PER_SEC 1000
struct tm {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;
	int tm_yday;
	int tm_isdst;
};
char      *asctime(const struct tm *tp);
clock_t    clock(void);
char      *ctime(const time_t *t);
struct tm *gmtime(const time_t *t);
struct tm *localtime(const time_t *t);
time_t     mktime(struct tm *tp);
size_t     strftime(char *s, size_t max, const char *fmt, const struct tm *tp);
time_t     time(time_t *t);
void       tzset(void);
extern char *tzname[2];
extern long timezone;
extern int daylight;
#endif
