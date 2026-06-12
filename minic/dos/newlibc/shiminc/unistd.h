/* unistd.h - triage shim for the §6a newlibc sweep. */
#ifndef _UNISTD_H
#define _UNISTD_H

#include <sys/types.h>
#include <stddef.h>

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

int read(int fd, void *buf, size_t n);
int write(int fd, const void *buf, size_t n);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);
int unlink(const char *path);
int isatty(int fd);
void *sbrk(int incr);
int rmdir(const char *path);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);

#endif /* _UNISTD_H */
