/*
 * interrupts.h -- minic-dialect port of newlibc's drivers/interrupts.h
 * (§6y, Phase-6 step 4 follow-up).
 *
 * The upstream header is mostly a pure-declaration API contract, but it
 * carries two ia16-elf-gcc-isms minic does not speak:
 *
 *   1. SAVE_ES / RESTORE_ES -- GCC extended __asm__ with "=m"/"m" operand
 *      constraints.  These are the same ES damage-control sites the §6e/§6i
 *      driver ports DROPPED, not translated: on this toolchain the §6d ISR
 *      ABI owns ES (the compiler-emitted prologue saves it to CS-local
 *      static memory and restores it before iret), and a plain volatile far
 *      access carries its own segment, so there is nothing for caller code
 *      to save.  Here they become no-ops.
 *
 *   2. get_interrupt_vector() -- a `static inline` helper built on SAVE_ES.
 *      minic does not drop unreferenced static functions the way gcc does
 *      (it emits one per including TU), and it passes inline asm through
 *      verbatim, so the upstream body would emit AT&T `movw %es,...` that
 *      nasm (Intel syntax) rejects.  Reimplemented as a plain far-pointer
 *      IVT read -- identical observable behaviour, valid i8086 codegen even
 *      when emitted dead (then GC'd at link).
 *
 * Lives in minic/dos/newlibc/ (searched before $NL/drivers in the
 * bare-metal build's include path), so an UNMODIFIED upstream test that
 * `#include "interrupts.h"` links against this port -- exactly the
 * bm_interrupts.h / bm_sasi.h porting pattern, applied to the header the
 * upstream test names directly.  The declaration surface mirrors upstream
 * name-for-name (set_interrupt_vector, the three ISR prototypes, the
 * interrupts_init/enable/disable trio, ivt_entry_t, ISR_HANDLER).
 */

#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>
#include "v9k_hw.h"

/* IVT entry: segment:offset far pointer (4 bytes) -- name-for-name. */
typedef struct {
    uint16_t offset;    /* Offset part of far pointer */
    uint16_t segment;   /* Segment part of far pointer */
} ivt_entry_t;

void set_interrupt_vector(uint8_t int_num, ivt_entry_t handler);

/* ia16-elf GCC interrupt syntax -- the §6d ABI (`__attribute__((interrupt))`
 * → QBE `interrupt` linkage → the i8086 backend's ES-safe iret prologue). */
#define ISR_HANDLER __far __attribute__((interrupt))

void ISR_HANDLER timer_isr(void);
void ISR_HANDLER keyboard_isr(void);
void ISR_HANDLER serial_isr(void);

void interrupts_init(void);
void interrupts_enable(void);
void interrupts_disable(void);

/* ES save/restore are no-ops on this toolchain: the §6d ISR ABI owns ES and
 * volatile far accesses carry their own segment (see header comment). */
#define SAVE_ES(var)    ((void)(var))
#define RESTORE_ES(var) ((void)(var))

/*
 * get_interrupt_vector - Read IVT entry for interrupt number.
 * Plain far-pointer read; no extended asm, no ES juggling.
 */
static inline void get_interrupt_vector(uint8_t int_num, ivt_entry_t *entry) {
    ivt_entry_t __far *ivt;

    ivt = (ivt_entry_t __far *)MK_FP(0, int_num * 4);
    *entry = *ivt;
}

#endif /* INTERRUPTS_H */
