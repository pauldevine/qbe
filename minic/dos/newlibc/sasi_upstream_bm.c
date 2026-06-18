/*
 * sasi_upstream_bm.c -- bare-metal Victor 9000 SASI test driving the
 * UPSTREAM newlibc drivers/sasi.c (§8o, the §8l/§8m/§8n pattern applied to a
 * fourth driver -- the first block-storage one).
 *
 * Where sasi_bm.c (§6i) exercises the hand-mirrored bm_sasi.c port, this links
 * and runs newlibc's OWN drivers/sasi.c -- the file the §8k gas->nasm in-place
 * port made minic-compilable (its sasi_save_flags_cli now forks
 * `#if defined(__MINIC__)` to the Intel `pushf`/`pop word %0`/`cli` form, and
 * its SAVE_ES/RESTORE_ES collapse to no-ops via the upstream interrupts.h __MINIC__ fork --
 * the §8k decision that the §6d ISR ABI OWNS ES, so the driver need not save
 * it).  §8k proved that file COMPILES; this proves it RUNS on the bare machine
 * reading and writing real sectors, the Phase-6 end-state where newlibc's own
 * drivers replace the bm_*.c mirrors.  NOTHING from bm_sasi.c is linked.
 *
 * The §8k SAVE_ES drop is the headline being validated, so the SASI transfers
 * run UNDER A LIVE TIMER ISR: the local timer_isr is the compiler-emitted
 * ES-safe iret ABI (__attribute__((interrupt)) -> QBE `interrupt` linkage ->
 * the i8086 backend's prologue/epilogue, §6d) routing each IR2 tick to the
 * UPSTREAM timer_tick_handler() (drivers/timer.c, §8l).  sasi.c's HW_READ_BYTE/
 * HW_WRITE_BYTE load ES (`mov es,0xE020`) for every far MMIO access; if a tick
 * landed between the `mov es` and the access AND the ISR did not preserve ES,
 * the transfer would read/write the wrong segment.  A clean round-trip under
 * the live ISR is the proof that the §6d prologue's ES save makes the §8k
 * SAVE_ES drop safe -- two §8k-translated upstream drivers (sasi + timer)
 * running together under live interrupts.
 *
 * Disk: the known MAME Victor/Tandon image (victor_30mb.img, label
 * "tandon_703_mame" at LBA-0 offset 4, 59058 sectors of 512 bytes).  The
 * harness runs against a SCRATCH COPY (V9K_HARD_DISK), so the WRITE(6) at a
 * high scratch LBA is safe.  Output is deterministic (the fixed image bytes +
 * booleans); every phase prints before it runs (5 MHz 8088 rule).
 */

#include <stdint.h>
#include <string.h>
#include "bm_console.h"
#include "bm_pic.h"
#include "timer.h"      /* UPSTREAM: timer_init/get_ticks/delay_ms/tick_handler */
#include "block.h"      /* block registry: init/read/write/cache_invalidate */
#include "sasi.h"       /* UPSTREAM: sasi_register + sasi_device_t */
#include "v9k_hw.h"     /* INT_TIMER, IRQ_TIMER, INTEL_DEV_SEGMENT, PIC_COMMAND_PORT, HW_WRITE_BYTE */

#define SCRATCH_LBA   59057UL

/* Near offset of the (single) code frame, for the small-model IVT install. */
extern unsigned qbe_get_cs(void);

/* Timer ISR (IR2, INT 0x42): route the tick to the UPSTREAM handler, EOI.
 * The compiler-emitted prologue saved ES, set DS/ES=DGROUP and saved every
 * register; the EOI clears the in-service bit.  This is the ES owner that
 * lets sasi.c safely omit SAVE_ES around its far MMIO (§8k). */
