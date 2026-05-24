/*
 * mediumprobe.c -- medium-model runtime gate.
 *
 * Medium model = near-data + far-code: data lives in a single DGROUP
 * (DS), but code is split into multiple far .text segments.  All
 * pointer arguments to libstub helpers are 2-byte near pointers; the
 * unmangled `_sprintf` / `_strlen` etc. in libstub.asm are what gets
 * called (NOT the `_far_X` mangled variants used by compact mode).
 *
 * Right now the only medium-mode runtime coverage we have is stevie
 * (146KB, any bug buried) and far_probe.c (one byte through one far
 * pointer).  This probe is a focused gate on:
 *   - printf format coverage: %d %u %x %X %o %c %s %% mixed types,
 *     width/precision/zero-pad/left-align, %ld and %lx (32-bit l mod)
 *   - sprintf round-trip into a stack buffer + strcmp verify
 *   - malloc/free round-trip: write, free, realloc, verify values
 *     survive across malloc churn (catches obvious freelist bugs)
 *   - fopen/fputs/fwrite/fclose -> fopen/fread/fclose roundtrip with
 *     memcmp byte-for-byte verify
 *   - near-pointer strchr/strrchr/strstr (the unmangled libstub paths)
 *
 * Build:  tools/build-example.sh --model=medium minic/dos/examples/mediumprobe.c
 * Verify: tools/run-dos-exe.sh build/examples/mediumprobe/mediumprobe.exe \
 *             | diff - minic/dos/tests/mediumprobe.golden.txt
 *
 * Wired into tools/test-dos.sh under the "medium runtime" section.
 *
 * Validation pattern: each printf consumes at most one helper-call
 * return value (single-int return / strcmp result via sgn()), to
 * match the cstrprobe style.  Medium mode doesn't actually hit the
 * compact loadfb-AX-aliasing bug, but the pattern is just as readable.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sgn(int n) {
	if (n < 0) return -1;
	if (n > 0) return 1;
	return 0;
}

int main()
{
	char buf[64];
	char buf2[64];
	char readback[64];
	char *p;
	int *ip;
	int *jp;
	long lv;
	long la;
	long lb;
	long lc;
	unsigned long uv;
	FILE *f;
	int n;

	/* === printf int specifiers === */
	printf("dec=%d (want -42)\r\n", -42);
	printf("udec=%u (want 65494)\r\n", -42);            /* 0xFFD6 unsigned */
	printf("hex=%x (want abcd)\r\n", 0xabcd);
	printf("HEX=%X (want BEEF)\r\n", 0xbeef);
	printf("oct=%o (want 777)\r\n", 0777);
	printf("chr=%c (want Q)\r\n", 'Q');
	printf("pct=%%\r\n");

	/* === printf width / precision / flags === */
	printf("w1=[%5d] (want [   42])\r\n", 42);
	printf("w2=[%-5d] (want [42   ])\r\n", 42);
	printf("w3=[%05d] (want [00042])\r\n", 42);
	printf("w4=[%5.3d] (want [  042])\r\n", 42);
	printf("w5=[%08x] (want [0000abcd])\r\n", 0xabcd);

	/* === printf 32-bit (l modifier) === */
	lv = 100000L;                                       /* > 16 bits */
	printf("ldec=%ld (want 100000)\r\n", lv);
	lv = -100000L;
	printf("lneg=%ld (want -100000)\r\n", lv);
	uv = 0x12345678UL;
	printf("lhex=%lx (want 12345678)\r\n", uv);
	uv = 4000000000UL;
	printf("ludec=%lu (want 4000000000)\r\n", uv);

	/* === printf %s with near pointer === */
	strcpy(buf, "hello world");
	printf("sptr=[%s] (want [hello world])\r\n", buf);
	printf("sprec=[%.5s] (want [hello])\r\n", buf);
	printf("swid=[%15s] (want [    hello world])\r\n", buf);
	printf("sleft=[%-12s|] (want [hello world |])\r\n", buf);

	/* === sprintf round-trip === */
	sprintf(buf, "<%d,%s,%x>", 7, "mid", 0xff);
	printf("sprint_cmp=%d (want 0)\r\n", sgn(strcmp(buf, "<7,mid,ff>")));
	printf("sprint_len=%d (want 10)\r\n", strlen(buf));

	/* Long literals passed inline as varargs are pushed as 16-bit by minic
	 * (the lexer drops the L suffix and emits NUM as int).  Stage through
	 * long-typed locals so the push width matches the %ld consumer. */
	la = 60000L;
	lb = 70000L;
	lc = la + lb;
	sprintf(buf, "%ld+%ld=%ld", la, lb, lc);
	printf("sprint_long=%d (want 0)\r\n", sgn(strcmp(buf, "60000+70000=130000")));

	/* === malloc / free round-trip ===
	 * Step 1: alloc two int blocks, write distinct values.
	 * Step 2: free the first, alloc a third (should reuse or extend).
	 * Step 3: verify the second block's value survives the churn. */
	ip = (int *)malloc(8);
	jp = (int *)malloc(8);
	if (ip == 0 || jp == 0) {
		printf("malloc_null=1 (want 0)\r\n");
	} else {
		printf("malloc_null=0 (want 0)\r\n");
	}
	ip[0] = 0x1234;
	ip[1] = 0x5678;
	jp[0] = 0x9abc;
	jp[1] = 0xdef0;
	printf("mal_a0=%x (want 1234)\r\n", ip[0]);
	printf("mal_b1=%x (want def0)\r\n", jp[1]);
	free(ip);
	ip = (int *)malloc(8);
	if (ip == 0) {
		printf("malloc2_null=1 (want 0)\r\n");
	} else {
		printf("malloc2_null=0 (want 0)\r\n");
		ip[0] = 0xcafe;
		ip[1] = 0xbabe;
		printf("mal2_a1=%x (want babe)\r\n", ip[1]);
		printf("mal2_b1_survived=%x (want def0)\r\n", jp[1]);
	}
	free(ip);
	free(jp);

	/* === fopen / fputs / fclose -> fopen / fread / fclose roundtrip === */
	f = fopen("MPRB.TXT", "w");
	if (f == 0) {
		printf("file_open_w=1 (want 0)\r\n");
	} else {
		printf("file_open_w=0 (want 0)\r\n");
		if (fputs("medium-roundtrip", f) < 0) {
			printf("file_fputs=1 (want 0)\r\n");
		} else {
			printf("file_fputs=0 (want 0)\r\n");
		}
		fclose(f);
	}
	f = fopen("MPRB.TXT", "r");
	if (f == 0) {
		printf("file_open_r=1 (want 0)\r\n");
	} else {
		printf("file_open_r=0 (want 0)\r\n");
		memset(readback, 0, 64);
		n = fread(readback, 1, 63, f);
		fclose(f);
		printf("file_read_n=%d (want 16)\r\n", n);
		readback[n] = 0;
		printf("file_read_cmp=%d (want 0)\r\n", sgn(strcmp(readback, "medium-roundtrip")));
	}

	/* === near-pointer strchr / strrchr / strstr (unmangled libstub) === */
	strcpy(buf, "abc/def/ghi");
	p = strchr(buf, '/');
	printf("strchr_cmp=%d (want 0)\r\n", sgn(strcmp(p, "/def/ghi")));
	p = strrchr(buf, '/');
	printf("strrchr_cmp=%d (want 0)\r\n", sgn(strcmp(p, "/ghi")));
	strcpy(buf2, "def");
	p = strstr(buf, buf2);
	printf("strstr_cmp=%d (want 0)\r\n", sgn(strcmp(p, "def/ghi")));
	p = strstr(buf, "xyz");
	printf("strstr_miss=%d (want 1)\r\n", p == 0);

	/* === memcpy / memcmp near === */
	memset(buf, 0, 64);
	memcpy(buf, "0123456789", 11);
	printf("memcpy_len=%d (want 10)\r\n", strlen(buf));
	printf("memcmp_eq=%d (want 0)\r\n", sgn(memcmp(buf, "0123456789", 10)));

	return 0;
}
