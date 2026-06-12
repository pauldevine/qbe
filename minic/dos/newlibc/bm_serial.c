/*
 * bm_serial.c -- bare-metal Victor 9000 interrupt-driven serial RX on
 * NEC 7201 channel B (§6e, Phase-6 step 4b).
 *
 * newlibc's serial ISR was a stub (its loopback test polls); this is a
 * real receive path: 7201 channel B programmed like the validated
 * channel-A console (internal clock via VIA2 PA bit 1, 8253 counter 1
 * as the baud generator -- counter 1 is Serial B -- mode 2 divisor 8
 * for 9600, channel reset + WR4/WR3/WR5), plus WR1 = 0x18 (interrupt
 * on ALL received characters).  Both 7201 channels share IR1; only
 * channel B has interrupts enabled here, so the ISR just drains B.
 *
 * The drain loop matters: the 8259A is edge-triggered (ICW1 0x17) and
 * the 7201 INT line stays asserted while a character is pending, so an
 * ISR that left a byte behind would never see another edge.  Draining
 * until RR0 reports empty guarantees the line drops before the EOI.
 */

#include <stdint.h>
#include "v9k_hw.h"
#include "bm_pic.h"
#include "bm_interrupts.h"
#include "bm_serial.h"

#define BM_SERIAL_BAUD        9600
#define RX_BUFFER_SIZE        64      /* power of two */
#define RX_BUFFER_MASK        (RX_BUFFER_SIZE - 1)

static volatile uint8_t rx_buffer[RX_BUFFER_SIZE];
static volatile uint8_t rx_head;
static volatile uint8_t rx_tail;
static volatile uint16_t rx_overruns;
static volatile uint16_t isr_entries;

static void nec_ctl_b(uint8_t val) {
    HW_WRITE_BYTE(INTEL_DEV_SEGMENT, NEC7201_CTL_B, val);
}

static uint8_t nec_status_b(void) {
    return HW_READ_BYTE(INTEL_DEV_SEGMENT, NEC7201_CTL_B);   /* RR0 */
}

static void serial_b_write_control(uint8_t reg, uint8_t val) {
    nec_ctl_b(reg);
    nec_ctl_b(val);
}

/* Producer: ISR context only. */
static void rx_push(uint8_t byte) {
    uint8_t next_head = (uint8_t)((rx_head + 1) & RX_BUFFER_MASK);

    if (next_head == rx_tail) {
        rx_overruns = rx_overruns + 1;
        return;
    }
    rx_buffer[rx_head] = byte;
    rx_head = next_head;
}

/* IR1 ISR: drain every pending channel-B byte, then the specific EOI. */
void __far __attribute__((interrupt)) bm_serial_isr(void) {
    isr_entries = isr_entries + 1;
    while ((nec_status_b() & NEC7201_RX_CHAR_AVAIL) != 0)
        rx_push(HW_READ_BYTE(INTEL_DEV_SEGMENT, NEC7201_DATA_B));
    HW_WRITE_BYTE(INTEL_DEV_SEGMENT, PIC_COMMAND_PORT,
                  (uint8_t)(0x60 | IRQ_SERIAL));
}

void bm_serial_init(void) {
    uint8_t porta;
    uint16_t divisor;
    volatile int i;

    rx_head = 0;
    rx_tail = 0;
    rx_overruns = 0;
    isr_entries = 0;

    /* Serial B clock source = internal (VIA2 port A bit 1 low). */
    porta = HW_READ_BYTE(PHASE2_DEV_SEGMENT, VIA2_OFFSET + 1);
    HW_WRITE_BYTE(PHASE2_DEV_SEGMENT, VIA2_OFFSET + 1,
                  porta & (uint8_t)~VIA2_INT_EXT_B);

    /* 8253 counter 1 = Serial B baud (mode 2, LSB+MSB):
     * divisor = 1.25 MHz / (baud * 16x clock) = 8 at 9600. */
    divisor = (uint16_t)(TIMER_CHANNEL_01_CLOCK
                         / ((uint32_t)BM_SERIAL_BAUD * 16));
    if (divisor < 1)
        divisor = 1;
    HW_WRITE_BYTE(INTEL_DEV_SEGMENT, TIMER_CONTROL, 0x74);
    HW_WRITE_BYTE(INTEL_DEV_SEGMENT, TIMER_COUNTER_1,
                  (uint8_t)(divisor & 0xFF));
    HW_WRITE_BYTE(INTEL_DEV_SEGMENT, TIMER_COUNTER_1,
                  (uint8_t)(divisor >> 8));

    /* 7201 channel B: reset, then the console's validated WR sequence
     * with RX interrupts ON. */
    nec_ctl_b(NEC7201_CMD_CHANNEL_RESET);
    for (i = 0; i < 100; i++)
        ;                               /* let the reset settle */
    serial_b_write_control(0, 0x00);
    serial_b_write_control(4, 0x44);    /* x16 clock, 1 stop bit, no parity */
    serial_b_write_control(3, 0xC1);    /* RX enable, 8 bits/char */
    serial_b_write_control(5, 0xEA);    /* TX enable, 8 bits, RTS, DTR */
    serial_b_write_control(1, 0x18);    /* interrupt on all RX chars */

    /* ISR before unmask; interrupts are still globally disabled. */
    bm_install_isr(INT_SERIAL, (bm_isr_fn_t)bm_serial_isr);
    bm_pic_unmask(IRQ_SERIAL);
}

/* Consumer: main context only. */
int bm_serial_getc_nonblock(void) {
    uint8_t byte;

    if (rx_head == rx_tail)
        return -1;
    byte = rx_buffer[rx_tail];
    rx_tail = (uint8_t)((rx_tail + 1) & RX_BUFFER_MASK);
    return byte;
}

unsigned int bm_serial_isr_count(void) {
    return isr_entries;
}

unsigned int bm_serial_overruns(void) {
    return rx_overruns;
}
