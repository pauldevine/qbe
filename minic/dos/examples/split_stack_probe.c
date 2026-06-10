/*
 * split_stack_probe.c — SS != DS (separate stack segment) end-to-end.
 *
 * §4v: omf_link --separate-stack moves the C stack OUT of DGROUP into
 * its own segment, so SS != DS at runtime.  Three toolchain layers had
 * SS==DS assumptions:
 *
 *   1. qbe (i8086/emit.c): register-indirect NEAR derefs — the isel
 *      narrowing of an Oaddr-of-slot address to Kw produces
 *      `lea bx, [bp-N]; mov [bx]`, a DS-relative deref of an
 *      SS-relative offset.  `qbe -s` adds ss: overrides (near_seg).
 *   2. libstub (_far_* helpers): `push ss / pop ds` as "restore
 *      DS=DGROUP", `mov dx, ss` as "DGROUP segment" for malloc/fopen
 *      returns, [ss:label] overrides for DGROUP state, and stack-
 *      resident INT 21h DS:DX buffers reached via DS=DGROUP.  Fixed
 *      via the loader-relocated `_dgroup_para` word + DS=SS brackets.
 *   3. omf_link: the MZ header pointed SS at DGROUP; --separate-stack
 *      points it at the STACK segment itself (SP = stack size).
 *
 * The probe exercises every C-visible shape whose lowering crosses one
 * of those layers, plus pins that stack and DGROUP really are distinct
 * segments (the whole point of the flag) and that _malloc still hands
 * out DGROUP (not SS) far pointers.
 *
 * Build:  tools/build-example.sh --model=compact --split-stack \
 *             minic/dos/examples/split_stack_probe.c
 * Verify: diff <(tools/run-dos-exe.sh \
 *              build/examples/split_stack_probe/split_stack_probe.exe) \
 *              minic/dos/tests/split_stack_probe.golden.txt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

static int g_global = 77;          /* DGROUP anchor for segment compares */

#define SEG_OF(p) ((unsigned)((unsigned long)(void *)(p) >> 16))

/* Callee writes through an escaped stack pointer (far deref of an
 * SS-segment far pointer — the emit.c Oaddr path stamps SS). */
void write_through(int *p, int v) { *p = v; }

/* Stack struct member address through a chain of calls. */
struct pair { int a; int b; };
int *member_addr(struct pair *pp) { return &pp->b; }

/* Fn-ptr callback receiving a stack pointer. */
int call_cb(int (*cb)(int *), int *arg) { return cb(arg); }
int cb_double(int *p) { return *p * 2; }

/* Opaque identity to defeat folding. */
int *iident(int *p) { return p; }

static jmp_buf jb;

int deep(int *flag)
{
	*flag = 41;                /* write through caller-stack ptr */
	longjmp(jb, 7);
	return 0;                  /* not reached */
}

int main(void)
{
	int a = 5;
	int n;
	struct pair pr;
	char buf[40];
	int *mp;
	char *hp;
	unsigned stackseg, dgroupseg, heapseg;

	/* 1. Plain local access (narrowed near deref via [ss:reg]). */
	a = a + 1;
	n = *iident(&a);
	printf("ok1 %d\n", n == 6 ? 1 : 0);

	/* 2. &local escaping to a callee that writes through it. */
	write_through(&a, 42);
	printf("ok2 %d\n", a == 42 ? 1 : 0);

	/* 3. Stack struct member address through a call chain. */
	pr.a = 1; pr.b = 2;
	mp = member_addr(&pr);
	*mp = 33;
	printf("ok3 %d\n", pr.b == 33 ? 1 : 0);

	/* 4. Stack buffer filled by sprintf (_far_sprintf: far dest =
	 *    SS:buf, fmt copied to DGROUP scratch, varargs via ss:) and
	 *    read back through libstub strlen/strcmp (far helpers). */
	sprintf(buf, "v=%d s=%s x=%04x", 42, "str", 0xbeef);
	printf("ok4 %d len=%d\n",
	       strcmp(buf, "v=42 s=str x=beef") == 0 ? 1 : 0,
	       (int)strlen(buf));

	/* 5. Fn-ptr callback receiving a stack ptr. */
	printf("ok5 %d\n", call_cb(cb_double, &a) == 84 ? 1 : 0);

	/* 6. setjmp/longjmp across frames with stack ptrs live. */
	{
		int flag = 0;
		int r = setjmp(jb);
		if (r == 0) {
			deep(&flag);
			printf("ok6 0\n");   /* must not reach */
		} else {
			printf("ok6 %d\n", (r == 7 && flag == 41) ? 1 : 0);
		}
	}

	/* 7. Segment relationships: malloc returns DGROUP (== a global's
	 *    segment, NOT SS), and the stack really is its own segment. */
	hp = malloc(16);
	hp[0] = 'Z';
	stackseg  = SEG_OF(&a);
	dgroupseg = SEG_OF(&g_global);
	heapseg   = SEG_OF(hp);
	printf("ok7 %d\n", (hp != 0 && hp[0] == 'Z'
	                    && heapseg == dgroupseg) ? 1 : 0);
	printf("ok8 %d\n", (stackseg != dgroupseg && stackseg != 0) ? 1 : 0);

	/* 8. puts goes through _far_puts (stack-resident CRLF buffer). */
	puts("ok9");

	printf("done\n");
	return 0;
}