void __far __attribute__((interrupt)) timer_isr(void) {
    timer_tick_handler();
    HW_WRITE_BYTE(INTEL_DEV_SEGMENT, PIC_COMMAND_PORT,
                  (uint8_t)(0x60 | IRQ_TIMER));
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

static uint16_t sector_checksum(const uint8_t *buffer) {
    uint16_t sum;
    unsigned int i;

    sum = 0;
    for (i = 0; i < SASI_SECTOR_SIZE; i++) {
        sum = (uint16_t)((sum << 1) ^ buffer[i] ^ (sum >> 15));
    }
    return sum;
}

static sasi_device_t sasi0;
static uint8_t sector[SASI_SECTOR_SIZE];
static uint8_t sector2[SASI_SECTOR_SIZE];

int main(void) {
    block_device_info_t info;
    uint16_t sum1, sum2;
    int dev, ret, i, fails;
    unsigned long t0;

    fails = 0;
    bm_puts("__V9BEGIN__\n");
    bm_puts("Victor 9000 bare-metal UPSTREAM sasi.c test (qbe/minic)\n");

    bm_puts("phase 1: PIC re-init + install timer ISR (INT 0x42)\n");
    bm_pic_init();
    install_isr(INT_TIMER, timer_isr);

    bm_puts("phase 2: upstream timer_init() (timeout clock, IR2)\n");
    timer_init();

    bm_puts("phase 3: sti (SASI transfers will run under live ticks)\n");
    __asm__ volatile ("sti");

    bm_puts("phase 4: register UPSTREAM sasi + block_init: ");
    block_init_registry();
    memset(&sasi0, 0, sizeof(sasi0));
    sasi0.target_id = 0;
    sasi0.total_sectors = SASI_DEFAULT_TOTAL_SECTORS;
    sasi0.allow_writes = 1;
    dev = sasi_register(&sasi0);
    if (dev < 0) {
        bm_puts("FAIL sasi_register\n");
        fails++;
    }
    ret = block_init(dev);
    if (ret < 0) {
        bm_puts("FAIL block_init\n");
        fails++;
    } else {
        bm_puts("controller up\n");
    }

    bm_puts("phase 5: geometry sectors==59058: ");
    ret = block_get_info(dev, &info);
    if (ret == 0 && (unsigned long)info.total_sectors == 59058UL &&
        info.sector_size == SASI_SECTOR_SIZE) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO (");
        bm_putu((unsigned long)info.total_sectors);
        bm_puts(")\n");
        fails++;
    }

    bm_puts("phase 6: read LBA 0 under live ISR\n");
    memset(sector, 0, sizeof(sector));
    ret = block_read_sector(dev, 0, sector);
    if (ret < 0) {
        bm_puts("phase 6: FAIL read\n");
        fails++;
    }
    bm_puts("phase 6: LBA0[0..15] =");
    for (i = 0; i < 16; i++) {
        bm_puts(" ");
        bm_puthex((unsigned int)sector[i]);
    }
    bm_puts("\n");

    bm_puts("phase 7: LBA-0 label is \"tandon_703_mame\": ");
    if (memcmp(sector + 4, "tandon_703_mame", 15) == 0) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    bm_puts("phase 8: uncached re-read checksum matches: ");
    sum1 = sector_checksum(sector);
    block_cache_invalidate(dev);
    memset(sector, 0, sizeof(sector));
    ret = block_read_sector(dev, 0, sector);
    sum2 = sector_checksum(sector);
    if (ret == 0 && sum1 == sum2) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    bm_puts("phase 9: WRITE(6) round-trip @ LBA 59057 under live ISR: ");
    for (i = 0; i < SASI_SECTOR_SIZE; i++) {
        sector[i] = (uint8_t)(i ^ 0xA5);
    }
    ret = block_write_sector(dev, SCRATCH_LBA, sector);
    if (ret < 0) {
        bm_puts("FAIL write\n");
        fails++;
    } else {
        block_cache_invalidate(dev);
        memset(sector2, 0, sizeof(sector2));
        ret = block_read_sector(dev, SCRATCH_LBA, sector2);
        if (ret == 0 && memcmp(sector, sector2, SASI_SECTOR_SIZE) == 0) {
            bm_puts("pattern verified\n");
        } else {
            bm_puts("READBACK MISMATCH\n");
            fails++;
        }
    }

    bm_puts("phase 10: timer still ticking after transfers: ");
    t0 = timer_get_ticks();
    timer_delay_ms(200);
    if (timer_get_ticks() != t0) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    if (fails == 0)
        bm_puts("PASS: upstream sasi.c bare-metal checks completed.\n");
    else
        bm_puts("FAIL: upstream sasi.c bare-metal checks failed.\n");
    bm_puts("__V9END__\n");
    return 0;
}
