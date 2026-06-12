/* bm_interrupts.h -- bare-metal Victor 9000 interrupt framework (§6d). */
#ifndef BM_INTERRUPTS_H
#define BM_INTERRUPTS_H

/* Install handlers into the IVT.  Call with interrupts still disabled
 * (the bare-metal flow: bm_interrupts_init() then bm_timer_init() then
 * bm_interrupts_enable()). */
void bm_interrupts_init(void);
void bm_interrupts_enable(void);
void bm_interrupts_disable(void);
void bm_set_vector(unsigned char int_num,
                   unsigned short seg, unsigned short off);

#endif
