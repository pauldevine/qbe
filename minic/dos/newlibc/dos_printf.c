/*******************************************************************************
 * dos_printf.c - libstub-compatible printf family for the --no-libstub path
 *******************************************************************************
 * This is a FORK of newlibc's libgloss/printf_wrappers.c (COPY/ADD-NEVER-MUTATE:
 * newlibc itself stays pristine, exactly as dos_vfs.c shadows newlibc's vfs.c).
 * It is linked INSTEAD of printf_wrappers.c only on the build-example.sh /
 * build-stevie.sh --no-libstub path (ordinary DOS programs).  The bare-metal
 * (bm_stdio) path and build-newlibc-test.sh keep newlibc's own printf_wrappers.c
 * so the newlibc regression corpus and the bare-metal goldens are untouched.
 *
 * DELTA from printf_wrappers.c (everything else is verbatim — resync this file
 * if newlibc's printf_wrappers.c changes upstream):
 *
 *   %p : libstub formats a pointer as a raw, NO-"0x"-prefix, lowercase hex
 *        field zero-padded to the full pointer width (2 * sizeof(void *) hex
 *        digits — 8 in the far-data models, 4 in the near models), e.g.
 *        (void *)0x12345678 -> "12345678", (void *)0x42 -> "00000042", and a
 *        small-model near pointer 0x5678 -> "5678".  newlibc's printf_wrappers.c
 *        instead prints "0x" + width-4 hex.  The four codegen probes
 *        (cstrprobe / compactprobe_extra / huge_norm_probe / mediumprobe) gate
 *        the §4i/§4s/§7g normalisation arithmetic against libstub-captured
 *        goldens, so they need libstub's exact %p shape.
 *
 *   %o : libstub implements octal (base-8, no prefix); newlibc's
 *        printf_wrappers.c does not, and echoes the literal "%o".  mediumprobe's
 *        oct=%o (want 777) needs it.
 ******************************************************************************/

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>

#undef fgets
#undef getchar
#undef putchar
#undef puts
#undef fputc
#undef fputs

#define PRINT_BUFFER (-1)
#define PRINT_ERROR  (-2)

extern ssize_t _write(int fd, const void *buf, size_t count);

typedef struct {
    char *buffer;
    size_t limit;
    size_t count;
    int fd;
} print_ctx_t;

static int __attribute__((noinline)) fd_putc(int c, int fd) {
    unsigned char ch = (unsigned char)c;

    if (_write(fd, &ch, 1) != 1) {
        return EOF;
    }

    return ch;
}

static int stream_fd(FILE *fp) {
    if (fp == stdout) {
        return STDOUT_FILENO;
    }

    if (fp == stderr) {
        return STDERR_FILENO;
    }

    return fp->_file;
}

static void out_char(print_ctx_t *ctx, char ch) {
    if (ctx->fd >= 0) {
        if (fd_putc((unsigned char)ch, ctx->fd) == EOF) {
            ctx->fd = PRINT_ERROR;
        }
    } else if (ctx->limit > 0 && ctx->count + 1 < ctx->limit) {
        ctx->buffer[ctx->count] = ch;
    }

    ctx->count++;
}

static void out_repeat(print_ctx_t *ctx, char ch, int count) {
    while (count-- > 0) {
        out_char(ctx, ch);
    }
}

/*
 * Emit a string field.  prec < 0 means no precision; otherwise at most prec
 * characters of the string are written (NUL still terminates early).
 */
static void out_string(print_ctx_t *ctx, const char *str, int width, int prec,
                       int left) {
    size_t len = 0;
    size_t i;
    const char *p;

    if (str == NULL) {
        str = "(null)";
    }

    for (p = str; *p; p++) {
        len++;
    }

    if (prec >= 0 && (size_t)prec < len) {
        len = (size_t)prec;
    }

    if (!left && width > (int)len) {
        out_repeat(ctx, ' ', width - (int)len);
    }

    for (i = 0; i < len; i++) {
        out_char(ctx, str[i]);
    }

    if (left && width > (int)len) {
        out_repeat(ctx, ' ', width - (int)len);
    }
}

/*
 * Emit an integer with an optional leading sign character.  Precision (prec >=
 * 0) gives the minimum number of digits, zero-filled, and per the C standard
 * disables the '0' flag.  Space field padding precedes the sign; zero field
 * padding (the '0' flag) follows it.
 */
