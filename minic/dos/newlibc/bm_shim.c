/*
 * bm_shim.c -- bare-metal bottom layer for the newlibc stdio stack (§6h,
 * Phase-6 step 4e).
 *
 * The bare-metal counterpart of dos_shim.c: the same newlibc layering
 *
 *     printf family (libgloss/printf_wrappers.c)
 *       -> _write/_read/... (libgloss/syscalls.c)
 *         -> vfs_write/vfs_read/... (vfs/vfs.c, fds 0/1/2 = /dev/console)
 *           -> device ops: console_dev_*, tty_dev_*     <-- THIS FILE
 *
 * but the console device ops route to bm_tty (display + serial mirror,
 * cooked interrupt-driven keyboard) instead of DOS INT 21h -- this is
 * the read(0)/write(1) seam that retires libstub's DOS-only stdio on
 * the bare machine.  The timer surface routes to bm_timer (100 Hz 8253)
 * and the display surface to bm_display.
 *
 * Programs init the driver stack explicitly (interrupt-window ordering
 * is theirs to own -- see bm_stdio.h), then call bm_stdio_init() to
 * point fds 0/1/2 at /dev/console.
 *
 * Build: compiled by tools/build-newlibc-baremetal.sh when the program
 * includes bm_stdio.h, alongside the newlibc portable-subset TUs, small
 * model, linked against --no-stdio libstub.
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

#include "bm_tty.h"
#include "bm_timer.h"
#include "bm_display.h"
#include "bm_keyboard.h"
#include "bm_pic.h"
#include "bm_console.h"

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
 * _sbrk()'s heap: nothing in the bare-metal set calls newlib malloc
 * (malloc/free are libstub's, self-contained), so these only need to
 * EXIST -- documentedly NOT a usable heap. */
char __heap_start[2];
char __heap_end[2];

struct _reent shim_reent;
struct _reent *_impure_ptr = &shim_reent;

/* ---- console/tty device ops (vfs.c device table externs) ----
 * bm_tty: output mirrors to the display AND the serial console; input
 * is the cooked interrupt-driven keyboard (rubout, CR->LF, echo). */

ssize_t console_dev_write(const void *buf, size_t count)
{
	return (ssize_t)bm_tty_write((const char *)buf, (unsigned int)count);
}

ssize_t console_dev_read(void *buf, size_t count)
{
	return (ssize_t)bm_tty_read((char *)buf, (unsigned int)count);
}

/* /dev/tty is the upstream "serial debug console" (drivers/console.c:
 * tty_dev_write=console_putc, tty_dev_read=console_getc on 7201 channel A),
 * DISTINCT from /dev/console's cooked keyboard.  Our default bare-metal port
 * folds the two onto the cooked bm_tty (the bare machine's only interactive
 * console); serial_loopback_test is the first test to need them separated, so
 * under -DBM_SERIAL_LOOPBACK route /dev/tty to the raw channel-A serial path
 * (looped TXD->RXD) exactly as upstream does. */
#ifdef BM_SERIAL_LOOPBACK
ssize_t tty_dev_write(const void *buf, size_t count)
{
	const char *cbuf = (const char *)buf;
	size_t i;

	if (!buf)
		return 0;
	for (i = 0; i < count; i++)
		bm_console_putc(cbuf[i]);
	return (ssize_t)count;
}

ssize_t tty_dev_read(void *buf, size_t count)
{
	char *cbuf = (char *)buf;
	size_t i;

	if (!buf || count == 0)
		return 0;
	for (i = 0; i < count; i++) {
		int c = bm_console_getc();
		if (c < 0)
			break;
		cbuf[i] = (char)c;
		if (c == '\n') {
			i++;
			break;
		}
	}
	return (ssize_t)i;
}

/* The unprefixed newlibc raw-serial console API serial_loopback_test calls
 * directly (channel A, the loopback data path). */
