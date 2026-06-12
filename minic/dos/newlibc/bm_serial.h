/* bm_serial.h -- bare-metal Victor 9000 interrupt-driven serial RX
 * (NEC 7201 channel B, IR1) (§6e). */
#ifndef BM_SERIAL_H
#define BM_SERIAL_H

/* Bring up 7201 channel B (9600 8N1, internal clock, 8253 counter 1)
 * with RX interrupts on IR1, install the ISR at INT 0x41, unmask IR1.
 * Call AFTER bm_interrupts_init() and BEFORE bm_interrupts_enable().
 * Channel A (the polled console) is untouched. */
void bm_serial_init(void);

/* Next received byte, -1 when the ring buffer is empty. */
int bm_serial_getc_nonblock(void);

/* Battery diagnostics. */
unsigned int bm_serial_isr_count(void);
unsigned int bm_serial_overruns(void);

#endif
