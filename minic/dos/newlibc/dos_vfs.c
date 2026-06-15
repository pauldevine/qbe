/*
 * dos_vfs.c -- a DOS INT 21h file backend implementing newlibc's vfs_*
 * interface, for the libstub-free DOS-HOSTED link (§7s, Phase-6 libstub
 * retirement — making real file I/O work for stevie).
 *
 * newlibc's real vfs.c is the BARE-METAL VFS: a FAT-on-block/SASI + ramfs +
 * device table, the right thing for the Victor target and for the RAM/FAT
 * newlibc tests (build-newlibc-test.sh keeps vfs.c+fat.c+block.c).  But a real
 * DOS-hosted program (stevie) wants open()/read()/write()/fopen() to hit REAL
 * DOS files, which DOS already provides via INT 21h.  On a DOS host there is
 * no SASI disk to mount — DOS *is* the filesystem.
 *
 * This TU REPLACES vfs.c (and drops fat.c/block.c) on the build-example /
 * build-stevie --no-libstub path: it implements exactly the vfs_* surface that
 * syscalls.c / dirent.c / unlink.c call, mapping each to the matching INT 21h
 * file function.  The fd IS the DOS handle: 0/1/2 are DOS's pre-opened
 * CON/AUX/PRN handles, so console I/O (printf's _write(1), getchar's _read(0))
 * works with no special-casing, and opened files get DOS handles >= 5.  No fd
 * table, no FAT/block/ramfs.  This is what libstub's open/read/write/fopen did
 * directly; here it sits behind newlibc's stdio so the rest of the libstub-free
 * stack (printf_wrappers, the FILE layer in dos_shim, dos_libc) is unchanged.
 *
 * Compiled in the newlibc regime (shiminc + newlibc headers) like the other
 * support TUs.  Returns negative -errno on failure, matching the vfs.c
 * contract syscalls.c expects (it does errno = -ret).
 */

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <dos.h>
#include "vfs.h"		/* vfs_* prototypes + vfs_dirent_t — keep signatures honest */

/* One INT 21h call; returns 0 on success, -1 if the carry flag is set. */
static int dos_int21(union REGS *r)
{
	int86(0x21, r, r);
	return r->x.cflag ? -1 : 0;
}

void vfs_init(void)
{
	/* Nothing to do: DOS already has handles 0/1/2 open (CON), and files
	 * are opened on demand via INT 21h.  (The bare-metal vfs_init registers
	 * the console device + mounts; here DOS owns all of that.) */
}

int vfs_open(const char *path, int flags)
{
	union REGS r;
	int acc = flags & O_ACCMODE;
	int fd;

	/* O_CREAT|O_TRUNC ("w"/"w+"): AH=3Ch creates new or truncates existing,
	 * opened for read/write. */
	if ((flags & O_CREAT) && (flags & O_TRUNC)) {
		r.h.ah = 0x3C;
		r.x.cx = 0;			/* normal file attributes */
		r.x.dx = (unsigned)path;
		if (dos_int21(&r) < 0)
			return -EACCES;
		return (int)r.x.ax;
	}

	/* Otherwise open the existing file (AH=3Dh, AL = 0/1/2 access mode). */
	r.h.ah = 0x3D;
	r.h.al = (unsigned char)acc;
	r.x.dx = (unsigned)path;
	if (dos_int21(&r) < 0) {
		/* Not there.  If O_CREAT was asked (without O_TRUNC), create it. */
		if (flags & O_CREAT) {
			r.h.ah = 0x3C;
			r.x.cx = 0;
			r.x.dx = (unsigned)path;
			if (dos_int21(&r) < 0)
				return -EACCES;
			fd = (int)r.x.ax;
		} else {
			return -ENOENT;
		}
	} else {
		fd = (int)r.x.ax;
	}

	/* O_APPEND ("a"): seek to end so writes append. */
	if (flags & O_APPEND) {
		r.h.ah = 0x42;
		r.h.al = 2;		/* SEEK_END */
		r.x.bx = fd;
		r.x.cx = 0;
		r.x.dx = 0;
		int86(0x21, &r, &r);
	}
	return fd;
}

int vfs_close(int fd)
{
	union REGS r;

	r.h.ah = 0x3E;
	r.x.bx = fd;
	if (dos_int21(&r) < 0)
		return -EBADF;
	return 0;
}

ssize_t vfs_read(int fd, void *buf, size_t count)
{
	union REGS r;

	if (count == 0)
		return 0;
	r.h.ah = 0x3F;
	r.x.bx = fd;
	r.x.cx = (unsigned)count;
	r.x.dx = (unsigned)buf;
	if (dos_int21(&r) < 0)
		return -EIO;
	return (ssize_t)r.x.ax;		/* bytes read; 0 at EOF */
}

