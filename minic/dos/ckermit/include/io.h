/* io.h -- C-Kermit header set (Watcom/DOS-style). */
#ifndef _IO_H
#define _IO_H
#include <unistd.h>
#include <fcntl.h>
int setmode(int fd, int mode);
long filelength(int fd);
#endif
