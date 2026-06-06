/*
 * farglobal_probe.c — exercises DIRECT global access (scalar, struct
 * member, pointer, struct copy, ++ ) under far-data models WHEN the
 * statics are relocated to their own far segment (built with
 * QBE_FAR_STATIC_DATA=1, so each global sits at offset 0 of a FAR_DATA
 * segment outside DGROUP — exactly the placement MicroPython needs).
 *
 * This is the FARSTORAGE feature ([[minic-far-data-segment]]): minic
 * used to emit NEAR load/store for direct global access (`g`, `g.m`,
 * `g = x`, `g++`, `g1 = g2` struct copy), which only worked because
 * globals lived in DGROUP (=DS).  Once relocated to a far segment, a
 * near access reads/writes the WRONG segment (DS, not the global's
 * segment) → garbage.  The FARSTORAGE predicate routes all direct
 * global access through loadfX/storefX (and 4-byte address arithmetic
 * for member offsets), the same far path array-subscript already used.
 *
 * Array subscript (`arr[i]`) already went far before FARSTORAGE, so a
 * pure-array probe (fardata_probe.c) does NOT cover this — the cases
 * here are specifically the DIRECT (non-subscript) access shapes.
 *
 * Build:  QBE_FAR_STATIC_DATA=1 tools/build-example.sh --model=compact \
 *             minic/dos/examples/farglobal_probe.c
 * Verify: diff <(tools/run-dos-exe.sh \
 *              build/examples/farglobal_probe/farglobal_probe.exe) \
 *              minic/dos/tests/farglobal_probe.golden.txt
 */

#include <stdio.h>

struct point { int x; int y; long tag; };

static int          g_i;        /* direct scalar */
static unsigned     g_u;
static long         g_l;        /* 4-byte scalar through far store/load */
static struct point g_pt;       /* member access on a far global */
static struct point g_pt2;      /* struct-copy destination */
static int          g_arr[4];   /* take &g_arr[k] -> far pointer into segment */
static int         *g_ptr;      /* a POINTER global: its 4-byte value lives far */

int main(void)
{
	int local;
	int *p;

	/* Direct scalar write then read (storefw / loadfw on $g_i). */
	g_i = 12345;
	g_u = 54321u;
	g_l = 100000L + 23456L;   /* 123456, needs storefl/loadfl (4-byte) */

	if (g_i == 12345)  printf("g_i ok\r\n");      else printf("g_i FAIL %d\r\n", g_i);
	if (g_u == 54321u) printf("g_u ok\r\n");      else printf("g_u FAIL %u\r\n", g_u);
	if (g_l == 123456L) printf("g_l ok\r\n");     else printf("g_l FAIL %ld\r\n", g_l);

	/* ++ / -- read-modify-write on a far global. */
	g_i++;
	--g_u;
	if (g_i == 12346)  printf("g_i_inc ok\r\n");  else printf("g_i_inc FAIL %d\r\n", g_i);
	if (g_u == 54320u) printf("g_u_dec ok\r\n");  else printf("g_u_dec FAIL %u\r\n", g_u);

	/* Member access on a far global struct (offset-0 and offset>0 members,
	 * plus a 4-byte long member). */
	g_pt.x = 11;
	g_pt.y = 22;
	g_pt.tag = 333444L;
	if (g_pt.x == 11)       printf("pt_x ok\r\n");   else printf("pt_x FAIL %d\r\n", g_pt.x);
	if (g_pt.y == 22)       printf("pt_y ok\r\n");   else printf("pt_y FAIL %d\r\n", g_pt.y);
	if (g_pt.tag == 333444L) printf("pt_tag ok\r\n"); else printf("pt_tag FAIL %ld\r\n", g_pt.tag);

	/* Struct copy between two far globals (emit_struct_copy, both sides far). */
	g_pt2 = g_pt;
	if (g_pt2.x == 11 && g_pt2.y == 22 && g_pt2.tag == 333444L)
		printf("ptcopy ok\r\n");
	else
		printf("ptcopy FAIL %d %d %ld\r\n", g_pt2.x, g_pt2.y, g_pt2.tag);

	/* Pointer global: store the address of a far-segment object into a
	 * far-segment pointer slot, read it back, deref it. */
	g_arr[0] = 7;
	g_arr[1] = 8;
	g_arr[2] = 9;
	g_arr[3] = 10;
	g_ptr = &g_arr[2];        /* a far pointer value stored to a far slot (storefl) */
	p = g_ptr;                /* read it back (loadfl) */
	if (*p == 9)            printf("gptr ok\r\n");   else printf("gptr FAIL %d\r\n", *p);
	if (g_ptr[1] == 10)     printf("gptr_idx ok\r\n"); else printf("gptr_idx FAIL %d\r\n", g_ptr[1]);

	/* Mix: read a far global into a local, compute, write back. */
	local = g_i * 2;
	g_i = local + 1;
	if (g_i == 24693)      printf("g_i_rw ok\r\n");  else printf("g_i_rw FAIL %d\r\n", g_i);

	return 0;
}
