/* sys/stat.h -- C-Kermit header set.  Mode bits are Open Watcom's.
   struct stat is this header set's own layout (NOT shiminc's, NOT
   minic/include's): whatever implements stat()/fstat() for C-Kermit must be
   compiled against this header. */
#ifndef _SYS_STAT_H
#define _SYS_STAT_H
#include <sys/types.h>
#define S_IFMT   0xF000
#define S_IFIFO  0x1000
#define S_IFCHR  0x2000
#define S_IFDIR  0x4000
#define S_IFNAM  0x5000
#define S_IFBLK  0
#define S_IFREG  0x8000
#define S_IFLNK  0
#define S_IFSOCK 0
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#define S_ISUID 004000
#define S_ISGID 002000
#define S_ISVTX 001000
#define S_IRWXU 000700
#define S_IRUSR 000400
#define S_IWUSR 000200
#define S_IXUSR 000100
#define S_IREAD  S_IRUSR
#define S_IWRITE S_IWUSR
#define S_IEXEC  S_IXUSR
#define S_IRWXG 000070
#define S_IRGRP 000040
#define S_IWGRP 000020
#define S_IXGRP 000010
#define S_IRWXO 000007
#define S_IROTH 000004
#define S_IWOTH 000002
#define S_IXOTH 000001
struct stat {
	dev_t   st_dev;
	ino_t   st_ino;
	mode_t  st_mode;
	nlink_t st_nlink;
	uid_t   st_uid;
	gid_t   st_gid;
	dev_t   st_rdev;
	off_t   st_size;
	time_t  st_atime;
	time_t  st_mtime;
	time_t  st_ctime;
	unsigned int st_attr;           /* DOS attribute byte */
};
int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);
int lstat(const char *path, struct stat *buf);
int chmod(const char *path, mode_t mode);
mode_t umask(mode_t mask);
int mkdir(const char *path);    /* DOS: no mode argument (ckvictor.h wraps it) */
#endif
