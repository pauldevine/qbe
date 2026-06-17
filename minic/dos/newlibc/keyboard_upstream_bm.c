/*
 * keyboard_upstream_bm.c -- bare-metal Victor 9000 keyboard test driving the
 * UPSTREAM newlibc drivers/keyboard.c (§8n, the §8l/§8m pattern applied to a
 * third driver).
 *
 * Where keyboard_bm.c (§6e) exercises the hand-mirrored bm_keyboard.c port,
 * this links and runs newlibc's OWN drivers/keyboard.c -- the file the §8k
 * gas->nasm in-place port made minic-compilable (its keyboard_flags_save now
 * forks `#if defined(__MINIC__)` to the Intel `pushf`/`pop word %0`/`cli`
 * form, and its SAVE_ES/RESTORE_ES collapse to no-ops via the §6y shadow
 * interrupts.h).  §8k proved that file COMPILES; this proves it RUNS on the
 * bare machine -- the Phase-6 end-state where newlibc's own drivers replace
 * the bm_*.c mirrors.
 *
 * Like §8l's timer (and unlike §8m's polled display), this is INTERRUPT-
 * DRIVEN: the keyboard's dedicated KBINT line is IR6.  The interrupt plumbing
 * is the generic toolchain feature, not a driver -- the local keyboard_isr is
 * the compiler-emitted ES-safe iret ABI (__attribute__((interrupt)) -> QBE
 * `interrupt` linkage -> the i8086 backend's prologue/epilogue, §6d), and it
 * routes each KBINT to the UPSTREAM keyboard_irq_handler() so the upstream
 * event ring is what keyboard_getc_nonblock() drains.  Upstream timer.c (§8l)
 * supplies the deterministic timeout clock (a second compiler-emitted ISR on
 * IR2), so this is TWO §8k-translated upstream drivers running together under
 * live interrupts.  NOTHING from bm_keyboard.c / bm_timer.c is linked.
 *
 * The harness (V9K_KEYPOST=v9k) types "v9k" through MAME's natural keyboard a
 * few seconds in; every keystroke travels VIA CS2 shift register -> KBINT
 * (IR6) -> compiler-emitted keyboard_isr -> upstream keyboard_irq_handler ->
 * event ring -> cooked ASCII.  Output is deterministic: fixed text plus the
 * received characters.  Every phase prints before it runs (5 MHz 8088 rule).
 */

#include <stdint.h>
#include "bm_console.h"
#include "bm_pic.h"
#include "interrupts.h"   /* §6y shadow: interrupts_enable decl, SAVE_ES no-ops */
#include "timer.h"        /* UPSTREAM: timer_init/get_ticks/tick_handler (timeout clock) */
#include "keyboard.h"     /* UPSTREAM: keyboard_init/getc_nonblock/irq_handler */
#include "v9k_hw.h"        /* INT_*, IRQ_*, INTEL_DEV_SEGMENT, PIC_COMMAND_PORT, HW_WRITE_BYTE */

#define EXPECT_LEN 3

/* Near offset of the (single) code frame, for the small-model IVT install. */
extern unsigned qbe_get_cs(void);

/*
 * Upstream keyboard.c's flags-restore calls interrupts_enable() (defined in
 * upstream drivers/interrupts.c, which we deliberately do NOT link -- it also
 * defines its own timer_isr/keyboard_isr that would collide with the local
 * ISRs here).  Supply the one-liner the §6y shadow interrupts.h declares.
 */
void interrupts_enable(void) {
    __asm__ volatile ("sti");
}

/* Upstream keyboard.c has no ISR-entry accessor (the hand-mirrored
 * bm_keyboard.c added bm_keyboard_isr_count for the §6e test); count entries
 * in the local ISR wrapper instead, the timer_upstream_bm pattern. */
static volatile unsigned kbd_isr_entries;

/* Timer ISR (IR2, INT 0x42): route the tick to the UPSTREAM handler, EOI. */
void __far __attribute__((interrupt)) timer_isr(void) {
    timer_tick_handler();
    HW_WRITE_BYTE(INTEL_DEV_SEGMENT, PIC_COMMAND_PORT,
                  (uint8_t)(0x60 | IRQ_TIMER));
}

