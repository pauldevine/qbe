/* extern_array_decay_probe.c — extern/multi-decl sized-array declarators
 * must register as ARRAYS, not scalars (§4z).
 *
 * `extern char a[65024];` with a BARE NUMBER dimension does not reduce
 * through the dedicated `EXTERN type IDENT [ expr ] ;` rule — the
 * LR machine routes `IDENT [ NUM ]` through ext_decllist, whose
 * kr_array_node makes an op-B node that the EXTERN list walkers did not
 * handle: the symbol registered as a plain base-type SCALAR (isarray=0).
 * A later reference then LOADED the first byte(s) of the array and passed
 * that as the "pointer" — MicroPython's gc_add(mp_gc_heap2, ...) received
 * seg 0:0 and gc_setup_area zeroed the interrupt vector table (machine
 * wedge between the C2 and C3 boot markers).  A parenthesized dimension
 * `[(65024)]` happened to dodge the bug by reducing through the expr rule.
 *
 * Also pinned here: the same op-B gap in the file-scope NON-extern
 * multi-name decl (`int ga, gb[10];` emitted a wrong-size scalar global
 * for gb), the function-local `extern` list, and sizeof on sized extern
 * arrays (arraybytes was never recorded — sizeof answered pointer-size).
 *
 * Single-TU linkable: uses emit at main's closing brace while the symbols
 * are still in extern state; the real definitions follow at end of file
 * (varadd upgrades extern -> definition).
 */
#include <stdio.h>

extern char xa[40];            /* bare NUM dim — the op-B path (THE bug) */
extern int xb[(8)];            /* parenthesized dim — dedicated expr rule */
extern unsigned xs1, xc[6];    /* multi-name extern with sized array */
int ga, gb[10];                /* non-extern file-scope multi-decl op-B */

static int sumc(char *p, int n)
{
    int s = 0, i;
    for (i = 0; i < n; i++)
        s += p[i];
    return s;
}

static long sumi(int *p, int n)
{
    long s = 0;
    int i;
    for (i = 0; i < n; i++)
        s += p[i];
    return s;
}

int main(void)
{
    int i;
    extern int xd[12];         /* function-local extern, bare NUM dim */

    /* fill through indexing, read back through the DECAYED pointer (with
     * the bug the callee receives a loaded byte, not the address) */
    for (i = 0; i < 40; i++)
        xa[i] = (char)(i + 1);
    printf("xa %d\n", sumc(xa, 40));            /* 820 */

    for (i = 0; i < 8; i++)
        xb[i] = 100 + i;
    printf("xb %ld\n", sumi(xb, 8));            /* 828 */

    for (i = 0; i < 6; i++)
        xc[i] = (unsigned)(7 * i);
    xs1 = 3;
    printf("xc %u %u\n", xs1, xc[5]);           /* 3 35 */

    for (i = 0; i < 12; i++)
        xd[i] = 2 * i;
    printf("xd %ld\n", sumi(xd, 12));           /* 132 */

    for (i = 0; i < 10; i++)
        gb[i] = 1000 + i;
    ga = 5;
    printf("gb %d %ld\n", ga, sumi(gb, 10));    /* 5 10045 */

    /* pointer identity: the decayed name must equal the first element
     * address (with the bug the lhs is a sign-extended data byte) */
    printf("eq %d %d\n",
        (void *)xa == (void *)&xa[0],
        (void *)gb == (void *)&gb[0]);          /* 1 1 */

    /* sizeof on sized extern arrays (arraybytes recorded at decl) */
    printf("sz %d %d %d %d\n",
        (int)sizeof(xa), (int)sizeof(xb),
        (int)sizeof(xc), (int)sizeof(gb));      /* 40 16 12 20 */

    printf("done\n");
    return 0;
}

/* Real definitions — AFTER every use above, so the uses compile against
 * the extern-registered symbol state (the buggy path), and the link still
 * resolves in one TU. */
char xa[40];
int xb[8];
unsigned xs1, xc[6];
int xd[12];
