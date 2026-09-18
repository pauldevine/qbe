# C-Kermit header set

The system headers `tools/build-ckermit.sh` compiles C-Kermit (the Victor 9000
port in `~/projects/ckermit`) against, in place of Open Watcom's.  They are
self-contained: nothing here includes `minic/include` or
`minic/dos/newlibc/shiminc`.

Search order (from `tools/ckermit/cppflags.sh`), the same as `victorow.mak`
with this directory in Watcom's place:
`victorow/` → `victor/` → this directory → the C-Kermit tree.

## Compatibility rule

C-Kermit picks code paths from what the system headers define (`#ifdef
F_SETFL`, `DONDELAY`, `O_NDELAY`, `EWOULDBLOCK`, ...).  These headers must
select the same code as Watcom's.  `tools/ckermit/hdrcheck.sh` checks that:
every macro C-Kermit tests is defined here only if Watcom defines it, and each
module compiles the same statements both ways.  Run it after changing any
header here.

So some ordinary POSIX names are deliberately missing: `fcntl()`, `F_*`,
`O_NDELAY`, `O_NONBLOCK`, `O_NOCTTY`, `EWOULDBLOCK` (Watcom's DOS headers have
none of them).  Watcom defines a few macros we do not (`feof`, `putc` and
friends as macros, `LLONG_MAX`, `_MAX_FNAME`, ...); hdrcheck lists why each is
harmless.

Values follow Watcom where C-Kermit depends on them: signal numbers, mode
bits, `PATH_MAX` 143, `CLOCKS_PER_SEC` 1000, `NULL` `((void *)0)`, `off_t`
`long`.  They follow newlib (as shiminc does) where a value crosses into the
newlibc support code: `errno` values and `__errno()`, the `O_ACCMODE`,
`O_APPEND`, `O_CREAT`, `O_TRUNC`, `O_EXCL` bits, and `FILE`'s layout.

## ABI notes for the libc fill

Whatever provides a function declared here for C-Kermit has to agree with
these declarations.  The existing libstub-free runtime is compiled against
shiminc, and several declarations differ:

| name | here | existing runtime |
|---|---|---|
| `struct stat` | Watcom-shaped, `off_t st_size` (long) | `dos_vfs.c` `vfs_stat` writes nothing (the §7s cross-regime fix) |
| `off_t`, `lseek` | `long` | shiminc `off_t` is `int`; `vfs_lseek` returns the low word |
| `signal` | returns `void (*)(int)` | `dos_libc.c` `int signal(int, int)` stub |
| `struct dirent` / `DIR` | Watcom layout, `DIR` = `struct dirent` | newlibc `libgloss/dirent.c`, a different struct |
| `O_TEXT`, `O_BINARY` | 0x10, 0x20 (free bits in shiminc's scheme) | ignored by `vfs_open` |
| `_fmode` | `extern int` in stdlib.h | none |
| time (`struct tm`, `localtime`, ...) | declared | none |
| `union REGS` | adds Watcom's `.w` view to `.x`/`.h` | same word layout as `dos_syscall*.asm` expects |

`FILE` (`struct __sFILE { int _file; }`), `jmp_buf` (`int[8]`, the
`setjmp_rt.asm` layout) and `errno` match the runtime.
