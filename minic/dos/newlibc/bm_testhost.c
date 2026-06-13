/*
 * bm_testhost.c -- bare-metal host for an UNMODIFIED newlibc phase-3
 * test TU (§6j, Phase-6).
 *
 * The DOS-hosted gate runs upstream tests by renaming their main()
 * (-Dmain=newlibc_test_main) and letting dos_shim's main run vfs_init()
 * first.  This TU is the bare-metal seat of that arrangement:
 * build-newlibc-baremetal.sh compiles a source that resolves into
 * newlibc's tests/ directory with the same rename and links this main()
 * in front of it -- full driver bring-up in the mandated order (§6d
 * interrupt window: PIC re-init BEFORE sti), VFS init, then the test,
 * then a result line so the golden proves the test RETURNED.
 *
 * Every phase prints before it runs (5 MHz 8088 rule); the preamble
 * goes through the polled serial console (bm_puts), the result line
 * through the newlibc stack the test itself just exercised.
 */

#include <stdio.h>
#include "bm_console.h"
#include "bm_interrupts.h"
#include "bm_timer.h"
#include "bm_tty.h"
#include "bm_stdio.h"

extern int newlibc_test_main(void);

int main(void) {
    int ret;

    bm_puts("__V9BEGIN__\n");
    bm_puts("bm_testhost: pic+timer\n");
    bm_interrupts_init();
    bm_timer_init();
    bm_puts("bm_testhost: tty+sti\n");
    bm_tty_init();
    bm_interrupts_enable();
    bm_puts("bm_testhost: vfs\n");
    bm_stdio_init();

    ret = newlibc_test_main();

    printf("bm_testhost: test returned %d\n", ret);
    bm_puts("__V9END__\n");
    return ret;
}
