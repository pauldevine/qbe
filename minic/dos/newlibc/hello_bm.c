/*
 * hello_bm.c -- bare-metal hello for the Victor 9000 (§6c, Phase-6 step 3).
 *
 * The first program from this toolchain to run on the bare machine: linked
 * by omf_link.py --raw-binary at 0x3000, loaded by the MAME Lua autoboot
 * loader (tools/run-victor-baremetal.sh), printing over serial port A via
 * the polled bm_console driver.  Output is bracketed by the standard
 * __V9BEGIN__/__V9END__ sentinels and carries a PASS:/FAIL: verdict line
 * (newlibc run_test.sh convention) so harnesses can gate on it.
 *
 * The checks are small but deliberately cross libstub helpers: 16-bit mul
 * through volatile globals (no folding), 32-bit unsigned divide
 * (_qbe_udivmod32 via bm_putu's %10 loop and the divisor computation),
 * and string traversal.
 */

#include "bm_console.h"

static volatile int six = 6;
static volatile int seven = 7;
static volatile unsigned long clock_hz = 1250000UL;

static int bm_strlen(const char *s) {
    int n;
    n = 0;
    while (s[n])
        n++;
    return n;
}

int main(void) {
    int product;
    unsigned long divisor;
    int fails;

    fails = 0;
    bm_puts("__V9BEGIN__\n");
    bm_puts("Victor 9000 bare-metal hello (qbe/minic, raw binary @ 0x3000)\n");

    product = six * seven;
    bm_puts("  6 * 7 = ");
    bm_putu((unsigned long)product);
    bm_putc('\n');
    if (product != 42)
        fails++;

    divisor = clock_hz / (9600UL * 16);
    bm_puts("  baud divisor 1250000/(9600*16) = ");
    bm_putu(divisor);
    bm_putc('\n');
    if (divisor != 8)
        fails++;

    bm_puts("  strlen(\"Victor 9000\") = ");
    bm_putu((unsigned long)bm_strlen("Victor 9000"));
    bm_putc('\n');
    if (bm_strlen("Victor 9000") != 11)
        fails++;

    bm_puts("  hex check: ");
    bm_puthex(0xBEEF);
    bm_putc('\n');

    if (fails == 0)
        bm_puts("PASS: bare-metal hello checks completed.\n");
    else
        bm_puts("FAIL: bare-metal hello checks failed.\n");
    bm_puts("__V9END__\n");
    return 0;
}
