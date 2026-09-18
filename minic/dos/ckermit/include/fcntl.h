/* fcntl.h -- C-Kermit header set.  O_ACCMODE/APPEND/CREAT/TRUNC/EXCL use the
   values of minic/dos/newlibc/shiminc/fcntl.h, so an open() that forwards to
   dos_vfs.c sees the flags it expects; O_TEXT/O_BINARY sit on unused bits. */
#ifndef _FCNTL_H
#define _FCNTL_H
#include <sys/types.h>
#define O_RDONLY   0
#define O_WRONLY   1
#define O_RDWR     2
#define O_ACCMODE  3
#define O_APPEND   0x0008
#define O_TEXT     0x0010
#define O_BINARY   0x0020
#define O_CREAT    0x0200
#define O_TRUNC    0x0400
#define O_EXCL     0x0800
/* Deliberately absent, as in Open Watcom's DOS headers: O_NDELAY,
   O_NONBLOCK, O_NOCTTY, the F_* commands and fcntl().  C-Kermit keys whole
   code paths on them (#ifdef F_SETFL in ckutio.c, DONDELAY), and those paths
   are not part of the reference build. */
int open(const char *path, int oflag, ...);
int creat(const char *path, mode_t mode);
#endif
