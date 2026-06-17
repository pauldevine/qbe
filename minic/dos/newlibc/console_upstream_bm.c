/*
 * console_upstream_bm.c -- bare-metal Victor 9000 serial-console test driving
 * the UPSTREAM newlibc drivers/console.c (§8p, the §8l/§8m/§8n driver-sweep
 * pattern applied to a fifth driver -- the 7201 channel-A polled serial
 * console).
 *
 * Where the §6c bm_console.c port (always linked, all bm_*-prefixed) is the
 * harness's own polled TX path, this links and runs newlibc's OWN
 * drivers/console.c -- the file the §8k gas->nasm in-place port made
 * minic-compilable (its intel_dev_write_byte + every serial_write_control /
 * status / RX site fork `#if defined(__MINIC__)` to HW_WRITE_BYTE/HW_READ_BYTE
 * and no-op SAVE_ES/RESTORE_ES via the §6y shadow interrupts.h).  §8k proved
 * that file COMPILES; this proves it RUNS on the bare machine.
 *
 * The console driver is pure polled MMIO (no interrupts), so -- like §8m's
 * display test -- there is no ISR plumbing.  The captured harness serial is
 * 7201 channel A (run-victor-baremetal.sh -rs232a null_modem -bitbanger), the
 * SAME channel upstream console_putc/console_puts drive, so the driver under
 * test produces the captured output DIRECTLY: a console_puts/console_putc line
 * that appears in the golden IS the proof its TX path ran (a broken TX path
 * drops the line -> a loud golden diff).  The framing/result lines use bm_puts
 * (bm_console.c, the proven-good harness path) so a TX-path break still prints
 * readable diagnostics.  The RX side (console_rx_ready / console_getc_nonblock)
 * is exercised in its deterministic NO-input idle form -- channel A has a
 * null_modem (not the §7i loopback), so no byte ever arrives.  console.c's
 * SAVE_ES-during-far-MMIO story under a live ISR is already validated by §8o
 * (sasi.c), so this test stays polled and deterministic.  NOTHING from the
 * console_* aliases in bm_shim.c is linked (this is not a bm_stdio program).
 *
 * Every phase prints before it runs (5 MHz 8088 rule).
 */

#include <stdint.h>
#include "v9k_hw.h"        /* far MMIO macros (pulled in by console.h's deps) */
#include "bm_console.h"    /* harness serial: bm_puts (channel A, captured) */
#include "console.h"       /* UPSTREAM: console_* raw serial console API */

/* Link-time gap (the §8n "supply it in the test, not the driver" pattern):
 * console.c is small enough to be ONE TU code segment, so --gc-sections keeps
 * its unused cooked-console paths live -- console_dev_read reads the keyboard
 * and console_echo_input echoes to the display, calling keyboard_getc /
 * display_putc.  This test exercises the RAW serial console API only (the
 * cooked /dev/console path is covered by the §6n/§6o/§6t cooked-console
 * tests), so those two functions never execute; these stubs satisfy the
 * linker for the dead code.  This test deliberately does NOT include the
 * display or keyboard headers by name -- the §8m/§8n build rules grep $SRC for
 * those quoted include lines and would otherwise link display.c/keyboard.c. */
void display_putc(char c) { (void)c; }
int keyboard_getc(void) { return -1; }

static int fails;

static void check(const char *label, int ok) {
    bm_puts(label);
    if (ok) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }
}

int main(void) {
    unsigned long spin;

    fails = 0;

    /* Exercise the upstream init FIRST, before the captured region opens.
     * crt0 already ran the §6c bm_console_init with the identical validated
     * sequence, so re-resetting the now-live 7201 channel A emits one
     * transient byte on the wire (the channel-reset glitch).  Calling
     * console_init() before __V9BEGIN__ lets the harness trim that transient
     * -- it keeps only the __V9BEGIN__..__V9END__ region -- exactly as crt0's
     * own bm_console_init reset precedes the boot banner.  This still fully
     * runs console.c's init: its intel_dev_write_byte (Intel HW_WRITE_BYTE
     * fork) + enable_internal_clock/set_baud_rate/serial_write_control with
     * their no-op'd SAVE_ES/RESTORE_ES sites.  That every captured line below
     * arrives intact is the proof the re-init left channel A working. */
    console_init();

    bm_puts("__V9BEGIN__\n");
    bm_puts("Victor 9000 bare-metal UPSTREAM console.c test (qbe/minic)\n");

    bm_puts("phase 1: console_init ran; channel A alive in captured region\n");

    /* console_puts output IS captured on channel A -- the line below is the
     * proof its TX path ran (a broken TX path drops it -> golden diff). */
    bm_puts("phase 2: console_puts -> ");
    console_puts("UPSTREAM_console_puts_OK\n");

    /* console_putc, char by char; the trailing '\n' exercises the driver's
     * CR/LF conversion (the harness strips CR, so the golden shows "wxyz"). */
    bm_puts("phase 3: console_putc -> ");
    console_putc('w');
    console_putc('x');
    console_putc('y');
    console_putc('z');
    console_putc('\n');

    /* console_tx_ready reads RR0's TX-buffer-empty bit.  Immediately after
     * console_putc the buffer still holds the just-written byte, so poll
     * (bounded) until it drains and assert tx_ready then signals ready. */
    for (spin = 0; spin < 200000UL && !console_tx_ready(); spin++)
        ;
    check("phase 4: console_tx_ready signals ready after drain: ",
          console_tx_ready() != 0);

    /* Channel A has a null_modem and no loopback -> no byte ever arrives. */
    check("phase 5: console_rx_ready with no input is 0: ",
          console_rx_ready() == 0);

    check("phase 6: console_getc_nonblock with no input is -1: ",
          console_getc_nonblock() == -1);

    if (fails == 0)
        bm_puts("PASS: upstream console.c bare-metal checks completed.\n");
    else
        bm_puts("FAIL: upstream console.c bare-metal checks failed.\n");
    bm_puts("__V9END__\n");
    return 0;
}
