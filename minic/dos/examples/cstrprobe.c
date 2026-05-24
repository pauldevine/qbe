/*
 * cstrprobe.c — compact-model str/mem far helpers probe.
 *
 * Exhaustively exercises minic's compact-mode call-target mangling
 * (strlen → _far_strlen, etc.) against the far variants in
 * minic/dos/libstub.asm and _far_sprintf in tools/libstub_to_exe.py.
 *
 * Every assertion prints a `name=<result> (want <expected>)` line so the
 * output can be diff'd verbatim against minic/dos/tests/cstrprobe.golden.txt.
 *
 * Build:  tools/build-example.sh --model=compact minic/dos/examples/cstrprobe.c
 * Verify: tools/run-dos-exe.sh build/examples/cstrprobe/cstrprobe.exe \
 *             | diff - minic/dos/tests/cstrprobe.golden.txt
 *
 * Wired into tools/test-dos.sh under the "compact runtime" section.
 *
 * Strings used in tests are deliberately a mix of DGROUP string literals
 * (segment = data segment) and stack-local char arrays (segment = SS,
 * which omf_link makes coincident with DGROUP but the helpers don't
 * assume that — they faithfully load ES/DS from the far ptr seg word).
 *
 * Validation strategy: we use *one* helper-call return value per printf
 * line.  Two consecutive loadfb's of byte indices off the same buffer
 * (e.g. `printf("%d %d", dst[0], dst[1])`) trigger a pre-existing QBE
 * compact-mode register-allocation bug — both loadfb's write to AX
 * without preservation, so the first value is clobbered.  See
 * [[i8086-compact-loadfb-aliases-ax]].  Coarse-grained validators
 * (strcmp / strlen / memcmp returns) sidestep the bug entirely.
 */

#include <stdio.h>
#include <string.h>

/* Collapse a strcmp/memcmp return to -1/0/+1 for stable diff. */
static int sgn(int n) {
	if (n < 0) return -1;
	if (n > 0) return 1;
	return 0;
}

