/*
 * codegen_term_probe.c -- three minic SSA-emission fixes surfaced by pushing
 * MicroPython py/*.c through the full minic -> qbe(-t i8086) codegen pipeline
 * (the parse-only spike never ran qbe, so these went undetected):
 *
 *  (1) Sub-word arithmetic result class.  uint16_t+uint16_t and uint8_t+uint8_t
 *      where BOTH operands share the narrow type: prom() returned the operand
 *      type, so the add result temp was emitted as `=h`/`=b` -- not a valid
 *      QBE temp class (only w/l/s/d).  Now widened to `w` via irtyp_ret(), which
 *      is also C-correct (integer promotion computes in int width, truncating
 *      only on store).  (py/ringbuf.c, gc.c, emitbc.c)
 *
 *  (2) Function whose textual tail is a goto-reached fall-through block.  A
 *      Seq reported r1||r2 termination, so an earlier `return` masked a trailing
 *      labeled block that falls through, and minic skipped the synthetic `ret`
 *      -> "last block misses jump".  Now a Seq whose tail contains a label
 *      reports the tail's termination alone.  (py/parsenum.c, compile.c, objstr.c)
 *
 *  (3) goto Label between switch cases.  genswitchbody short-circuited past a
 *      Seq tail when the prior case body terminated (break) and the tail held
 *      no *case* label -- dropping a plain goto target sitting between cases
 *      -> "block @user_X is used undefined".  Now goto labels are kept too.
 *      (py/runtime.c's mp_binary_op `power_overflow:`)
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/codegen_term_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/codegen_term_probe/codegen_term_probe.exe \
 *             | diff - minic/dos/tests/codegen_term_probe.golden.txt
 *
 * Frontend-only / model-agnostic: runs under medium + large.
 */

#include <stdio.h>

typedef unsigned short u16;
typedef unsigned char  u8;

/* (1) same-typed sub-word arithmetic -- the result must be 16-bit (w) and
 * truncate only on store back to the narrow type. */
static u16
add16(u16 a, u16 b)
{
	u16 r = a + b;          /* was `=h add` (invalid) */
	return r;
}

static u8
add8(u8 a, u8 b)
{
	u8 r = a + b;           /* was `=b add` (invalid); wraps mod 256 on store */
	return r;
}

/* (2) tail goto-label that falls through.  The middle `return` used to mask
 * the trailing labeled block; minic then emitted no terminator for it. */
static int
tail_label(int x)
{
	int acc = 0;
	if (x == 1)
		return 100;
	goto finish;
finish:
	acc = x * 2;
	return acc;             /* labeled tail; falls through to here */
}

/* (3) goto label between switch cases (the py/runtime.c shape). */
static int
mid_switch(int op, int v)
{
	int r = 0;
	switch (op) {
	case 0:
		r = v;
		break;
	recover:                /* plain goto label between cases */
		r = v + 1000;
		goto done;
	case 1:
		if (v < 0)
			goto recover;
		r = v * 10;
		break;
	default:
		r = -1;
	}
done:
	return r;
}

int
main(void)
{
	printf("a=%u\r\n", (unsigned)add16(40000u, 30000u)); /* 70000 & 0xffff = 4464 */
	printf("b=%u\r\n", (unsigned)add8(200u, 100u));      /* 300 & 0xff = 44 */
	printf("c=%d\r\n", tail_label(5));                   /* 10 */
	printf("d=%d\r\n", tail_label(1));                   /* 100 */
	printf("e=%d\r\n", mid_switch(0, 7));                /* 7 */
	printf("f=%d\r\n", mid_switch(1, 3));                /* 30 */
	printf("g=%d\r\n", mid_switch(1, -2));               /* recover -> 1000 */
	return 0;
}
