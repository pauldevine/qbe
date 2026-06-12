/*
 * stdio_bm.c -- bare-metal Victor 9000 newlibc-stdio test (§6h,
 * Phase-6 step 4e).
 *
 * The first program whose printf()/fgets() run on the bare machine
 * through the REAL newlibc stack: printf_wrappers -> syscalls ->
 * VFS /dev/console -> bm_shim -> bm_tty (display + serial mirror,
 * cooked interrupt-driven keyboard).  libstub's DOS INT 21h stdio is
 * linked --no-stdio and unreachable -- this is the seam that retires it.
 *
 * The harness types "vx\b9k\nz" (same input as tty_bm), but here the
 * line comes back through fgets(stdin): keyboard ISR -> bm_tty cooked
 * read -> console_dev_read -> vfs_read(0) -> _read -> read -> fgets.
 *
 * Every phase prints before it runs (5 MHz 8088 rule).
 */

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/times.h>
#include "bm_console.h"
#include "bm_timer.h"
#include "bm_interrupts.h"
#include "bm_tty.h"
#include "bm_stdio.h"

#define LINE_MAX 16

/* Print a line with '\n'/'\b' made visible, for a stable golden --
 * through putchar(), i.e. through the stack under test. */
static void put_visible(const char *s) {
    putchar('"');
    while (*s) {
        if (*s == '\n') {
            putchar('\\');
            putchar('n');
        } else if (*s == '\b') {
            putchar('\\');
            putchar('b');
        } else {
            putchar(*s);
        }
        s++;
    }
    putchar('"');
}

int main(void) {
    char line[LINE_MAX];
    char rb[8];
    FILE *fp;
    struct tms tm;
    long t0, t1;
    int n, c, i, fails;

    fails = 0;
    bm_puts("__V9BEGIN__\n");
    bm_puts("Victor 9000 bare-metal newlibc-stdio test (qbe/minic)\n");

    bm_puts("phase 1: PIC re-init + timer ISR (INT 0x42)\n");
    bm_interrupts_init();

    bm_puts("phase 2: 8253 ch2 100 Hz + IR2 unmask\n");
    bm_timer_init();

    bm_puts("phase 3: tty init (display + keyboard, IR6)\n");
    bm_tty_init();

    bm_puts("phase 4: sti\n");
    bm_interrupts_enable();

    bm_puts("phase 5: vfs init (fds 0/1/2 -> /dev/console)\n");
    bm_stdio_init();

    /* From here on, output goes through the newlibc stack: it lands on
     * the SCREEN and in this serial capture. */
    n = printf("phase 6: printf formats: %d %u 0x%04x %s %c\n",
               -123, 456U, 0xBEEF, "v9k", '!');
    printf("phase 6: printf returned %d\n", n);

    n = write(1, "phase 7: write(1) direct\n", 25);
    printf("phase 7: write returned %d\n", n);

    printf("phase 8: fputc/fputs to stdout+stderr: ");
    if (fputc('.', stdout) == '.' && fputc('.', stderr) == '.' &&
        fputs(" ok\n", stderr) != EOF) {
        /* the dots and " ok" printed above are part of the check */
    } else {
        printf("NO\n");
        fails++;
    }

    printf("phase 9: fprintf to /dev/null: ");
    fp = fopen("/dev/null", "w");
    if (fp != 0 && fprintf(fp, "[%s:%04u]", "ok", 7U) == 9 &&
        fclose(fp) == 0) {
        printf("yes\n");
    } else {
        printf("NO\n");
        fails++;
    }

    printf("phase 10: read /ram/readme.txt via fopen+fread: ");
    fp = fopen("/ram/readme.txt", "r");
    if (fp != 0 && fread(rb, 1, 6, fp) == 6 &&
        rb[0] == 'V' && rb[1] == 'i' && rb[2] == 'c' &&
        rb[3] == 't' && rb[4] == 'o' && rb[5] == 'r' &&
        fclose(fp) == 0) {
        printf("yes\n");
    } else {
        printf("NO\n");
        fails++;
    }

    printf("phase 11: isatty(0)=%d isatty(1)=%d\n", isatty(0), isatty(1));

    /* The prompt and the echo land on the screen AND in this serial
     * capture; the harness types "vx\b9k\nz" a few seconds in. */
    printf("phase 12: prompt + fgets(stdin)\n");
    printf("v9k> ");
    for (i = 0; i < LINE_MAX; i++)
        line[i] = 0;
    if (fgets(line, LINE_MAX, stdin) != line) {
        printf("phase 12: fgets returned NULL\n");
        fails++;
    }

    printf("phase 13: got ");
    put_visible(line);
    printf(" -- ");
    if (line[0] == 'v' && line[1] == '9' && line[2] == 'k' &&
        line[3] == '\n' && line[4] == 0) {
        printf("yes\n");
    } else {
        printf("NO\n");
        fails++;
    }

    /* One more typed char is still queued behind the line. */
    printf("phase 14: getchar: ");
    c = getchar();
    printf("\n");
    if (c == 'z') {
        printf("got 'z'\n");
    } else {
        printf("got WRONG char\n");
        fails++;
    }

    printf("phase 15: clock via times(): ");
    t0 = (long)times(&tm);
    bm_timer_delay_ms(200);
    t1 = (long)times(&tm);
    if (t1 > t0) {
        printf("advancing\n");
    } else {
        printf("STUCK\n");
        fails++;
    }

    if (fails == 0)
        printf("PASS: bare-metal newlibc-stdio checks completed.\n");
    else
        printf("FAIL: bare-metal newlibc-stdio checks failed.\n");
    bm_puts("__V9END__\n");
    return 0;
}
