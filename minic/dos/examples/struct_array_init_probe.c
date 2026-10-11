/*
 * struct_array_init_probe.c — bug-loud gate for three block-scope
 * initializer gaps the gcc.dg runtime sweep (tools/gccdg) surfaced, all
 * three of which CRASHED the compiler (so on the unfixed minic this probe
 * does not build at all):
 *
 *  1. A local array of structs with braced elements,
 *     `struct pt p[] = {{1,2},{3,4}};` — the four local array brace-init
 *     rules passed each `{…}` item to expr() as a bare '{' node, which hit
 *     expr()'s `case 0: abort()`  (gcc.dg torture/pr101009, pr36584).
 *     FIX: a braced element lowers to `p[i] = (struct pt){…}`, a compound
 *     literal through the ordinary struct-copy path; a sized array of
 *     structs is zero-filled whole first (partial-init semantics).
 *  2. A nested array MEMBER inside a struct initializer,
 *     `poly q = {2, {10, 20}};` — the compound-literal member filler
 *     accepted a nested `{…}` only for a sub-struct ("braced initializer
 *     for non-aggregate member").  FIX: emit_clit_array fills the member
 *     element by element (nested struct elements recurse).
 *  4. A string literal initializing a char-ARRAY member, `{.s = "12345", .x = 2}`
 *     — the compound-literal member filler stored the literal's ADDRESS
 *     into the first byte (gcc.dg pr52769, array-init-2).  FIX: the bytes
 *     are copied in (slack stays zero).
 *  3. `char big[600] = "edge";` — the deferred per-byte store chain was a
 *     LEFT-nested comma tree, so expr() recursed once per byte and
 *     overflowed the C stack at ~400 bytes (gcc.dg pr38902, 1024-byte
 *     buffers).  FIX: the chains are balanced trees (log2 depth).
 *
 * Golden = plain C semantics (verified against a native cc run), model
 * independent.  Covers function-top and mid-block (deferred) contexts,
 * sized (zero-filled tail) and unsized arrays, and block re-entry.
 */
#include <stdio.h>

struct pt { int x; int y; };
typedef struct { int ord; long coef[4]; } poly;
struct pts2 { struct pt a[2]; int tag; };
struct named { char s[6]; int x; };

static void print_poly(const char *label, poly *p)
{
	printf("%s ord=%d coef=%ld,%ld,%ld,%ld\n", label, p->ord,
	       p->coef[0], p->coef[1], p->coef[2], p->coef[3]);
}

static int sum_block(int round)
{
	int s = 0;
	if (round >= 0) {
		/* mid-block (deferred) inits: must re-run on every entry */
		struct pt loc[3] = {{round, 1}, {2, 3}};
		struct pt un[] = {{7, 8}, {9, 10}};
		char buf[500] = "re";
		int i;
		for (i = 0; i < 3; i++)
			s += loc[i].x * 100 + loc[i].y;
		s += un[1].x + un[1].y;
		s += buf[0] + buf[1] + buf[2] + buf[499];
		loc[0].x = 999;               /* must not survive to the next entry */
	}
	return s;
}

int main(void)
{
	/* 1. function-top array-of-struct, sized (tail zero-filled) + unsized */
	struct pt pts[3] = {{1, 2}, {3, 4}};
	struct pt q[] = {{5, 6}, {7, 8}, {9, 10}};
	/* 2. nested array member, single struct + array of such structs */
	poly p = {2, {10, 20, 30}};
	poly ps[2] = {{1, {1, 2}}, {3, {4, 5, 6, 7}}};
	struct pts2 nest[2] = {{{{1, 1}, {2, 2}}, 5}, {{{3, 3}}, 6}};
	/* 4. string literal into a char-array member (designated + positional) */
	struct named nm = {.s = "12345", .x = 2};
	struct named nma[2] = {{"ab", 7}, {"", 8}};
	/* 3. big string init: balanced chain, slack zero-filled */
	char big[600] = "edge";
	int i;

	for (i = 0; i < 3; i++)
		printf("pts[%d]=%d,%d\n", i, pts[i].x, pts[i].y);
	printf("q count=%d last=%d,%d\n", (int)(sizeof(q) / sizeof(q[0])), q[2].x, q[2].y);
	print_poly("p", &p);
	print_poly("ps[0]", &ps[0]);
	print_poly("ps[1]", &ps[1]);
	p = (poly){9, {7}};
	print_poly("clit", &p);
	printf("nest[0]=%d,%d/%d,%d tag=%d nest[1]=%d,%d/%d,%d tag=%d\n",
	       nest[0].a[0].x, nest[0].a[0].y, nest[0].a[1].x, nest[0].a[1].y, nest[0].tag,
	       nest[1].a[0].x, nest[1].a[0].y, nest[1].a[1].x, nest[1].a[1].y, nest[1].tag);
	printf("big=%s size=%d big[4]=%d big[599]=%d\n", big, (int)sizeof(big), big[4], big[599]);
	printf("named s=%s s[5]=%d x=%d | %s/%d %d/%d\n", nm.s, nm.s[5], nm.x, nma[0].s, nma[0].x, nma[1].s[0], nma[1].x);
	printf("block r0=%d r1=%d r0again=%d\n", sum_block(0), sum_block(1), sum_block(0));
	printf("struct_array_init_probe done\n");
	return 0;
}
