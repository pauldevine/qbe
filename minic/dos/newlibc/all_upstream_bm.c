/*
 * all_upstream_bm.c -- the Phase-6 driver-sweep CAPSTONE (§8s): a single
 * bare-metal Victor 9000 program that links and runs ALL SIX of newlibc's
 * OWN drivers together -- timer.c (§8l), display.c (§8m), keyboard.c (§8n),
 * sasi.c (§8o), console.c (§8p), and pic.c (§8q) -- the §8k gas->nasm
 * in-place ports.  Where §8l..§8q each ran one upstream driver in isolation
 * (in place of its bm_*.c mirror), this proves the six coexist in ONE image,
 * under live interrupts, with data flowing ACROSS drivers.  NOTHING from any
 * bm_*.c driver mirror is linked: the unprefixed upstream symbols (timer_*,
 * display_*, keyboard_*, console_*, pic_*, sasi_* + block_*) are the only
 * drivers in the image.  The build script's per-upstream-header rules
 * (build-newlibc-baremetal.sh) each fire on this file's `#include "<drv>.h"`
 * lines and pull the matching $NL/drivers/<drv>.c, so NO build-glue change
 * is needed -- the rules were designed additive for exactly this.
 *
 * Two ISR-driven drivers run live the whole time: the timer on IR2 and the
 * keyboard on IR6, each through the compiler-emitted ES-safe iret ABI (§6d)
 * acknowledged with the UPSTREAM pic_send_eoi (§8q).  pic.c's pic_init does
 * the §6d-mandatory full 8259A re-init before sti.  The SASI sector read
 * (phase 10) runs UNDER those live ISRs -- sasi.c's far-MMIO ES loads are
 * safe only because the §6d prologue owns ES (the §8k SAVE_ES drop, §8o).
 *
 * Cross-driver flows that no single-driver test could exercise:
 *   - the SASI LBA-0 disk label is written to the CRT through display.c and
 *     read back from VRAM (disk -> display);
 *   - typed keyboard characters are echoed to the CRT through display.c and
 *     read back from VRAM (keyboard -> display).
 *
 * Output framing/results go over the proven-good harness serial path
 * (bm_console.c's bm_puts, 7201 channel A, captured by the harness); the
 * upstream console.c TX path is exercised separately (phase 11) by a
 * console_puts line that also lands on channel A -- its presence in the
 * golden is the proof console.c's TX ran.  Disk: the known MAME Victor image
 * (label "tandon_703_mame" at LBA-0 offset 4), attached as a SCRATCH COPY
 * (hd field -> V9K_HARD_DISK).  The harness types "v9k" (V9K_KEYPOST) a few
 * seconds in.  Output is deterministic (booleans + the captured console line
 * + the received chars).  Every phase prints before it runs (5 MHz 8088).
 */

#include <stdint.h>
#include <string.h>
#include "bm_console.h"    /* harness serial: bm_puts/bm_puthex (channel A) */
#include "interrupts.h"    /* §6y shadow: interrupts_enable/disable, SAVE_ES no-ops */
#include "timer.h"         /* UPSTREAM timer.c */
#include "display.h"       /* UPSTREAM display.c (+ font_data.c) */
#include "keyboard.h"      /* UPSTREAM keyboard.c */
#include "console.h"       /* UPSTREAM console.c */
#include "pic.h"           /* UPSTREAM pic.c */
#include "block.h"         /* block registry */
#include "sasi.h"          /* UPSTREAM sasi.c */
#include "v9k_hw.h"        /* INT_*, IRQ_*, MK_FP, VIDEO_RAM_SEGMENT/BASE */

#define EXPECT_LEN   3      /* the harness types "v9k" */
#define GLYPH_OFFSET 0x60   /* VRAM cell glyph_ptr = char + 0x60 */
#define LABEL_LEN    15     /* "tandon_703_mame" */

/* Near offset of the (single) code frame, for the small-model IVT install. */
extern unsigned qbe_get_cs(void);

/*
 * pic.c's pic_init() calls interrupts_disable() and keyboard.c / pic.c's
 * flags-restore call interrupts_enable() -- both defined only in upstream
 * drivers/interrupts.c, which we deliberately do NOT link (it also defines
 * its own timer_isr/keyboard_isr that would collide with the local ISRs
 * here).  Supply the one-liners the §6y shadow interrupts.h declares.
 */
void interrupts_enable(void) {
    __asm__ volatile ("sti");
}
void interrupts_disable(void) {
    __asm__ volatile ("cli");
}

static int fails;
static volatile unsigned kbd_isr_entries;

