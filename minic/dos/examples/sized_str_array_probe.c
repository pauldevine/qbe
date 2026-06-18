/* sized_str_array_probe.c — file-scope sized char array with a string-literal
 * initializer: `char NAME[N] = "string";` (§8v).
 *
 * The file-scope declaration grammar had `'[' ']' '=' STR ';'` (the UNSIZED
 * `char a[] = "x"` form) but NO `'[' expr ']' '=' STR ';'` sibling, so an
 * EXPLICIT-dimension char array initialized from a string literal —
 * `static char cwd[64] = "/";`, the form newlibc's tests/vshell.c uses for its
 * path buffers — hit `parse error`.  (The brace form `int a[3] = {1,2,3}` and
 * the unsized string form `char a[] = "x"` both already parsed; only sized +
 * string-literal was missing.)  The fix adds the production wired to a new
 * emit_string_array_sized() that lays the literal's bytes (incl. NUL) at the
 * front and zero-fills the remaining N*sizeof(T)-natural bytes.
 *
 * Every sized declaration below was `parse error` before the fix.  Frontend
 * grammar / data-layout only and model-agnostic (a string-array is near data
 * in DGROUP for every model); gated small + medium + compact + large + huge,
 * one golden.  Bug-loud: on the unfixed compiler the file does not parse, so
 * the build fails outright.
 */
#include <stdio.h>
#include <string.h>

/* The headline vshell form: sized buffer, room to spare, zero-filled tail. */
static char cwd[64] = "/";
/* Plenty of slack — exercises a large zero-fill. */
char greeting[16] = "hi";
/* Exact fit including the NUL (pad == 0 — reuses the literal block verbatim). */
char exactfit[3] = "ab";
/* Empty initializer into a sized array (one NUL + zero-fill). */
char emptied[8] = "";

int
main(void)
{
	int i, tail_zero, slack_zero;

	/* Contents are the initializer string, NUL-terminated. */
	printf("cwd=%s len=%d\n", cwd, (int)strlen(cwd));            /* / 1 */
	printf("greeting=%s len=%d\n", greeting, (int)strlen(greeting)); /* hi 2 */
	printf("exactfit=%s len=%d\n", exactfit, (int)strlen(exactfit)); /* ab 2 */
	printf("emptied=[%s] len=%d\n", emptied, (int)strlen(emptied));  /* [] 0 */

	/* The declared size is real storage: the byte right after the string is
	 * the NUL, and the slack beyond that is zero-filled. */
	tail_zero = (greeting[2] == 0);              /* the NUL */
	slack_zero = 1;
	for (i = 3; i < 16; i++)
		if (greeting[i] != 0)
			slack_zero = 0;
	printf("tailzero=%d slackzero=%d\n", tail_zero, slack_zero); /* 1 1 */

	/* The full N bytes are writable storage, not just the string. */
	cwd[30] = 'X';
	cwd[31] = 0;
	printf("cwd[30]=%c off=%d\n", cwd[30], 30);  /* X 30 */

	/* Rewrite the whole buffer to prove it holds N bytes. */
	for (i = 0; i < 63; i++)
		cwd[i] = 'a' + (i % 26);
	cwd[63] = 0;
	printf("cwd_rewrite_len=%d\n", (int)strlen(cwd));  /* 63 */

	printf("sized_str_array_probe done\n");
	return 0;
}
