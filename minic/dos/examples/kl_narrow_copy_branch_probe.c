/*
 * kl_narrow_copy_branch_probe.c — regression for the i8086 copy.c
 * class-narrowing fold bug.
 *
 * `(int)(a >> 31)` casts a 32-bit (Kl) value to int (Kw), which minic emits as
 *   %w =w copy %l
 * a real 16-bit truncation on i8086 (l = 4-byte pair, w = 2-byte reg).  When
 * that w value feeds a `&&` (hence a jnz), copy.c used to fold the copy to the
 * wide temp; spill then parked the value in a 4-byte slot and rega never
 * reloaded its low word into the branch register, so the branch tested
 * garbage and the sign decision came out INVERTED.  See copy.c copyref().
 *
 * Pure integer (no float / no pointers), so a plain medium build pins it; the
 * fix lives in generic copy.c gated on T.wordsz == 2 (all i8086 models).
 */
#include <stdio.h>

/* kept in its own function so the && short-circuit branch is compiled against
 * runtime args (no constant folding across the call boundary). */
static int sign_and_ne(unsigned long a, unsigned long t)
{
	if ((int)(a >> 31) && (t != a))
		return 1;
	return 0;
}

/* the bare classifier (shift result returned directly, no branch on it) — this
 * always worked, included as a control. */
static int just_sign(unsigned long a)
{
	return (int)(a >> 31);
}

int main(void)
{
	printf("neg_ne=%d\n", sign_and_ne(0x80000000UL, 0x12345678UL)); /* 1 */
	printf("neg_eq=%d\n", sign_and_ne(0x80000000UL, 0x80000000UL)); /* 0 */
	printf("pos_ne=%d\n", sign_and_ne(0x40000000UL, 0x12345678UL)); /* 0 */
	printf("sign_neg=%d\n", just_sign(0xC0000000UL));               /* 1 */
	printf("sign_pos=%d\n", just_sign(0x40000000UL));               /* 0 */
	return 0;
}
