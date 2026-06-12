/* bm_timer.h -- bare-metal Victor 9000 100 Hz system timer (§6d). */
#ifndef BM_TIMER_H
#define BM_TIMER_H

void bm_timer_init(void);
void bm_timer_tick_handler(void);
unsigned long bm_timer_get_ticks(void);
void bm_timer_delay_ms(unsigned long ms);

#endif
