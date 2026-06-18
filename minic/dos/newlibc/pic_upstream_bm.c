/*
 * pic_upstream_bm.c -- bare-metal Victor 9000 8259A PIC test driving the
 * UPSTREAM newlibc drivers/pic.c (the LAST of the §8l/§8m/§8n/§8o/§8p driver
 * sweep -- timer, display, keyboard, sasi, console, and now the interrupt
 * controller itself).
 *
 * Where pic_bm.c (§6f) exercises the hand-mirrored bm_pic.c port, this links
 * and runs newlibc's OWN drivers/pic.c -- the file the §8k gas->nasm in-place
 * port made minic-compilable (its pic_delay forks the nasm two-jump idiom and
 * its interrupt_flags_save forks `#if defined(__MINIC__)` to the Intel
 * `pushf`/`pop word %0`/`cli` form, with the §8j extended-asm operand resolving
 * to a frame slot; its pic_write_command/data SAVE_ES/RESTORE_ES sites collapse
 * to no-ops via the upstream interrupts.h __MINIC__ fork).  §8k proved that file
 * COMPILES; this proves it RUNS on the bare machine, the Phase-6 end-state
 * where newlibc's own drivers replace the bm_*.c mirrors.  NOTHING from
 * bm_pic.c is linked -- the upstream pic_* functions (pic_*, not the mirror's
 * bm_pic_*) are the only PIC driver in the image.
 *
 * The whole test runs UNDER A LIVE TIMER ISR: the local timer_isr is the
 * compiler-emitted ES-safe iret ABI (__attribute__((interrupt)) -> QBE
 * `interrupt` linkage -> the i8086 backend's prologue/epilogue, §6d), routing
 * each IR2 tick through the UPSTREAM timer_tick_handler() (drivers/timer.c,
 * §8l) and acknowledging it with the UPSTREAM pic_send_eoi() -- so the EOI path
 * of pic.c is exercised every tick (a broken EOI yields exactly one tick then
 * silence; continuous ticks across the run are the EOI proof).  Two §8k-
 * translated upstream drivers (pic + timer) running together under live
 * interrupts.  pic_send_eoi loads ES (0xE000) for the memory-mapped command
 * register inside the ISR; that is ES-safe ONLY because the §6d prologue saved
 * ES, the same §8k SAVE_ES-drop story §8o validated for sasi.
 *
 * Coverage of the upstream IRQ-mask API: pic_init (full re-init), pic_get_mask
 * (IMR readback), pic_disable_irq / pic_enable_irq (the read-modify-write mask
 * path through the live interrupt_flags_save/cli/restore), and pic_set_mask
 * (whole-mask write).  IR5 is the unused expansion bit poked harmlessly while
 * the timer is live on IR2; masking IR2 must FREEZE ticks and unmasking must
 * RESUME them, proving the mask register gates delivery, not just stores bits.
 *
 * Output over the raw serial console (bm_puts/bm_puthex); mask values are
 * deterministic (after pic_init + timer_init the IMR is 0xFB -- all masked,
 * only IR2 open).  Every phase prints before it runs (5 MHz 8088 rule).
 */

#include <stdint.h>
#include "bm_console.h"
#include "interrupts.h"   /* merged upstream (§8k __MINIC__ fork): interrupts_enable/disable decls, SAVE_ES no-ops */
#include "pic.h"          /* UPSTREAM: pic_init/enable_irq/disable_irq/send_eoi/get/set_mask */
#include "timer.h"        /* UPSTREAM: timer_init/get_ticks/delay_ms/tick_handler */
#include "v9k_hw.h"       /* INT_TIMER, IRQ_TIMER, PIC_INT_BASE */

#define IR5_BIT 0x20      /* unused expansion IRQ */
#define IR2_BIT 0x04      /* the live timer IRQ */

/* Near offset of the (single) code frame, for the small-model IVT install. */
extern unsigned qbe_get_cs(void);

/*
 * Upstream pic.c's pic_init() calls interrupts_disable() and its
 * interrupt_flags_restore() calls interrupts_enable() -- both defined only in
 * upstream drivers/interrupts.c, which we deliberately do NOT link (it also
 * defines its own timer_isr that would collide with the local one here).
 * Supply the one-liners the upstream interrupts.h declares.
 */
void interrupts_enable(void) {
    __asm__ volatile ("sti");
}
void interrupts_disable(void) {
    __asm__ volatile ("cli");
}

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

