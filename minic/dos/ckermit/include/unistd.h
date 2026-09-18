/* unistd.h -- C-Kermit header set. */
#ifndef _UNISTD_H
#define _UNISTD_H
#include <sys/types.h>
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
int     access(const char *path, int mode);
int     close(int fd);
int     dup(int fd);
int     dup2(int fd, int fd2);
int     isatty(int fd);
off_t   lseek(int fd, off_t off, int whence);
int     read(int fd, void *buf, unsigned n);
int     write(int fd, const void *buf, unsigned n);
int     unlink(const char *path);
int     chdir(const char *path);
char   *getcwd(char *buf, size_t size);
int     rmdir(const char *path);
pid_t   getpid(void);
unsigned sleep(unsigned secs);
void    _exit(int status);
int     execl(const char *path, const char *arg0, ...);
int     execlp(const char *file, const char *arg0, ...);
int     execv(const char *path, char *const argv[]);
int     execvp(const char *file, char *const argv[]);
#endif
