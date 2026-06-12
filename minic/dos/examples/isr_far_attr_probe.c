/* isr_far_attr_probe.c — §6a: `void __far __attribute__((interrupt)) f(void);`
 *
 * ia16-elf-gcc spells far interrupt handlers with __far BETWEEN the return
 * type and the attribute (newlibc interrupts.h prototypes).  minic accepted
 * `void __attribute__((interrupt)) f(void)` (type attropt IDENT) but had no
 * production for the interposed __far, so every newlibc TU including
 * interrupts.h died with a parse error.  The __far is accepted and dropped
 * (function far-ness is a memory-model property on this toolchain).
 *
 * PROTOTYPE only: ISR *definitions* are a designed gap until the Phase 6
 * ISR strategy lands — minic's vestigial interrupt body emission produces
 * `asm "iret"` with no block terminator (QBE rejects it) and would skip
 * the frame epilogue anyway.  drivers/interrupts.c stays red until then.
 */
#include <stdio.h>

void __far __attribute__((interrupt)) fake_timer_isr(void);
void __far __attribute__((interrupt)) fake_kbd_isr(void);

static int poll_count = 2;

int main()
{
	printf("isr-attr %d\n", poll_count);
	return 0;
}
