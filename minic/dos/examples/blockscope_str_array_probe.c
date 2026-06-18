/* blockscope_str_array_probe.c — BLOCK-scope char array with a string-literal
 * initializer: `char NAME[] = "s";` / `char NAME[N] = "s";` /
 * `static char NAME[N] = "s";` inside a function body (§8x).
 *
 * §8v closed the FILE-scope sized form `char NAME[N] = "string";`.  Inside a
 * function body the grammar still rejected every string-array init except the
 * static-unsized one: `char a[] = "x"`, `char a[N] = "x"`, and
 * `static char a[N] = "x"` were all `parse error`, in BOTH the function-top
 * (dcls) and mid-block (statement) contexts.  §8x adds the six missing
 * productions: the non-static forms allocate a stack array and initialize it
 * at runtime (decoded literal bytes then a zero-filled tail, a deferred store
 * chain so it re-runs on each block re-entry); the static-sized form lays a
 * mangled file-scope data block exactly like §8v's file-scope path.
 *
 * Every declaration below was `parse error` before §8x, so the probe is
 * bug-loud at the parse stage (the build fails outright on the unfixed
 * compiler).  Frontend grammar / data-layout only and model-agnostic; gated
 * small + medium + compact + large + huge against one golden.
 *
 * The headline correctness property a string-array stack init must satisfy —
 * and which a naive "store it in a shared global" mis-implementation would
 * violate — is RE-ENTRANCY: a non-static local re-initializes on every call,
 * while a static local initializes once and persists.  fresh_each_call() and
 * persist_across_calls() pin both halves.
 */
#include <stdio.h>
#include <string.h>

/* A NON-static local must be re-initialized on every entry: mutate it, return,
 * call twice — the second call must still see the fresh initializer. */
static int
fresh_each_call(void)
{
	char buf[6] = "ab";          /* function-top (dcls) sized stack array */
	int was_a = (buf[0] == 'a'); /* fresh 'a' at entry every call */
	buf[0] = 'Z';                /* mutation must NOT leak to the next call */
	return was_a;
}

/* A static local initializes ONCE and persists across calls. */
static int
persist_across_calls(void)
{
	static char s[8] = "x";      /* function-top (dcls) static sized */
	int len = (int)strlen(s);
	s[len] = 'y';                /* append; persists into the next call */
	s[len + 1] = 0;
	return (int)strlen(s);
}

int
main(void)
{
	/* function-top (dcls) context */
	char top_unsized[] = "hi";
	char top_sized[8] = "cd";
	char esc[6] = "\t\x41";      /* 9, 'A'(65), then zero-fill */
	int i, slack_ok, r1, r2, p1, p2;

	printf("top_unsized=%s len=%d\n", top_unsized, (int)strlen(top_unsized));
	printf("top_sized=%s len=%d\n", top_sized, (int)strlen(top_sized));

	/* statement-scope (mid-block) context — these follow the statements above */
	char mid_unsized[] = "mid";
	char mid_sized[8] = "ef";
	static char mid_static[8] = "gh";

	printf("mid_unsized=%s len=%d\n", mid_unsized, (int)strlen(mid_unsized));
	printf("mid_sized=%s len=%d\n", mid_sized, (int)strlen(mid_sized));
	printf("mid_static=%s len=%d\n", mid_static, (int)strlen(mid_static));

	/* escape decode + zero-filled slack of a sized array */
	printf("esc0=%d esc1=%d esc2=%d\n", esc[0], esc[1], esc[2]);
	slack_ok = 1;
	for (i = 2; i < 8; i++)
		if (top_sized[i] != 0)
			slack_ok = 0;
	printf("top_sized_slack=%d\n", slack_ok);

	/* re-entrancy: non-static re-inits each call; static persists */
	r1 = fresh_each_call();
	r2 = fresh_each_call();
	printf("fresh=%d %d\n", r1, r2);

	p1 = persist_across_calls();
	p2 = persist_across_calls();
	printf("persist=%d %d\n", p1, p2);

	printf("blockscope_str_array_probe done\n");
	return 0;
}
