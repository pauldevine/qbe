/*
 * bm_console.c -- bare-metal Victor 9000 polled serial console (§6c,
 * Phase-6 step 3).
 *
 * A minic-dialect port of newlibc's drivers/console.c TX path: same
 * validated register sequence (VIA2 internal clock select, 8253 Counter 0
 * baud — Counter 0 is Serial A, NOT Counter 1 — then NEC 7201 channel A
 * reset + WR4/WR3/WR5/WR1), but all hardware access through plain
 * volatile-far MMIO (v9k_hw.h HW_READ/WRITE_BYTE).  The original's inline
 * asm exists only as ia16-gcc workarounds (forced byte stores, ES
 * save/restore around far derefs) that this toolchain doesn't need: minic
 * far volatile stores are single byte stores and ES is managed per-access.
 *
 * Polled TX only, no interrupts, no RX — just enough for a bare-metal
 * program to print over -rs232a null_modem under MAME (and the real
 * machine's serial port A).
 */

#include <stdint.h>
#include "v9k_hw.h"

#define BM_BAUD_RATE   9600
#define BM_TX_TIMEOUT  60000U   /* prevent hangs when no terminal drains TX */

static void intel_write(uint16_t off, uint8_t val) {
    HW_WRITE_BYTE(INTEL_DEV_SEGMENT, off, val);
}

static void nec_ctl_write(uint8_t val) {
    HW_WRITE_BYTE(INTEL_DEV_SEGMENT, NEC7201_CTL_A, val);
}

static uint8_t nec_status(void) {
    return HW_READ_BYTE(INTEL_DEV_SEGMENT, NEC7201_CTL_A);  /* RR0 */
}

static void serial_write_control(uint8_t reg, uint8_t val) {
    nec_ctl_write(reg);   /* WR0: select register */
    nec_ctl_write(val);   /* write it */
}

/* --- newlibc raw-serial console API on channel A (console_putc/getc/...).
 * bm_console_init enables channel-A RX (WR3=0xC1) already; these complete
 * the polled RX path the upstream drivers/console.c exposes.  bm_shim.c
 * aliases them to the unprefixed console_* names for serial_loopback_test,
 * which drives them through a hardware TXD->RXD loopback on channel A.
 * (Unreferenced -- so stripped by --gc-sections -- in every other build.) */
void bm_console_putc(char c) {
    uint16_t timeout;

    if (c == '\n')
        bm_console_putc('\r');      /* match upstream console_putc CRLF */
    timeout = BM_TX_TIMEOUT;
    while ((nec_status() & NEC7201_TX_BUF_EMPTY) == 0 && timeout != 0)
        timeout--;
    if (timeout != 0)
        HW_WRITE_BYTE(INTEL_DEV_SEGMENT, NEC7201_DATA_A, (uint8_t)c);
}

int bm_console_rx_ready(void) {
    return (nec_status() & NEC7201_RX_CHAR_AVAIL) != 0;
}

int bm_console_getc_nonblock(void) {
    if ((nec_status() & NEC7201_RX_CHAR_AVAIL) == 0)
        return -1;
    return (int)HW_READ_BYTE(INTEL_DEV_SEGMENT, NEC7201_DATA_A);
}

int bm_console_getc(void) {
    while ((nec_status() & NEC7201_RX_CHAR_AVAIL) == 0)
        ;
    return (int)HW_READ_BYTE(INTEL_DEV_SEGMENT, NEC7201_DATA_A);
}

#ifdef BM_SERIAL_LOOPBACK
/* --- serial_loopback_test only: the test commandeers channel A as its
 * TXD->RXD loopback data path (console_putc above), so the captured harness
 * debug console (bm_putc, below) moves to channel B -- the same channel-B
 * program bm_serial.c uses for RX, but polled TX (WR1=0, no interrupt). --- */
static void nec_ctl_write_b(uint8_t val) {
    HW_WRITE_BYTE(INTEL_DEV_SEGMENT, NEC7201_CTL_B, val);
}

static uint8_t nec_status_b(void) {
    return HW_READ_BYTE(INTEL_DEV_SEGMENT, NEC7201_CTL_B);  /* RR0 */
}

static void serial_write_control_b(uint8_t reg, uint8_t val) {
    nec_ctl_write_b(reg);
    nec_ctl_write_b(val);
}