/* Timer ISR (IR2, INT 0x42): route the tick to the UPSTREAM tick handler and
 * acknowledge with the UPSTREAM pic_send_eoi.  The compiler-emitted prologue
 * saved ES, set DS/ES=DGROUP and saved every register; pic_send_eoi's far MMIO
 * write to E000:0000 is ES-safe because of that prologue (§8k SAVE_ES drop). */
void __far __attribute__((interrupt)) timer_isr(void) {
    timer_tick_handler();
    pic_send_eoi(IRQ_TIMER);
}

/* Model-agnostic IVT install, mirroring bm_interrupts.c's bm_install_isr:
 * far-code models carry seg:off in the pointer; near-code models a bare offset
 * whose segment is qbe_get_cs().  Call with interrupts disabled. */
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

/* Bounded spin that doesn't touch the timer: returns nonzero if ticks moved
 * off `start` within `budget` outer iterations (~6 ms each on the 5 MHz 8088;
 * the tick period is ~8 ms).  Must NOT call timer_delay_ms here -- that waits
 * on ticks and would hang forever when delivery is frozen. */
static int wait_tick_change(unsigned long start, unsigned int budget) {
    unsigned int i, j;
    volatile unsigned int sink;

    for (i = 0; i < budget; i++) {
        for (j = 0; j < 2000; j++)
            sink = j;
        if (timer_get_ticks() != start)
            return 1;
    }
    return 0;
}

int main(void) {
    unsigned char saved, mask;
    unsigned long t0;

    fails = 0;
    bm_puts("__V9BEGIN__\n");
    bm_puts("Victor 9000 bare-metal UPSTREAM pic.c test (qbe/minic)\n");

    bm_puts("phase 1: upstream pic_init() + install timer ISR (INT 0x42)\n");
    pic_init();                       /* full 8259A re-init, all IRQs masked */
    install_isr(INT_TIMER, timer_isr);

    bm_puts("phase 2: upstream timer_init() (8253 ch2 + unmask IR2)\n");
    timer_init();

    bm_puts("phase 3: sti (whole test runs under the live timer ISR)\n");
    __asm__ volatile ("sti");

    bm_puts("phase 4: IMR after init is 0xfb (only IR2 open): ");
    saved = pic_get_mask();
    if (saved == 0xFB) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO (");
        bm_puthex(saved);
        bm_puts(")\n");
        fails++;
    }

    check("phase 5: timer ticks advance (EOI through pic_send_eoi): ",
          wait_tick_change(timer_get_ticks(), 2000));

    bm_puts("phase 6: pic_disable_irq(5) sets the IR5 bit: ");
    pic_disable_irq(5);
    mask = pic_get_mask();
    if (mask == (unsigned char)(saved | IR5_BIT)) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO (");
        bm_puthex(mask);
        bm_puts(")\n");
        fails++;
    }

    check("phase 7: timer undisturbed by IR5 masking: ",
          wait_tick_change(timer_get_ticks(), 2000));

    bm_puts("phase 8: pic_enable_irq(5) clears the IR5 bit: ");
    pic_enable_irq(5);
    mask = pic_get_mask();
    if (mask == (unsigned char)(saved & (unsigned char)~IR5_BIT)) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO (");
        bm_puthex(mask);
        bm_puts(")\n");
        fails++;
    }

    pic_set_mask(saved);
    check("phase 9: pic_set_mask whole-mask restore reads back: ",
          pic_get_mask() == saved);

    /* The mask must GATE delivery, not just store bits: masking the live timer
     * IRQ freezes ticks (interrupts still globally enabled), unmasking resumes
     * them. */
    bm_puts("phase 10: pic_disable_irq(2) freezes ticks: ");
    pic_disable_irq(2);
    t0 = timer_get_ticks();
    if (!wait_tick_change(t0, 150)) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    pic_enable_irq(2);
    check("phase 11: pic_enable_irq(2) resumes ticks: ",
          wait_tick_change(timer_get_ticks(), 2000));

    /* ~250 ms more of live interrupts; continuous ticks across the run are the
     * implicit EOI check (broken pic_send_eoi = one tick, then hang). */
    check("phase 12: EOI verified by continuous ticks: ",
          wait_tick_change(timer_get_ticks(), 2000));

    if (fails == 0)
        bm_puts("PASS: upstream pic.c bare-metal checks completed.\n");
    else
        bm_puts("FAIL: upstream pic.c bare-metal checks failed.\n");
    bm_puts("__V9END__\n");
    return 0;
}
