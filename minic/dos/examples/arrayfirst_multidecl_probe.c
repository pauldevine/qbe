/* arrayfirst_multidecl_probe.c — statement-context multi-declarator whose
 * FIRST declarator is a sized array (§7c).
 *
 * The dcls-context (function-top) grammar already accepted
 * `int arr[3], *counter;` via kr_array_node + emit_local_multi_decl_full,
 * but the STATEMENT-context multi-decl rule only matched a bare leading
 * IDENT (`type IDENT ',' ext_decllist`), so an array-first multi-decl that
 * appeared MID-BLOCK (after a statement) hit `parse error`.  pointer-first
 * `int *p, n;` and follow-item `int n, *p;` both already parsed; only the
 * array-FIRST form was missing.  The fix adds the stmt-context production
 * `type IDENT '[' expr ']' ',' ext_decllist ';'` mirroring the dcls rule,
 * deferring the initializer chain as an Expr stmt (control-flow order).
 *
 * Every case below was `parse error` before the fix (the declarations sit
 * mid-block, after an executable statement, so they cannot fall into the
 * dcls prologue).  Frontend-only / model-agnostic: gated small + medium
 * (like local_shadow_probe.c / multi_decl_shadow_probe.c).
 */
#include <stdio.h>

int counter = 100;          /* shadowed by an array-first multi-decl item */

int main(void)
{
	int total;

	total = 0;

	/* (a) plain array-first multi-decl, mid-block: array + pointer-second. */
	{
		int touch;
		touch = 9;                  /* force statement context */
		int arr[3], *p;             /* array-FIRST, pointer follows */
		arr[0] = 1; arr[1] = 2; arr[2] = 3;
		p = arr;
		total = p[0] + p[1] + p[2] + touch;   /* 1+2+3+9 = 15 */
	}
	printf("a=%d\n", total);            /* 15 */

	/* (b) array-first item shadows a same-named global (the 'B' path now
	 * routes through block_scope_decl too); a later scalar item carries an
	 * initializer that must run in control-flow order. */
	{
		int seen;
		seen = 1;                   /* force statement context */
		int counter[2], n = 7;      /* counter[] shadows global int counter */
		counter[0] = 40; counter[1] = 50;
		total = counter[0] + counter[1] + n + seen;   /* 40+50+7+1 = 98 */
	}
	printf("b=%d\n", total);            /* 98 */
	printf("%d\n", counter);            /* 100 (global untouched) */

	/* (c) array-first followed by a deref of an initialized pointer item. */
	{
		int probe;
		probe = 4;                  /* force statement context */
		int vals[2], *q = vals;     /* pointer-second has an initializer */
		vals[0] = 11; vals[1] = 22;
		total = q[0] + q[1] + *q + probe;   /* 11+22+11+4 = 48 */
	}
	printf("c=%d\n", total);            /* 48 */

	return 0;
}
