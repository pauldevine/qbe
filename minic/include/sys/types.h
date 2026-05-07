/* sys/types.h - Minimal stub for MiniC / DOS target.
 * Stevie's fileio.c includes this for stat()/chmod() — DOS doesn't
 * really have these, so the corresponding code paths in fileio.c are
 * usually #ifdef-guarded for UNIX.
 */

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

#endif /* _SYS_TYPES_H */