static void out_number(print_ctx_t *ctx, unsigned long value, unsigned base,
                       int uppercase, char sign, int width, int prec, int zero,
                       int left) {
    char digits[16];
    const char *table = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int len = 0;
    int zeros = 0;
    int total;
    int use_zero = zero && prec < 0;

    if (value == 0) {
        if (prec != 0) {
            digits[len++] = '0';
        }
    } else {
        while (value != 0) {
            digits[len++] = table[value % base];
            value /= base;
        }
    }

    if (prec > len) {
        zeros = prec - len;
    }

    total = len + zeros + (sign ? 1 : 0);

    if (!left && !use_zero && width > total) {
        out_repeat(ctx, ' ', width - total);
    }

    if (sign) {
        out_char(ctx, sign);
    }

    if (!left && use_zero && width > total) {
        out_repeat(ctx, '0', width - total);
    }

    out_repeat(ctx, '0', zeros);

    while (len-- > 0) {
        out_char(ctx, digits[len]);
    }

    if (left && width > total) {
        out_repeat(ctx, ' ', width - total);
    }
}

static void out_unsigned(print_ctx_t *ctx, unsigned long value, unsigned base,
                         int uppercase, int width, int prec, int zero, int left) {
    out_number(ctx, value, base, uppercase, 0, width, prec, zero, left);
}

static void out_signed(print_ctx_t *ctx, long value, int width, int prec,
                       int zero, int left) {
    char sign = 0;
    unsigned long magnitude;

    if (value < 0) {
        sign = '-';
        magnitude = (unsigned long)(-value);
    } else {
        magnitude = (unsigned long)value;
    }

    out_number(ctx, magnitude, 10, 0, sign, width, prec, zero, left);
}

static int tiny_vformat(print_ctx_t *ctx, const char *fmt, va_list ap) {
    while (*fmt) {
        int left = 0;
        int zero = 0;
        int width = 0;
        int prec = -1;
        int long_arg = 0;
        char spec;

        if (*fmt != '%') {
            out_char(ctx, *fmt++);
            continue;
        }

        fmt++;
        if (*fmt == '%') {
            out_char(ctx, *fmt++);
            continue;
        }

        while (*fmt == '-' || *fmt == '0') {
            if (*fmt == '-') left = 1;
            if (*fmt == '0') zero = 1;
            fmt++;
        }

        while (*fmt >= '0' && *fmt <= '9') {
            width = (width * 10) + (*fmt - '0');
            fmt++;
        }

        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                prec = (prec * 10) + (*fmt - '0');
                fmt++;
            }
        }

        if (*fmt == 'l') {
            long_arg = 1;
            fmt++;
        } else if (*fmt == 'h') {
            fmt++;
        }

        spec = *fmt++;
        switch (spec) {
            case 'c':
                out_char(ctx, (char)va_arg(ap, int));
                break;
            case 's':
                out_string(ctx, va_arg(ap, const char *), width, prec, left);
                break;
            case 'd':
            case 'i':
                if (long_arg) {
                    out_signed(ctx, va_arg(ap, long), width, prec, zero, left);
                } else {
                    out_signed(ctx, va_arg(ap, int), width, prec, zero, left);
                }
                break;
            case 'u':
                if (long_arg) {
                    out_unsigned(ctx, va_arg(ap, unsigned long), 10, 0, width, prec, zero, left);
                } else {
                    out_unsigned(ctx, va_arg(ap, unsigned int), 10, 0, width, prec, zero, left);
                }
                break;
            case 'o':
                /* DELTA: libstub implements octal; newlibc echoes "%o". */
                if (long_arg) {
                    out_unsigned(ctx, va_arg(ap, unsigned long), 8, 0, width, prec, zero, left);
                } else {
                    out_unsigned(ctx, va_arg(ap, unsigned int), 8, 0, width, prec, zero, left);
                }
                break;
            case 'x':
            case 'X':
                if (long_arg) {
                    out_unsigned(ctx, va_arg(ap, unsigned long), 16, spec == 'X', width, prec, zero, left);
                } else {
                    out_unsigned(ctx, va_arg(ap, unsigned int), 16, spec == 'X', width, prec, zero, left);
                }
                break;
            case 'p':
                /*
                 * DELTA: match libstub — raw pointer value, lowercase hex, NO
                 * "0x" prefix, zero-padded to the full pointer width.  libstub
                 * prints the argument's raw bytes: a 4-byte far pointer as its
                 * (seg << 16) | off image (so 1734:0007 -> "17340007"), an
                 * 8-hex field; a 2-byte near pointer as its offset, a 4-hex
                 * field.  minic's (uintptr_t)(void *) cast keeps only the
                 * offset (drops the segment) under a far-data model, so read
                 * the 4-byte far pointer arg straight as an unsigned long to
                 * recover seg:off; for a near pointer the offset cast is right.
                 */
                if (sizeof(void *) == 2) {
                    out_unsigned(ctx, (uintptr_t)va_arg(ap, void *), 16, 0,
                                 4, -1, 1, 0);
                } else {
                    out_unsigned(ctx, va_arg(ap, unsigned long), 16, 0,
                                 8, -1, 1, 0);
                }
                break;
            case '\0':
                fmt--;
                break;
            default:
                out_char(ctx, '%');
                out_char(ctx, spec);
                break;
        }
    }

    return (int)ctx->count;
}

