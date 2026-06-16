/*
 * aoa_instance_probe.c -- array-typedef INSTANCE declarations at every
 * declaration site (§8g), the last bounded gaps left open by §7e/§7j.
 *
 * `jmp_buf` is `typedef int jmp_buf[8]`, so a plain instance `jmp_buf env;`
 * is itself an array.  §7e/§7j made the BRACKETED array-of-jmp_buf forms
 * (`jmp_buf bufs[N]`, `jmp_buf a[2], b[2]`) and the BLOCK-LOCAL single
 * instance correct, but several plain-INSTANCE and multi-declarator sites
 * still mistreated `jmp_buf env` as a single scalar `int` (2-byte storage +
 * a scalar value load) instead of a 16-byte array decaying to its address,
 * and one shape was an outright parse error:
 *
 *   (1) file-scope single instance   `static jmp_buf g;`        (mis-sized)
 *   (2) file-scope multi instance    `static jmp_buf a, b;`     (mis-sized)
 *   (3) file-scope array-FIRST aoa   `static jmp_buf fa[2],fb[2];` (PARSE ERROR)
 *   (4) block-local multi instance   `jmp_buf a, b;`            (mis-sized)
 *   (5) static-local single instance `static jmp_buf s;` (in fn)(mis-sized)
 *
 * The fix sizes each array-typedef instance D*sizeof(elem), registers it
 * IDIR(elem) with the array flag so it decays to its address (no scalar
 * load), and adds the file-scope array-first multi-decl production
 * `[expr] ',' ext_decllist ';'`.  Bug-loud: on the unfixed compiler (3) is
 * a hard parse error (build fails) and (1)/(2)/(4)/(5) pass setjmp() a
 * garbage pointer loaded from a 2-byte slot — a crash or wrong value.
 *
 * Each case round-trips a distinct value through setjmp/longjmp IN-FRAME
 * (the file-scope/static buffers persist, but the longjmp fires while the
 * setjmp frame is still live, so no cross-frame UB).
 *
 * Model-independent (program output only); gated medium + compact + large,
 * matching arr_jmpbuf_probe / aoa_extended_probe.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/aoa_instance_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/aoa_instance_probe/aoa_instance_probe.exe \
 *             | diff - minic/dos/tests/aoa_instance_probe.golden.txt
 */

#include <stdio.h>
#include <setjmp.h>

/* (1) file-scope single instance */
static jmp_buf g_single;
/* (2) file-scope multi instance (the §8g GAP2 at file scope) */
static jmp_buf g_a, g_b;
/* (3) file-scope array-first aoa multi-decl (the §8g GAP1 parse-error form) */
static jmp_buf g_fa[2], g_fb[2];

static int
file_single(void)
{
	int r = setjmp(g_single);
	if (r == 0)
		longjmp(g_single, 11);
	return r;                       /* 11 */
}

static int
file_multi(int which)
{
	if (which == 0) {
		int r = setjmp(g_a);
		if (r == 0)
			longjmp(g_a, 21);
		return r;               /* 21 */
	} else {
		int r = setjmp(g_b);
		if (r == 0)
			longjmp(g_b, 22);
		return r;               /* 22 */
	}
}

static int
file_aoa(int i, int useb)
{
	if (useb) {
		int r = setjmp(g_fb[i]);
		if (r == 0)
			longjmp(g_fb[i], i + 40);
		return r;               /* 40 / 41 */
	} else {
		int r = setjmp(g_fa[i]);
		if (r == 0)
			longjmp(g_fa[i], i + 30);
		return r;               /* 30 / 31 */
	}
}

static int
block_multi(int useb)
{
	jmp_buf a, b;                   /* (4) block-local multi instance */
	if (useb) {
		int r = setjmp(b);
		if (r == 0)
			longjmp(b, 52);
		return r;               /* 52 */
	} else {
		int r = setjmp(a);
		if (r == 0)
			longjmp(a, 51);
		return r;               /* 51 */
	}
}

static int
static_single(void)
{
	static jmp_buf s;               /* (5) static-local single instance */
	int r = setjmp(s);
	if (r == 0)
		longjmp(s, 61);
	return r;                       /* 61 */
}

int
main(void)
{
	printf("file_single=%d\n", file_single());
	printf("file_multi=%d,%d\n", file_multi(0), file_multi(1));
	printf("file_aoa=%d,%d,%d,%d\n",
	       file_aoa(0, 0), file_aoa(1, 0), file_aoa(0, 1), file_aoa(1, 1));
	printf("block_multi=%d,%d\n", block_multi(0), block_multi(1));
	printf("static_single=%d\n", static_single());
	return 0;
}