/* Timer ISR (IR2, INT 0x42): route the tick to the UPSTREAM tick handler,
 * acknowledge with the UPSTREAM pic_send_eoi.  The §6d prologue saved ES,
 * set DS/ES=DGROUP and saved every register, so pic_send_eoi's far MMIO
 * write to the PIC command register is ES-safe (§8q). */
void __far __attribute__((interrupt)) timer_isr(void) {
    timer_tick_handler();
    pic_send_eoi(IRQ_TIMER);
}

/* Keyboard ISR (IR6, INT 0x46): service the UPSTREAM state machine, EOI. */
void __far __attribute__((interrupt)) keyboard_isr(void) {
    kbd_isr_entries++;
    keyboard_irq_handler();
    pic_send_eoi(IRQ_KEYBOARD);
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

/* Bounded spin that does NOT touch the timer (must not call timer_delay_ms,
 * which waits on ticks and would hang when delivery is frozen): returns
 * nonzero if ticks moved off `start` within `budget` outer iterations. */
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

/* A VRAM cell word is (attr << 8) | (char + 0x60) -- the same encoding the
 * upstream display.c make_screen_word uses; display_putc/puts default attr 0. */
static uint16_t screen_word(char c, uint8_t attr) {
    uint8_t glyph_ptr = (uint8_t)((uint8_t)c + GLYPH_OFFSET);
    return (uint16_t)(((uint16_t)attr << 8) | glyph_ptr);
}

static uint16_t read_cell(uint16_t pos) {
    volatile uint16_t __far *vram =
        (volatile uint16_t __far *)MK_FP(VIDEO_RAM_SEGMENT, VIDEO_RAM_BASE);
    return vram[pos];
}

static int cell_is(uint16_t pos, char c, uint8_t attr) {
    return read_cell(pos) == screen_word(c, attr);
}

/* Read back `len` chars from display row `row`, cols 0.., comparing to s. */
static int row_matches(uint8_t row, const char *s, int len) {
    uint16_t pos;
    int i;

    pos = (uint16_t)(row * DISPLAY_COLS);
    for (i = 0; i < len; i++) {
        if (!cell_is((uint16_t)(pos + i), s[i], 0))
            return 0;
    }
    return 1;
}

static void check(const char *label, int ok) {
    bm_puts(label);
    if (ok) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }
}

static sasi_device_t sasi0;
static uint8_t sector[SASI_SECTOR_SIZE];

