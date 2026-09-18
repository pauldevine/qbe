/* direct.h -- C-Kermit header set (DOS directory calls). */
#ifndef _DIRECT_H
#define _DIRECT_H
#include <stddef.h>
char *getcwd(char *buf, size_t size);
int chdir(const char *path);
int mkdir(const char *path);
int rmdir(const char *path);
int _getdrive(void);
int _chdrive(int drive);
#endif