static void bm_console_b_init(void) {
    uint8_t porta;
    uint16_t divisor;
    volatile int i;

    /* Serial B clock source = internal (VIA2 port A bit 1 low). */
    porta = HW_READ_BYTE(PHASE2_DEV_SEGMENT, VIA2_OFFSET + 1);
    HW_WRITE_BYTE(PHASE2_DEV_SEGMENT, VIA2_OFFSET + 1,
                  porta & (uint8_t)~VIA2_INT_EXT_B);

    /* 8253 counter 1 = Serial B baud (mode 2, LSB+MSB). */
    divisor = (uint16_t)(TIMER_CHANNEL_01_CLOCK
                         / ((uint32_t)BM_BAUD_RATE * 16));
    if (divisor < 1)
        divisor = 1;
    intel_write(TIMER_CONTROL, 0x74);   /* channel 1, LSB+MSB, mode 2 */
    intel_write(TIMER_COUNTER_1, (uint8_t)(divisor & 0xFF));
    intel_write(TIMER_COUNTER_1, (uint8_t)(divisor >> 8));

    /* 7201 channel B: reset + the validated WR sequence, polled. */
    nec_ctl_write_b(NEC7201_CMD_CHANNEL_RESET);
    for (i = 0; i < 100; i++)
        ;
    serial_write_control_b(0, 0x00);
    serial_write_control_b(4, 0x44);    /* x16 clock, 1 stop bit, no parity */
    serial_write_control_b(3, 0xC1);    /* RX enable, 8 bits/char */
    serial_write_control_b(5, 0xEA);    /* TX enable, 8 bits, RTS, DTR */
    serial_write_control_b(1, 0x00);    /* polled: no interrupts */
}
#endif

void bm_console_init(void) {
    uint8_t porta;
    uint16_t divisor;
    volatile int i;

    /* Serial A clock source = internal (VIA2 port A bit 0 low). */
    porta = HW_READ_BYTE(PHASE2_DEV_SEGMENT, VIA2_OFFSET + 1);
    HW_WRITE_BYTE(PHASE2_DEV_SEGMENT, VIA2_OFFSET + 1,
                  porta & (uint8_t)~VIA2_INT_EXT_A);

    /* 8253 Counter 0 = Serial A baud rate generator (mode 2, LSB+MSB):
     * divisor = 1.25 MHz / (baud * 16x clock). */
    divisor = (uint16_t)(TIMER_CHANNEL_01_CLOCK
                         / ((uint32_t)BM_BAUD_RATE * 16));
    if (divisor < 1)
        divisor = 1;
    intel_write(TIMER_CONTROL, 0x34);   /* channel 0, LSB+MSB, mode 2 */
    intel_write(TIMER_COUNTER_0, (uint8_t)(divisor & 0xFF));
    intel_write(TIMER_COUNTER_0, (uint8_t)(divisor >> 8));

    /* NEC 7201 channel A. */
    nec_ctl_write(NEC7201_CMD_CHANNEL_RESET);
    for (i = 0; i < 100; i++)
        ;                               /* let the reset settle */
    serial_write_control(0, 0x00);
    serial_write_control(4, 0x44);      /* x16 clock, 1 stop bit, no parity */
    serial_write_control(3, 0xC1);      /* RX enable, 8 bits/char */
    serial_write_control(5, 0xEA);      /* TX enable, 8 bits, RTS, DTR */
    serial_write_control(1, 0x00);      /* polled: no interrupts */

#ifdef BM_SERIAL_LOOPBACK
    bm_console_b_init();                /* harness console moves to channel B */
#endif
}

void bm_board_init(void) {
    bm_console_init();
}

void bm_putc(char c) {
    uint16_t timeout;

    if (c == '\n')
        bm_putc('\r');

    timeout = BM_TX_TIMEOUT;
#ifdef BM_SERIAL_LOOPBACK
    /* channel B: channel A is the test's TXD->RXD loopback (uncaptured). */
    while ((nec_status_b() & NEC7201_TX_BUF_EMPTY) == 0 && timeout != 0)
        timeout--;
    if (timeout != 0)
        HW_WRITE_BYTE(INTEL_DEV_SEGMENT, NEC7201_DATA_B, (uint8_t)c);
#else
    while ((nec_status() & NEC7201_TX_BUF_EMPTY) == 0 && timeout != 0)
        timeout--;
    if (timeout != 0)
        HW_WRITE_BYTE(INTEL_DEV_SEGMENT, NEC7201_DATA_A, (uint8_t)c);
#endif
}

void bm_puts(const char *s) {
    while (*s) {
        bm_putc(*s);
        s++;
    }
}

void bm_putu(unsigned long v) {
    char buf[12];
    int i;

    if (v == 0) {
        bm_putc('0');
        return;
    }
    i = 0;
    while (v != 0) {
        buf[i] = (char)('0' + (int)(v % 10));
        i++;
        v = v / 10;
    }
    while (i > 0) {
        i--;
        bm_putc(buf[i]);
    }
}

void bm_puthex(unsigned int v) {
    int shift;
    unsigned int nib;

    bm_putc('0');
    bm_putc('x');
    for (shift = 12; shift >= 0; shift -= 4) {
        nib = (v >> shift) & 0xF;
        bm_putc((char)(nib < 10 ? '0' + nib : 'a' + (nib - 10)));
    }
}