int main(void) {
    block_device_info_t info;
    char label[LABEL_LEN + 1];
    char got[EXPECT_LEN + 1];
    int dev, ret, ngot, c;
    unsigned char imr;
    unsigned long t0;

    fails = 0;
    kbd_isr_entries = 0;

    /* Exercise console.c's init FIRST, before the captured region opens, so
     * the harness trims the channel-reset glitch byte (the §8p lesson):
     * crt0 already ran bm_console_init on channel A with the identical
     * sequence, and re-resetting the now-live 7201 emits one transient byte. */
    console_init();

    bm_puts("__V9BEGIN__\n");
    bm_puts("Victor 9000 bare-metal CAPSTONE: all six upstream drivers (qbe/minic)\n");

    /* ----- driver bring-up (§6d order: PIC re-init, then unmaskers) ----- */
    bm_puts("phase 1: upstream pic_init() + install timer(IR2)+keyboard(IR6) ISRs\n");
    pic_init();                       /* full 8259A re-init, all IRQs masked */
    install_isr(INT_TIMER, timer_isr);
    install_isr(INT_KEYBOARD, keyboard_isr);

    bm_puts("phase 2: upstream timer_init() (8253 ch2 + unmask IR2)\n");
    timer_init();

    bm_puts("phase 3: upstream keyboard_init() (VIA CS2 + unmask IR6)\n");
    keyboard_init();

    bm_puts("phase 4: upstream display_init() (font RAM + CRTC + clear)\n");
    display_init();

    bm_puts("phase 5: register UPSTREAM sasi + block_init: ");
    block_init_registry();
    memset(&sasi0, 0, sizeof(sasi0));
    sasi0.target_id = 0;
    sasi0.total_sectors = SASI_DEFAULT_TOTAL_SECTORS;
    sasi0.allow_writes = 0;            /* read-only: the capstone never writes */
    dev = sasi_register(&sasi0);
    ret = (dev >= 0) ? block_init(dev) : -1;
    if (dev >= 0 && ret == 0) {
        bm_puts("controller up\n");
    } else {
        bm_puts("FAIL\n");
        fails++;
    }

    bm_puts("phase 6: sti (timer+keyboard ISRs now live for the whole test)\n");
    __asm__ volatile ("sti");

    /* ----- the six drivers verified together ----- */
    bm_puts("phase 7: IMR is 0xbb (IR2 timer + IR6 keyboard open): ");
    imr = pic_get_mask();
    if (imr == 0xBB) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO (");
        bm_puthex(imr);
        bm_puts(")\n");
        fails++;
    }

    check("phase 8: timer ticks advance (EOI through pic_send_eoi): ",
          wait_tick_change(timer_get_ticks(), 2000));

    bm_puts("phase 9: display a banner, read it back from VRAM: ");
    display_set_cursor(0, 0);
    display_puts("CAPSTONE");
    check("", row_matches(0, "CAPSTONE", 8));

    bm_puts("phase 10: read SASI LBA 0 under the live ISRs\n");
    ret = block_get_info(dev, &info);
    if (ret != 0 || (unsigned long)info.total_sectors != 59058UL) {
        bm_puts("phase 10: FAIL geometry\n");
        fails++;
    }
    memset(sector, 0, sizeof(sector));
    ret = block_read_sector(dev, 0, sector);
    if (ret < 0) {
        bm_puts("phase 10: FAIL read\n");
        fails++;
    }
    check("phase 10: LBA-0 label is \"tandon_703_mame\": ",
          memcmp(sector + 4, "tandon_703_mame", LABEL_LEN) == 0);

    bm_puts("phase 11: cross-driver disk->display, label shown + read back: ");
    memcpy(label, sector + 4, LABEL_LEN);
    label[LABEL_LEN] = 0;
    display_set_cursor(2, 0);
    display_puts(label);
    check("", row_matches(2, label, LABEL_LEN));

    /* console.c TX path: this line also lands on the captured channel A -- its
     * presence in the golden is the proof the upstream console_puts TX ran. */
    bm_puts("phase 12: console_puts -> ");
    console_puts("UPSTREAM_console_puts_OK\n");

    /* The harness types "v9k" at ~3 s (V9K_KEYPOST_DELAY).  The keyboard ring
     * buffers every keystroke, so the slow SASI read above may have let the
     * keys arrive before this loop starts -- it drains the ring either way.
     * (The non-consuming "no key pending at start" idle check is racy once
     * typing has begun and is already gated by keyboard_upstream_bm §8n; the
     * capstone just collects the typed chars.)  Echo each char to the CRT
     * through display.c (cross-driver keyboard->display) at a known empty row. */
    bm_puts("phase 13: waiting for 3 typed chars (echoed to display): ");
    display_set_cursor(4, 0);
    ngot = 0;
    t0 = timer_get_ticks();
    while (ngot < EXPECT_LEN && (timer_get_ticks() - t0) < 1900) {
        c = keyboard_getc_nonblock();
        if (c >= 0) {
            got[ngot] = (char)c;
            ngot++;
            display_putc((char)c);
        }
    }
    got[ngot] = 0;
    if (ngot == EXPECT_LEN) {
        bm_puts("got \"");
        bm_puts(got);
        bm_puts("\"\n");
    } else {
        bm_puts("TIMEOUT (");
        bm_putu((unsigned long)ngot);
        bm_puts(")\n");
        fails++;
    }

    check("phase 14: typed chars match \"v9k\": ",
          ngot == 3 && got[0] == 'v' && got[1] == '9' && got[2] == 'k');

    check("phase 15: keyboard ISR entered: ", kbd_isr_entries > 0);

    bm_puts("phase 16: cross-driver keyboard->display, \"v9k\" echoed in VRAM: ");
    check("", row_matches(4, "v9k", 3));

    /* The PIC mask must GATE delivery, not just store bits: masking the live
     * timer IRQ freezes ticks (interrupts still globally enabled). */
    bm_puts("phase 17: pic_disable_irq(2) freezes ticks: ");
    pic_disable_irq(2);
    t0 = timer_get_ticks();
    check("", !wait_tick_change(t0, 150));

    pic_enable_irq(2);
    check("phase 18: pic_enable_irq(2) resumes ticks: ",
          wait_tick_change(timer_get_ticks(), 2000));

    bm_puts("phase 19: all six drivers alive at exit: ");
    check("", kbd_isr_entries > 0 &&
              wait_tick_change(timer_get_ticks(), 2000));

    if (fails == 0)
        bm_puts("PASS: all six upstream drivers ran together bare-metal.\n");
    else
        bm_puts("FAIL: capstone checks failed.\n");
    bm_puts("__V9END__\n");
    return 0;
}
