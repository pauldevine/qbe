/* sys/types.h -- C-Kermit header set.  Widths follow Open Watcom's 16-bit
   DOS headers (the port's reference toolchain): off_t is a LONG, so file
   sizes and offsets past 64 KB survive. */
#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H
#include <stddef.h>
typedef int ssize_t;
typedef long off_t;
typedef int pid_t;
typedef int uid_t;
typedef int gid_t;
typedef unsigned short mode_t;
typedef int dev_t;
typedef unsigned int ino_t;
typedef unsigned short nlink_t;
typedef unsigned long time_t;
typedef unsigned long clock_t;
#endif
