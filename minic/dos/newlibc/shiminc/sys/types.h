/* sys/types.h - triage shim for the §6a newlibc sweep.
 * Shadows minic/include/sys/types.h (shim dir is searched first) and
 * adds the extra newlib-ish types the VFS layer uses. */
#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#include <stddef.h>

typedef int pid_t;
typedef int off_t;
typedef int mode_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned long time_t;
typedef long ssize_t;
typedef short dev_t;
typedef unsigned short ino_t;
typedef unsigned short nlink_t;

#endif /* _SYS_TYPES_H */
