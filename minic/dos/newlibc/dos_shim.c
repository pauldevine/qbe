/*
 * dos_shim.c -- DOS-hosted bottom layer for the newlibc portable subset (§6b).
 *
 * Phase-6 step 2 runs newlibc's libgloss/VFS/FAT/printf tests DOS-hosted in
 * DOSBox, long before the bare-metal Victor drivers can be ported.  newlibc
 * layers cleanly:
 *
 *     printf family (libgloss/printf_wrappers.c)
 *       -> _write/_read/... (libgloss/syscalls.c)
 *         -> vfs_write/vfs_read/... (vfs/vfs.c)
 *           -> device ops: console_dev_*, tty_dev_*     <-- THIS FILE
 *           -> FAT over block devices (RAM-backed in the tests)
 *
 * so the only hardware-shaped holes are the console/tty device ops, the
 * timer, and the display calls a couple of tests make directly.  This TU
 * fills them with DOS INT 21h equivalents, plus:
 *
 *   - the POSIX unprefixed aliases newlib's libc normally provides
 *     (read -> _read, open -> _open, ...);
 *   - a minimal FILE table + fopen/fclose/fread/fgetc over the VFS fds
 *     (newlib proper, which would provide buffered stdio, is out of
 *     scope; libstub's DOS-handle stdio is suppressed via
 *     libstub_to_exe.py --no-stdio so the newlibc printf family wins);
 *   - _impure_ptr / __heap_start / __heap_end link satisfaction;
 *   - main(): calls vfs_init() (board_init() does this on hardware) and
 *     tail-calls the test's entry, renamed newlibc_test_main by the
 *     build script's -Dmain=.
 *
 * Build: compiled by tools/build-newlibc-test.sh with the shiminc/
 * newlib-shaped headers, small model, linked against --no-stdio libstub.
 */

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <reent.h>
#include <stdio.h>
#include <time.h>
#include <dos.h>

/* ---- newlibc syscall layer (libgloss/syscalls.c) ---- */
extern int _open(const char *path, int flags, int mode);
extern int _close(int fd);
extern ssize_t _read(int fd, void *buf, size_t count);
extern ssize_t _write(int fd, const void *buf, size_t count);
extern off_t _lseek(int fd, off_t offset, int whence);
extern int _stat(const char *path, struct stat *st);
extern int _fstat(int fd, struct stat *st);
extern int _isatty(int fd);
extern int _unlink(const char *path);
extern int _gettimeofday(struct timeval *tv, void *tz);
extern clock_t _times(struct tms *buf);
extern int _kill(int pid, int sig);
extern int _link(const char *o, const char *n);
extern void vfs_init(void);

/* ---- link satisfaction for syscalls.c ----
 * _sbrk()'s heap: nothing in the DOS-hosted test set calls newlib
 * malloc (the tests' malloc/free are libstub's, self-contained), so
 * these only need to EXIST.  They are separate arrays, so the
 * start/end ordering is whatever the linker chose -- documentedly NOT
 * a usable heap. */
char __heap_start[2];
char __heap_end[2];

struct _reent shim_reent;
struct _reent *_impure_ptr = &shim_reent;

/* ---- console/tty device ops (vfs.c device table externs) ----
 * DOS handle I/O: INT 21h AH=3Fh (read) / AH=40h (write) on the std
 * handles.  Under the small model DS already covers all data, so the
 * buffer offset in DX is enough. */

static int dos_handle_rw(int ah, int handle, const void *buf, unsigned count)
{
	union REGS r;

	if (count == 0)
		return 0;
	r.h.ah = ah;
	r.x.bx = handle;
	r.x.cx = count;
	r.x.dx = (unsigned)buf;
	int86(0x21, &r, &r);
	if (r.x.cflag)
		return -1;
	return (int)r.x.ax;
}

ssize_t console_dev_write(const void *buf, size_t count)
{
	return dos_handle_rw(0x40, 1, buf, (unsigned)count);
}

ssize_t console_dev_read(void *buf, size_t count)
{
	return dos_handle_rw(0x3F, 0, buf, (unsigned)count);
}

ssize_t tty_dev_write(const void *buf, size_t count)
{
	return console_dev_write(buf, count);
}

ssize_t tty_dev_read(void *buf, size_t count)
{
	return console_dev_read(buf, count);
}

/* ---- timer (drivers/timer.h surface) ----
 * DOS AH=2Ch wall-clock, scaled to centisecond "ticks" (freq 100Hz). */

