/* multi_decl_shadow_probe.c — block-scope shadowing through a
 * MULTI-declarator local declaration (§7b).
 *
 * local_shadow_probe.c pinned the SINGLE-declarator case: a block-scope
 * local named like a global/function/enum is alpha-renamed by
 * block_scope_decl() instead of dying "double definition".  But the
 * multi-declarator helpers (emit_local_multi_decl / _full) called varadd
 * DIRECTLY, bypassing block_scope_decl — so EVERY declarator in
 * `T a, b, c;` (the first included) skipped the rename and a comma-list
 * local shadowing a global/function/enum/different-typed outer local
 * still died "double definition".  The fix routes each storage-allocating
 * declarator through block_scope_decl, including the first item and the
 * `int a = 1, b = 2;` first-has-init form.
 *
 * Each case below was a hard "double definition" before the fix; after it,
 * the inner names are renamed (name$N) with uses stamped to the renamed
 * slot, and the shadowed outer/global/function/enum is untouched.
 *
 * Frontend-only / model-agnostic: gated small + medium (like
 * local_shadow_probe.c).
 */
#include <stdio.h>

int counter = 100;          /* shadowed by a same-typed multi-decl item */
int gflag = 7;              /* shadowed by a different-typed item */

int fat_mount(int dev)      /* shadowed by a multi-decl item */
{
	return dev * 2;
}

enum { RED = 77 };          /* shadowed by a multi-decl item */

/* (a) first item shadows a same-typed global, a later item shadows a
 * different-typed global, another shadows a function, another an enum. */
static int multi_shadow(void)
{
	int counter, x;         /* counter shadows global int counter */
	char gflag, fat_mount;  /* gflag (char) shadows int gflag; fat_mount fn */
	long RED;               /* shadows enum constant RED */
	counter = 5;
	x = 1;
	gflag = 3;
	fat_mount = 4;
	RED = 9;
	return counter + x + gflag + fat_mount + (int)RED;  /* 22 */
}

int main(void)
{
	long total;

	printf("%d\n", multi_shadow());     /* 22 */

	/* globals/function/enum must be intact afterwards */
	printf("%d\n", counter);            /* 100 */
	printf("%d\n", gflag);              /* 7 */
	printf("%d\n", fat_mount(21));      /* 42 */
	printf("%d\n", RED);                /* 77 */

	/* (b) inner-block multi-decl shadows a DIFFERENT-typed outer local;
	 * the outer must survive the block (deferred rename-pop). */
	total = 0;
	{
		long v = 100;
		{
			char v, w;          /* both shadow the outer long v */
			v = 5; w = 6;
			total = v + w;      /* 11 */
		}
		total += v;             /* outer v still 100 -> 111 */
	}
	printf("b=%ld\n", total);           /* 111 */

	/* (c) first-declarator-has-init form `T a = .., b = ..;` where a
	 * later item shadows a global (this rule also bypassed rename). */
	{
		int gflag = 2, q = 3;   /* gflag shadows int gflag */
		total = gflag + q;      /* 5 */
	}
	printf("c=%ld\n", total);           /* 5 */
	printf("%d\n", gflag);              /* 7 (global untouched) */

	/* (d) pointer-decorated multi-decl item shadowing a global (the 'P'
	 * path), uses stamped across a deref. */
	{
		int *counter, n;        /* counter (int*) shadows global int counter */
		n = 66;
		counter = &n;
		total = *counter;       /* 66 */
	}
	printf("d=%ld\n", total);           /* 66 */
	printf("%d\n", counter);            /* 100 (global untouched) */

	return 0;
}
