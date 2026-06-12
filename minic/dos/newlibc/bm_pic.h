/* bm_pic.h -- bare-metal Victor 9000 8259A PIC driver (§6d). */
#ifndef BM_PIC_H
#define BM_PIC_H

/* Full re-init: ICW1 0x17 (edge, single, ADI=4 — the Victor ROM /
 * MS-DOS value, NOT the IBM-PC 0x11), base 0x40, 8086 normal-EOI
 * mode, stale in-service bits cleared, ALL IRQs masked.  Call with
 * interrupts disabled. */
void bm_pic_init(void);
void bm_pic_unmask(unsigned char irq);
void bm_pic_mask(unsigned char irq);
/* IMR readback / whole-mask write (OCW1 data register, E000:0001). */
unsigned char bm_pic_get_mask(void);
void bm_pic_set_mask(unsigned char mask);

#endif