uint32_t timer_get_ticks(void)
{
	union REGS r;
	uint32_t secs;

	r.h.ah = 0x2C;
	int86(0x21, &r, &r);
	secs = (uint32_t)r.h.ch * 3600UL + (uint32_t)r.h.cl * 60UL
	     + (uint32_t)r.h.dh;
	return secs * 100UL + (uint32_t)r.h.dl;
}

uint32_t timer_get_frequency(void)
{
	return 100UL;
}

void timer_delay_ms(uint32_t ms)
{
	uint32_t start;
	uint32_t want;

	start = timer_get_ticks();
	want = ms / 10UL;
	while (timer_get_ticks() - start < want)
		;
}

/* ---- display (drivers/display.h surface; bss/memory/hello tests) ---- */

static unsigned shim_strlen(const char *s)
{
	unsigned n;

	n = 0;
	while (s[n])
		n++;
	return n;
}

void display_puts(const char *str)
{
	console_dev_write(str, shim_strlen(str));
}

void display_clear(void)
{
}

void display_set_cursor(uint8_t row, uint8_t col)
{
}

/* ---- POSIX unprefixed aliases (newlib libc normally provides these) ---- */

int open(const char *path, int flags, ...)
{
	/* mode (the third arg) is ignored: the VFS is read-only. */
	return _open(path, flags, 0);
}

int close(int fd)
{
	return _close(fd);
}

int read(int fd, void *buf, size_t n)
{
	return (int)_read(fd, buf, n);
}

int write(int fd, const void *buf, size_t n)
{
	return (int)_write(fd, buf, n);
}

off_t lseek(int fd, off_t offset, int whence)
{
	return _lseek(fd, offset, whence);
}

int stat(const char *path, struct stat *st)
{
	return _stat(path, st);
}

int fstat(int fd, struct stat *st)
{
	return _fstat(fd, st);
}

int isatty(int fd)
{
	return _isatty(fd);
}

int unlink(const char *path)
{
	return _unlink(path);
}

int gettimeofday(struct timeval *tv, void *tz)
{
	return _gettimeofday(tv, tz);
}

clock_t times(struct tms *buf)
{
	return _times(buf);
}

int kill(int pid, int sig)
{
	return _kill(pid, sig);
}

int link(const char *o, const char *n)
{
	return _link(o, n);
}

/* ---- minimal FILE layer over VFS fds ----
 * Read-only (the FAT driver is read-only); FILE._file is the fd, the
 * same one-word shape as libstub's _stdin/_stdout/_stderr sentinels,
 * which stay linked in for the std streams. */

#define SHIM_NFILES 8

static FILE shim_files[SHIM_NFILES];
static char shim_file_used[SHIM_NFILES];

FILE *fopen(const char *path, const char *mode)
{
	int i;
	int fd;

	/* "r" -> O_RDONLY(0), anything else -> O_WRONLY(1); whether a
	 * write target exists is the VFS's call (e.g. /dev/null says yes,
	 * the read-only FAT says no). */
	fd = _open(path, mode[0] == 'r' ? 0 : 1, 0);
	if (fd < 0)
		return 0;
	for (i = 0; i < SHIM_NFILES; i++) {
		if (!shim_file_used[i]) {
			shim_file_used[i] = 1;
			shim_files[i]._file = fd;
			return &shim_files[i];
		}
	}
	_close(fd);
	return 0;
}

int fclose(FILE *fp)
{
	int i;

	for (i = 0; i < SHIM_NFILES; i++) {
		if (fp == &shim_files[i] && shim_file_used[i]) {
			shim_file_used[i] = 0;
			return _close(fp->_file);
		}
	}
	return -1;
}

int fread(void *buf, size_t size, size_t nmemb, FILE *fp)
{
	/* int return to match the shiminc stdio.h prototype the tests
	 * compiled against. */
	long got;

	got = (long)_read(fp->_file, buf, size * nmemb);
	if (got <= 0)
		return 0;
	return (int)((size_t)got / size);
}

int fgetc(FILE *fp)
{
	unsigned char c;

	if (_read(fp->_file, &c, 1) != 1)
		return -1;
	return (int)c;
}

/* ---- entry: init the VFS (hardware's board_init does this), then run
 * the test, whose own main() was renamed by -Dmain=newlibc_test_main. */

extern int newlibc_test_main();

int main()
{
	vfs_init();
	return newlibc_test_main();
}
