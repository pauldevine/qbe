/*
 * extern_array_expr_probe.c -- extern array declaration whose dimension is
 * a constant EXPRESSION, not a plain integer literal:
 *     extern char buf[(32) + 1];
 * Needed by extmod/network_ppp_lwip.c:
 *     extern char mod_network_hostname_data[(MICROPY_PY_NETWORK_HOSTNAME_MAX_LEN) + 1];
 * The plain-literal form `extern char buf[2];` already parsed; the
 * expression form parse-errored because the `EXTERN type IDENT '[' NUM ']'`
 * rule only accepted a NUM.  It now accepts `'[' expr ']'` (the folded size
 * is discarded -- an extern allocates no storage here).
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/extern_array_expr_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/extern_array_expr_probe/extern_array_expr_probe.exe \
 *             | diff - minic/dos/tests/extern_array_expr_probe.golden.txt
 *
 * Frontend-only / model-agnostic: runs under medium + large.
 */

#include <stdio.h>

/* Forward extern declarations using constant-expression dimensions: a
 * parenthesised literal plus an offset, and a product.  These are the new
 * grammar path; the same arrays are defined below. */
extern char gbuf[(4) + 1];
extern int gints[(2) * 3];

char gbuf[(4) + 1] = {'a', 'b', 'c', 'd', 0};
int gints[(2) * 3] = {10, 20, 30, 40, 50, 60};

int
main(void)
{
	int sum = 0;
	int i;

	/* (a) the char array's declared dimension is (4)+1 = 5: 4 chars + NUL. */
	printf("a=%s\r\n", gbuf);                       /* abcd */

	/* (b) the int array's dimension is (2)*3 = 6 elements; sum them. */
	for (i = 0; i < 6; i++)
		sum += gints[i];
	printf("b=%d\r\n", sum);                        /* 210 */

	return 0;
}