ssize_t vfs_write(int fd, const void *buf, size_t count)
{
	union REGS r;

	if (count == 0)
		return 0;
	r.h.ah = 0x40;
	r.x.bx = fd;
	r.x.cx = (unsigned)count;
	r.x.dx = (unsigned)buf;
	if (dos_int21(&r) < 0)
		return -EIO;
	return (ssize_t)r.x.ax;		/* bytes actually written */
}

off_t vfs_lseek(int fd, off_t offset, int whence)
{
	union REGS r;

	r.h.ah = 0x42;
	r.h.al = (unsigned char)whence;		/* 0=SET, 1=CUR, 2=END */
	r.x.bx = fd;
	r.x.cx = (offset < 0) ? 0xFFFF : 0;	/* high word (off_t is 16-bit) */
	r.x.dx = (unsigned)offset;		/* low word */
	if (dos_int21(&r) < 0)
		return -EINVAL;
	return (off_t)r.x.ax;			/* low word of the new position */
}

int vfs_isatty(int fd)
{
	union REGS r;

	/* INT 21h AH=44h AL=00h: get device info; DX bit 7 set => char device. */
	r.x.ax = 0x4400;
	r.x.bx = fd;
	if (dos_int21(&r) < 0)
		return 0;
	return (r.x.dx & 0x0080) ? 1 : 0;
}

/* stat/fstat: DELIBERATELY write NOTHING to *st — only return success/failure.
 *
 * `struct stat` has DIFFERENT sizes in the two compile regimes that meet here:
 * a DOS program (stevie) declares its statbuf with minic/include/sys/stat.h
 * ({int st_mode; int st_size; time_t st_mtime} ~ 6-8 B) and passes &statbuf
 * across the linker to these functions, which see the newlibc shiminc struct
 * (~30+ B, 11 fields).  Zeroing/filling sizeof(*st) (the shiminc size) would
 * overflow the caller's smaller stack buffer and clobber its locals — exactly
 * the bug that broke stevie's save (writeit's fname got corrupted to NULL when
 * stat succeeded on an existing file, while a new file's failed stat returned
 * early and saved fine).  Even a single field write lands at the shiminc
 * offset, not the caller's, so it cannot help the caller anyway.  libstub's
 * _stat/_fstat are likewise -1/no-write stubs.  stevie only uses the result to
 * gate a (no-op) chmod, so returning status without touching *st is correct
 * and overflow-proof.  A real cross-regime stat would need one shared ABI. */

int vfs_fstat(int fd, struct stat *st)
{
	(void)st;
	return (fd >= 0) ? 0 : -EBADF;
}

int vfs_stat(const char *path, struct stat *st)
{
	int fd;

	(void)st;
	fd = vfs_open(path, O_RDONLY);
	if (fd < 0)
		return -ENOENT;
	vfs_close(fd);
	return 0;		/* file exists; *st left untouched (see note above) */
}

int vfs_unlink(const char *path)
{
	union REGS r;

	r.h.ah = 0x41;			/* delete file */
	r.x.dx = (unsigned)path;
	if (dos_int21(&r) < 0)
		return -ENOENT;
	return 0;
}

/* rename: INT 21h AH=56h wants DS:DX = old name, ES:DI = new name.  Both names
 * live in DGROUP (near data), so point ES at DS via segread + int86x.  (stevie
 * calls rename() to back up the original before a write and ignores the
 * result, so a failure just means no backup — non-fatal.) */
int vfs_rename(const char *old_path, const char *new_path)
{
	union REGS r;
	struct SREGS s;

	segread(&s);
	r.h.ah = 0x56;
	r.x.dx = (unsigned)old_path;
	r.x.di = (unsigned)new_path;
	s.es = s.ds;
	int86x(0x21, &r, &r, &s);
	if (r.x.cflag)
		return -EACCES;
	return 0;
}

int vfs_fcntl(int fd, int cmd, int arg)
{
	(void)fd;
	(void)cmd;
	(void)arg;
	return 0;
}

int vfs_ioctl(int fd, unsigned long request, void *arg)
{
	(void)fd;
	(void)request;
	(void)arg;
	return 0;
}

/* Directory iteration is not supported over plain DOS handles here (stevie
 * does not list directories).  Stub to a clean error. */

int vfs_opendir(const char *path)
{
	(void)path;
	return -EINVAL;
}

int vfs_readdir(int dir, vfs_dirent_t *entry)
{
	(void)dir;
	(void)entry;
	return -1;
}

int vfs_closedir(int dir)
{
	(void)dir;
	return 0;
}
