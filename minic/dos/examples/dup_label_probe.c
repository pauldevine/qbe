/*
 * dup_label_probe.c -- per-function C label uniquification.
 *
 * C `goto` labels are function-scoped: two different functions may both
 * define a label of the same name.  minic emits each as a flat per-TU asm
 * block label `@user_<name>`, so two functions sharing a label name (e.g.
 * the two `too_short:` labels in MicroPython's py/runtime.c) collided into
 * one asm symbol -- the assembler then reported it "inconsistently
 * redefined" and the jumps in the second function targeted the FIRST
 * function's label.  minic now suffixes every user label with a per-function
 * id (`@user_<name>_F<id>`), so the labels are distinct and each goto stays
 * within its own function.
 *
 * This probe defines the SAME label name (`retry`, `done`) in three separate
 * functions and verifies each goto reaches its own function's target.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/dup_label_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/dup_label_probe/dup_label_probe.exe \
 *             | diff - minic/dos/tests/dup_label_probe.golden.txt
 *
 * Frontend-only / model-agnostic: runs under medium + large.
 */

#include <stdio.h>

/* Function 1: label `retry` loops while acc < limit; `done` returns. */
static int
count_up(int limit)
{
	int acc = 0;
retry:
	if (acc < limit) {
		acc++;
		goto retry;
	}
	goto done;
done:
	return acc;             /* limit */
}

/* Function 2: SAME label names, different bodies.  `retry` doubles until
 * over the cap; `done` returns the value. */
static int
double_to(int cap)
{
	int v = 1;
retry:
	if (v < cap) {
		v += v;
		goto retry;
	}
	goto done;
done:
	return v;               /* smallest power of two >= cap */
}

/* Function 3: SAME label names again, with a forward+backward mix. */
static int
sum_down(int n)
{
	int s = 0;
retry:
	if (n <= 0)
		goto done;
	s += n;
	n--;
	goto retry;
done:
	return s;               /* n*(n+1)/2 of the original n */
}

int
main(void)
{
	printf("a=%d\r\n", count_up(7));      /* 7 */
	printf("b=%d\r\n", double_to(20));    /* 32 */
	printf("c=%d\r\n", sum_down(5));      /* 15 */
	printf("d=%d\r\n", count_up(0));      /* 0 */
	printf("e=%d\r\n", double_to(1));     /* 1 */
	return 0;
}