void console_putc(char c)         { bm_console_putc(c); }
int  console_getc(void)           { return bm_console_getc(); }
int  console_getc_nonblock(void)  { return bm_console_getc_nonblock(); }
int  console_rx_ready(void)       { return bm_console_rx_ready(); }
#else
ssize_t tty_dev_write(const void *buf, size_t count)
{
	return console_dev_write(buf, count);
}

ssize_t tty_dev_read(void *buf, size_t count)
{
	return console_dev_read(buf, count);
}
#endif

/* ---- timer (drivers/timer.h surface) ----
 * bm_timer: 8253 channel 2 at 100 Hz through the compiler-emitted ISR. */

uint32_t timer_get_ticks(void)
{
	return (uint32_t)bm_timer_get_ticks();
}

uint32_t timer_get_frequency(void)
{
	return 100UL;
}

void timer_delay_ms(uint32_t ms)
{
	bm_timer_delay_ms(ms);
}

/* ---- display (drivers/display.h surface) ---- */

void display_init(void)
{
	bm_display_init();
}

void display_puts(const char *str)
{
	bm_display_puts(str);
}

void display_putc(char c)
{
	bm_display_putc(c);
}

void display_clear(void)
{
	bm_display_clear();
}

void display_set_cursor(uint8_t row, uint8_t col)
{
	bm_display_set_cursor(row, col);
}

/* ---- keyboard (drivers/keyboard.h surface) ----
 * bm_keyboard: interrupt-driven on IR6, the §6e driver (the same ring
 * bm_tty's cooked reader drains -- a program that calls the raw API
 * directly bypasses the cooking).  Aliased here so an UNMODIFIED upstream
 * keyboard test links its unprefixed names, exactly like the timer/display
 * surfaces above. */

int keyboard_get_raw_event_nonblock(void)
{
	return bm_keyboard_get_raw_event_nonblock();
}

int keyboard_hit(void)
{
	return bm_keyboard_hit();
}

int keyboard_getc_nonblock(void)
{
	return bm_keyboard_getc_nonblock();
}

int keyboard_getc(void)
{
	return bm_keyboard_getc();
}

/* ---- PIC (drivers/pic.h surface) ----
 * bm_pic: the §6d 8259A driver (memory-mapped E000:0000/0001, single PIC,
 * the Victor wiring -- keyboard=IR6, timer=IR2, serial=IR1).  bm_pic.c is
 * already linked into every bm_stdio build (the testhost preamble calls
 * bm_pic_init); these aliases let an UNMODIFIED upstream pic test link its
 * unprefixed names, exactly like the timer/display/keyboard surfaces above.
 * pic_enable_irq CLEARS the mask bit (unmask) and pic_disable_irq SETS it
 * (mask), matching upstream drivers/pic.c. */

uint8_t pic_get_mask(void)
{
	return bm_pic_get_mask();
}

void pic_set_mask(uint8_t mask)
{
	bm_pic_set_mask(mask);
}

void pic_enable_irq(uint8_t irq)
{
	bm_pic_unmask(irq);
}

void pic_disable_irq(uint8_t irq)
{
	bm_pic_mask(irq);
}

/* ---- POSIX unprefixed aliases (newlib libc normally provides these) ---- */

int open(const char *path, int flags, ...)
{
	/* mode (the third arg) is ignored: the VFS mounts read-only. */
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

/* ---- minimal FILE layer over VFS fds (same shape as dos_shim.c) ----
 * FILE._file is the fd, the same one-word shape as libstub's
 * _stdin/_stdout/_stderr sentinels, which stay linked in for the std
 * streams. */

#define SHIM_NFILES 8

static FILE shim_files[SHIM_NFILES];
static char shim_file_used[SHIM_NFILES];

FILE *fopen(const char *path, const char *mode)
{
	int i;
	int fd;

	/* "r" -> O_RDONLY(0), anything else -> O_WRONLY(1); whether a
	 * write target exists is the VFS's call (e.g. /dev/null says yes,
	 * the read-only ramfs/FAT say no). */
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

/* ---- the seam's init: point fds 0/1/2 at /dev/console ---- */

void bm_stdio_init(void)
{
	vfs_init();
}
