/* sys/stat.h - Minimal stub for MiniC / DOS target. */

#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/types.h>

struct stat {
    int st_mode;
    int st_size;
    time_t st_mtime;
};

int stat(char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);
int chmod(char *path, int mode);

#endif /* _SYS_STAT_H */
