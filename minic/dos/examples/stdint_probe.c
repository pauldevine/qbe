/*
 * stdint_probe.c -- medium-model runtime gate for minic/include/stdint.h.
 *
 * Pins the shipped <stdint.h> after the 2026-05-29 correctness fix:
 *   - int32_t/uint32_t are `long` (4 bytes), NOT `int` (was wrongly 16-bit)
 *   - intptr_t/uintptr_t are model-dependent (2 bytes under near-data/medium)
 *   - INT32_MAX / INTPTR_MAX / SIZE_MAX carry the target's widths
 * The 32-bit round-trip (100000*23) would truncate to a bogus 16-bit value
 * if int32_t were still `int`, so it is the load-bearing assertion.
 *
 * Build:  tools/build-example.sh --model=medium minic/dos/examples/stdint_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/stdint_probe/stdint_probe.exe \
 *             | diff - minic/dos/tests/stdint_probe.golden.txt
 * Wired into tools/test-dos.sh under the "medium runtime" section.
 */
#include <stdint.h>
#include <stdio.h>

int
main(void)
{
	int32_t a;
	uint32_t u;

	/* sizes */
	printf("sz_i8=%d (want 1)\r\n", (int)sizeof(int8_t));
	printf("sz_i16=%d (want 2)\r\n", (int)sizeof(int16_t));
	printf("sz_i32=%d (want 4)\r\n", (int)sizeof(int32_t));
	printf("sz_u32=%d (want 4)\r\n", (int)sizeof(uint32_t));
	printf("sz_i64=%d (want 4)\r\n", (int)sizeof(int64_t));
	printf("sz_imax=%d (want 4)\r\n", (int)sizeof(intmax_t));
	printf("sz_iptr=%d (want 2)\r\n", (int)sizeof(intptr_t));
	printf("sz_uptr=%d (want 2)\r\n", (int)sizeof(uintptr_t));

	/* 32-bit arithmetic must NOT truncate to 16 bits */
	a = 100000;
	a = a * 23;
	printf("i32_mul=%ld (want 2300000)\r\n", a);
	u = 4000000000UL;
	printf("u32_big=%lu (want 4000000000)\r\n", u);

	/* limit macros */
	printf("i32_max=%ld (want 2147483647)\r\n", (long)INT32_MAX);
	printf("iptr_max=%d (want 32767)\r\n", (int)INTPTR_MAX);
	printf("size_max=%u (want 65535)\r\n", (unsigned)SIZE_MAX);

	return 0;
}
