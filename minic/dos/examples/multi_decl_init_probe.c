/* multi_decl_init_probe.c — multi-declarator initializers must run at the
 * declaration point, in control-flow order (§5a).
 *
 * The stmt-context multi-decl rule `type IDENT , ext_decllist ;` emitted
 * each declarator's initializer via a direct expr() call at PARSE time,
 * which lands in the function entry block — so `size_t k, nf = 0;` inside
 * a loop body initialized nf ONCE at function entry instead of per
 * iteration, and the variable silently accumulated across iterations
 * (this corrupted §4z's own per-area debug counter: 1539-of-999).  The
 * single-declarator form was fixed long ago ([[minic-decl-init-hoisting]]);
 * the list form was not.  Worse, a hoisted init EXPRESSION reads its
 * operands before they are live: `int k, *p = &g[i];` computed &g[i] at
 * entry with i uninitialized.
 *
 * Also pinned here:
 *   - `int a = 1, b = 2;` inside a block (first declarator carries the
 *     init) was a PARSE ERROR — there was no stmt-level
 *     `type IDENT = expr , init_decllist ;` rule at all;
 *   - the dcls-context decorated-first-declarator form
 *     (`int a[5], b = 3;` at function top) went through
 *     emit_local_multi_decl_full, which silently DROPPED the init —
 *     b read uninitialized stack memory;
 *   - an initializer with a side effect declared in a never-taken branch
 *     must not run (the hoisted form ran it unconditionally at entry).
 */
#include <stdio.h>

int g[4];
int side_calls = 0;

int bump(void)
{
    side_calls++;
    return 7;
}

int never = 0;

int main(void)
{
    int a[5], b = 3;        /* dcls _full path: init used to be DROPPED */
    int i;

    printf("b %d\n", b);                     /* 3 */

    /* loop-body re-init: nf must restart at 0 every iteration */
    for (i = 0; i < 3; i++) {
        int k, nf = 0;
        nf = nf + i;
        printf("nf %d\n", nf);               /* 0 1 2 (hoisted: 0 1 3) */
        k = nf;
        a[i] = k;
    }
    printf("a2 %d\n", a[2]);                 /* 2 */

    /* hoisted init expr reads operands before they are live */
    for (i = 0; i < 4; i++) {
        int k, *p = &g[i];
        *p = i * 10 + 1;
        k = *p;
        if (i == 3)
            printf("p3 %d\n", k);            /* 31 */
    }
    printf("g0 %d g3 %d\n", g[0], g[3]);     /* 1 31 */

    /* first-declarator init inside a block: was a parse error */
    for (i = 0; i < 3; i++) {
        int x = 5, y = 0, z;
        y = y + x + i;
        z = y;
        printf("y %d\n", z);                 /* 5 6 7 */
    }

    /* side-effecting init in a never-taken branch must not run */
    if (never) {
        int q, r = bump();
        printf("r %d\n", r);
    }
    printf("side %d\n", side_calls);         /* 0 */

    /* taken branch: side effect runs exactly once, at the decl point */
    if (!never) {
        int q, r = bump();
        q = r;
        printf("q %d side %d\n", q, side_calls);  /* 7 1 */
    }

    /* pointer-decorated later declarator with init, in a loop */
    for (i = 0; i < 2; i++) {
        int k, *pp = &g[i + 1];
        k = *pp + 100;
        printf("pk %d\n", k);                /* 111 121 */
    }

    printf("DONE\n");
    return 0;
}
