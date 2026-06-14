/* param_static_shadow_probe.c — §7d: function parameters and static locals
 * shadow file-scope bindings.
 *
 * Before the fix, block_scope_decl's alpha-rename covered ordinary block
 * locals (§6a) and multi-declarator locals (§7b/§7c), but param() and
 * emit_static_local() called varadd() DIRECTLY, so a parameter or a static
 * local whose name collided with a global / function / enum / different-typed
 * outer binding died with "double definition".  Fixed by routing both through
 * the renamer (block_scope_rename): a colliding name is alpha-renamed
 * (`name$N`) and a rename is registered so uses in the body resolve to the
 * shadowing slot; the shadowed file-scope binding is untouched outside.
 */
#include <stdio.h>

int count = 100;       /* shadowed by params and a static local below */
char tag = 'G';        /* a DIFFERENT-typed global, shadowed by a param */

int helper(int dev)
{
	return dev * 2;
}

enum { LIMIT = 55 };

/* (a) parameter shadows a same-typed global: inside, `count` is the param. */
int addone(int count)
{
	return count + 1;
}

/* (b) parameter shadows a different-typed global (`char tag`); the param is
 *     an int.  Also a param shadowing a function name and an enum constant. */
int mix(int tag, int helper, int LIMIT)
{
	return tag + helper + LIMIT;
}

/* (c) pointer parameter shadows a global, used across a deref. */
int viaptr(int *count)
{
	*count = *count + 7;
	return *count;
}

/* (d) static local shadows the same-named global; persists across calls and
 *     is independent of the global. */
int counter_static(void)
{
	static int count = 10;   /* shadows the global `count` */
	count = count + 1;
	return count;
}

/* (e) a param shadow plus an inner-block local re-shadow of the same name:
 *     proves rename depth/pop — the inner block uses its own slot, then the
 *     param slot is visible again after the block closes. */
int nested(int count)
{
	int inner;
	{
		int count = 1000;   /* re-shadows the param inside this block */
		inner = count;      /* 1000 */
	}
	return inner + count;   /* count here is the PARAM again */
}

int main(void)
{
	int n = 3;

	printf("%d\n", addone(count));   /* count(global)=100 -> 101 */
	printf("%d\n", count);           /* global untouched: 100 */
	printf("%d\n", mix(1, 2, 3));    /* 1+2+3 = 6 */
	printf("%d\n", helper(20));      /* function intact: 40 */
	printf("%d\n", LIMIT);           /* enum intact: 55 */
	printf("%d\n", viaptr(&n));      /* 3+7 = 10 */
	printf("%d\n", n);               /* arg mutated through ptr: 10 */
	printf("%d\n", counter_static()); /* 11 */
	printf("%d\n", counter_static()); /* 12 (static persists) */
	printf("%d\n", count);           /* global still 100 */
	printf("%d\n", nested(5));       /* 1000 + 5 = 1005 */
	printf("%d\n", tag);             /* different-typed global intact: 71 ('G') */
	return 0;
}