/* Keyboard ISR (IR6, INT 0x46): service the upstream state machine, EOI.
 * One state-machine step per KBINT; the compiler-emitted prologue saved ES
 * to CS-local static memory and set DS/ES=DGROUP. */
void __far __attribute__((interrupt)) keyboard_isr(void) {
    kbd_isr_entries++;
    keyboard_irq_handler();
    HW_WRITE_BYTE(INTEL_DEV_SEGMENT, PIC_COMMAND_PORT,
                  (uint8_t)(0x60 | IRQ_KEYBOARD));
}

/* Model-agnostic IVT install, mirroring bm_interrupts.c's bm_install_isr:
 * far-code models carry seg:off in the pointer; near-code models a bare
 * offset whose segment is qbe_get_cs().  Call with interrupts disabled. */
static void install_isr(unsigned char int_num, void (*fn)(void)) {
    volatile uint16_t __far *ivt;
    uint32_t lin;
    uint16_t seg, off;

    lin = (uint32_t)fn;
    off = (uint16_t)lin;
    seg = (uint16_t)(lin >> 16);
    if (seg == 0)
        seg = (uint16_t)qbe_get_cs();

    ivt = (volatile uint16_t __far *)
        ((((uint32_t)0) << 16) | ((uint16_t)(int_num * 4)));
    ivt[0] = off;
    ivt[1] = seg;
}

int main(void) {
    char got[EXPECT_LEN + 1];
    int ngot, c, fails;
    unsigned long t0;

    fails = 0;
    bm_puts("__V9BEGIN__\n");
    bm_puts("Victor 9000 bare-metal UPSTREAM keyboard.c test (qbe/minic)\n");

    bm_puts("phase 1: PIC re-init + install timer + keyboard ISRs\n");
    bm_pic_init();
    install_isr(INT_TIMER, timer_isr);
    install_isr(INT_KEYBOARD, keyboard_isr);

    bm_puts("phase 2: upstream timer_init() (timeout clock, IR2)\n");
    timer_init();

    bm_puts("phase 3: upstream keyboard_init() (VIA CS2 + IR6 unmask)\n");
    keyboard_init();

    bm_puts("phase 4: sti\n");
    __asm__ volatile ("sti");

    bm_puts("phase 5: no key pending at start: ");
    c = keyboard_getc_nonblock();
    if (c < 0) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    /* The harness types after ~3 s; allow up to ~1900 ticks of waiting. */
    bm_puts("phase 6: waiting for 3 typed chars: ");
    ngot = 0;
    t0 = timer_get_ticks();
    while (ngot < EXPECT_LEN &&
           (timer_get_ticks() - t0) < 1900) {
        c = keyboard_getc_nonblock();
        if (c >= 0) {
            got[ngot] = (char)c;
            ngot++;
        }
    }
    got[ngot] = 0;
    if (ngot == EXPECT_LEN) {
        bm_puts("got \"");
        bm_puts(got);
        bm_puts("\"\n");
    } else {
        bm_puts("TIMEOUT after ");
        bm_putu((unsigned long)ngot);
        bm_puts(" char(s) \"");
        bm_puts(got);
        bm_puts("\"\n");
        fails++;
    }

    bm_puts("phase 7: chars match \"v9k\": ");
    if (ngot == 3 && got[0] == 'v' && got[1] == '9' && got[2] == 'k') {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    bm_puts("phase 8: keyboard ISR entered: ");
    if (kbd_isr_entries > 0) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    bm_puts("phase 9: timer still ticking alongside IR6: ");
    t0 = timer_get_ticks();
    timer_delay_ms(200);
    if (timer_get_ticks() != t0) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    if (fails == 0)
        bm_puts("PASS: upstream keyboard.c bare-metal checks completed.\n");
    else
        bm_puts("FAIL: upstream keyboard.c bare-metal checks failed.\n");
    bm_puts("__V9END__\n");
    return 0;
}