int vprintf(const char *__restrict fmt, va_list ap) {
    print_ctx_t ctx;

    ctx.limit = 0;
    ctx.count = 0;
    ctx.fd = STDOUT_FILENO;

    return tiny_vformat(&ctx, fmt, ap);
}

int printf(const char *__restrict fmt, ...) {
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vprintf(fmt, ap);
    va_end(ap);

    return ret;
}

int vfprintf(FILE *__restrict fp, const char *__restrict fmt, va_list ap) {
    print_ctx_t ctx;
    int ret;

    ctx.limit = 0;
    ctx.count = 0;
    ctx.fd = stream_fd(fp);

    ret = tiny_vformat(&ctx, fmt, ap);
    if (ctx.fd == PRINT_ERROR) {
        return EOF;
    }

    return ret;
}

int fprintf(FILE *__restrict fp, const char *__restrict fmt, ...) {
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vfprintf(fp, fmt, ap);
    va_end(ap);

    return ret;
}

int vsnprintf(char *__restrict str, size_t size, const char *__restrict fmt, va_list ap) {
    print_ctx_t ctx;
    int ret;

    ctx.buffer = str;
    ctx.limit = size;
    ctx.count = 0;
    ctx.fd = PRINT_BUFFER;

    ret = tiny_vformat(&ctx, fmt, ap);

    if (size > 0) {
        if (ctx.count < size) {
            str[ctx.count] = '\0';
        } else {
            str[size - 1] = '\0';
        }
    }

    return ret;
}

int snprintf(char *__restrict str, size_t size, const char *__restrict fmt, ...) {
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vsnprintf(str, size, fmt, ap);
    va_end(ap);

    return ret;
}

int vsprintf(char *__restrict str, const char *__restrict fmt, va_list ap) {
    return vsnprintf(str, SIZE_MAX, fmt, ap);
}

int sprintf(char *__restrict str, const char *__restrict fmt, ...) {
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vsprintf(str, fmt, ap);
    va_end(ap);

    return ret;
}

int getchar(void) {
    unsigned char ch;

    if (read(STDIN_FILENO, &ch, 1) == 1) {
        return ch;
    }

    return EOF;
}

char *fgets(char *__restrict str, int size, FILE *__restrict fp) {
    ssize_t n;
    int fd;
    int i;

    if (str == NULL || fp == NULL || size <= 0) {
        return NULL;
    }

    /*
     * Keep stdin on the console read path so line editing and echo remain
     * centralized in /dev/console.  Non-stdin streams use stdio character
     * input, which is enough for read-only RAMFS text files.
     */
    if (size == 1) {
        str[0] = '\0';
        return str;
    }

    fd = stream_fd(fp);
    if (fd == STDIN_FILENO) {
        n = read(STDIN_FILENO, str, (size_t)(size - 1));
        if (n <= 0) {
            return NULL;
        }

        str[n] = '\0';
        return str;
    }

    for (i = 0; i < size - 1; i++) {
        int c;

        c = fgetc(fp);
        if (c == EOF) {
            break;
        }

        str[i] = (char)c;
        if (c == '\n') {
            i++;
            break;
        }
    }

    if (i == 0) {
        return NULL;
    }

    str[i] = '\0';
    return str;
}

int putchar(int c) {
    return fd_putc(c, STDOUT_FILENO);
}

int fputc(int c, FILE *fp) {
    return fd_putc(c, stream_fd(fp));
}

int puts(const char *str) {
    if (fputs(str, stdout) == EOF || fputc('\n', stdout) == EOF) {
        return EOF;
    }

    return 0;
}

int fputs(const char *str, FILE *fp) {
    int fd = stream_fd(fp);

    if (str != NULL) {
        while (*str) {
            if (fd_putc((unsigned char)*str++, fd) == EOF) {
                return EOF;
            }
        }
    }
    return 0;
}