int main()
{
	char buf[64];
	char tmp[16];
	char dst[16];
	char *lit;
	char *p;
	char *pp;

	lit = "hello";

	/* === printf %p ===
	 * Run this BEFORE any strchr/strrchr/strstr call so minic's lvalue
	 * analysis hasn't yet inferred a return type for those (which
	 * currently rejects the subsequent `(char *)<long>` cast). */
	pp = (char *)0x12345678L;
	printf("pct_p=%p (want 12345678)\r\n", pp);

	/* === _far_strlen === */
	printf("strlen_lit=%d (want 5)\r\n", strlen(lit));
	strcpy(buf, "world");
	printf("strlen_stack=%d (want 5)\r\n", strlen(buf));
	printf("strlen_empty=%d (want 0)\r\n", strlen(""));

	/* === _far_strcpy === */
	strcpy(buf, "ABCDE");
	printf("strcpy_cmp=%d (want 0)\r\n", sgn(strcmp(buf, "ABCDE")));
	printf("strcpy_len=%d (want 5)\r\n", strlen(buf));

	/* === _far_strcmp === */
	printf("strcmp_eq=%d (want 0)\r\n", sgn(strcmp("abc", "abc")));
	printf("strcmp_lt=%d (want -1)\r\n", sgn(strcmp("abc", "abd")));
	printf("strcmp_gt=%d (want 1)\r\n", sgn(strcmp("abd", "abc")));
	printf("strcmp_short=%d (want -1)\r\n", sgn(strcmp("ab", "abc")));

	/* === _far_strncmp === */
	printf("strncmp_eq3=%d (want 0)\r\n", sgn(strncmp("abcXX", "abcYY", 3)));
	printf("strncmp_lt5=%d (want -1)\r\n", sgn(strncmp("abcXX", "abcYY", 5)));
	printf("strncmp_eq0=%d (want 0)\r\n", sgn(strncmp("a", "b", 0)));

	/* === _far_strncpy ===
	 * Pre-fill with 'Q', NUL-terminate at end so strlen is bounded.
	 * Case A (n < src_len): strncpy(dst,"abc",3) writes only 3 bytes,
	 *   leaves dst[3..14] as 'Q' and dst[15] as our sentinel NUL.
	 *   strlen finds NUL at byte 15.
	 * Case B (n > src_len): strncpy(dst,"abc",6) writes "abc" then
	 *   pads dst[3..5] with NUL.  strlen finds NUL at byte 3. */
	memset(dst, 81, 15); dst[15] = 0;       /* 'Q'*15 + NUL */
	strncpy(dst, "abc", 3);
	printf("strncpy_nopad_len=%d (want 15)\r\n", strlen(dst));
	printf("strncpy_nopad_pfx=%d (want 0)\r\n", sgn(strncmp(dst, "abc", 3)));

	memset(dst, 81, 15); dst[15] = 0;
	strncpy(dst, "abc", 6);
	printf("strncpy_pad_len=%d (want 3)\r\n", strlen(dst));
	printf("strncpy_pad_cmp=%d (want 0)\r\n", sgn(strcmp(dst, "abc")));

	/* === _far_strchr ===
	 * For found: returned ptr starts at 'l' so strlen("llo") = 3.
	 * For miss: returned ptr is NULL — test via cmp against 0 string
	 *   would deref NULL.  Use the pointer-vs-NULL comparison and let
	 *   minic emit the `cmp` directly.  Result is 1 (true) for NULL. */
	p = strchr("hello", 'l');
	printf("strchr_found_len=%d (want 3)\r\n", strlen(p));
	printf("strchr_found_cmp=%d (want 0)\r\n", sgn(strcmp(p, "llo")));
	p = strchr("hello", 'z');
	printf("strchr_miss_null=%d (want 1)\r\n", p == 0);

	/* === _far_strrchr === */
	p = strrchr("abcabc", 'b');
	printf("strrchr_last_len=%d (want 2)\r\n", strlen(p));
	printf("strrchr_last_cmp=%d (want 0)\r\n", sgn(strcmp(p, "bc")));
	p = strrchr("hello", 'z');
	printf("strrchr_miss_null=%d (want 1)\r\n", p == 0);

	/* === _far_strcat === */
	strcpy(buf, "foo");
	strcat(buf, "bar");
	printf("strcat_len=%d (want 6)\r\n", strlen(buf));
	printf("strcat_cmp=%d (want 0)\r\n", sgn(strcmp(buf, "foobar")));

	/* === _far_strcspn ===
	 * strcspn("abcdef","xyzc"): "ab" don't intersect; 'c' does → returns 2. */
	printf("strcspn_2=%d (want 2)\r\n", strcspn("abcdef", "xyzc"));
	printf("strcspn_none=%d (want 5)\r\n", strcspn("hello", "xyz"));
	printf("strcspn_first=%d (want 0)\r\n", strcspn("abcdef", "a"));

	/* === _far_strstr === */
	p = strstr("the quick brown fox", "quick");
	printf("strstr_found_len=%d (want 15)\r\n", strlen(p));
	printf("strstr_found_cmp=%d (want 0)\r\n", sgn(strcmp(p, "quick brown fox")));
	p = strstr("hello world", "xyz");
	printf("strstr_miss_null=%d (want 1)\r\n", p == 0);
	p = strstr("hello", "");
	printf("strstr_empty_len=%d (want 5)\r\n", strlen(p));

	/* === _far_memcpy === */
	memset(dst, 0, 16);
	memcpy(dst, "abcdef", 7);              /* incl. terminating NUL */
	printf("memcpy_len=%d (want 6)\r\n", strlen(dst));
	printf("memcpy_cmp=%d (want 0)\r\n", sgn(strcmp(dst, "abcdef")));

	/* === _far_memcmp === */
	printf("memcmp_eq=%d (want 0)\r\n", sgn(memcmp("abcdef", "abcdef", 6)));
	printf("memcmp_lt=%d (want -1)\r\n", sgn(memcmp("abcdef", "abcdeg", 6)));
	printf("memcmp_gt=%d (want 1)\r\n", sgn(memcmp("abcdeg", "abcdef", 6)));
	printf("memcmp_eq0=%d (want 0)\r\n", sgn(memcmp("a", "b", 0)));

	/* === _far_memset === */
	memset(tmp, 'X', 5);
	tmp[5] = 0;
	printf("memset_len=%d (want 5)\r\n", strlen(tmp));
	printf("memset_cmp=%d (want 0)\r\n", sgn(strcmp(tmp, "XXXXX")));
	memset(tmp, 0x41, 3);
	tmp[3] = 0;
	printf("memset_byte_cmp=%d (want 0)\r\n", sgn(strcmp(tmp, "AAA")));

	/* === printf %s extra cases === */
	printf("pct_s_pad=[%10s] (want [     hello])\r\n", lit);
	printf("pct_s_left=[%-6s] (want [hello ])\r\n", lit);
	printf("pct_s_prec=[%.3s] (want [hel])\r\n", lit);
	strcpy(buf, "world");
	printf("pct_s_stack=%s (want world)\r\n", buf);

	return 0;
}
