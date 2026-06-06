/*
 * farstruct_ptr_probe.c — exercises writing a POINTER MEMBER of a far-segment
 * struct under far-data models (built with QBE_FAR_STATIC_DATA=1, so the struct
 * sits in its own FAR_DATA segment outside DGROUP).
 *
 * The bug ([[minic-far-struct-ptr-member-store]]): the assignment store
 * condition far-stored only when ISFAR(lvalue) && KIND != PTR/FUN, or when the
 * lvalue was a DIRECT global (FARSTORAGE).  A pointer member of a far struct
 * reaches the store as a computed Tmp address (FARSTORAGE false) whose value
 * type is PTR (so the ISFAR clause excludes it) — so minic emitted a NEAR store
 * (to the DGROUP shadow of the symbol), while the member READ correctly used a
 * FAR load (loadfar) of the real far segment.  Result: the value written never
 * reached where the reader looked, so the pointer read back as garbage.
 *
 * This is exactly the shape of MicroPython's `MP_STATE_VM(last_pool) =
 * &CONST_POOL;` (mp_state_ctx is a far BSS struct, last_pool a pointer member):
 * qstr_find_strn read last_pool far, qstr_init wrote it near, the pool loop saw
 * NULL, and `print` was never found.  The fix adds a storage-far side-channel
 * (lval_storage_far) so the store matches the read.
 *
 * Build:  QBE_FAR_STATIC_DATA=1 tools/build-example.sh --model=compact \
 *             minic/dos/examples/farstruct_ptr_probe.c
 */

#include <stdio.h>

struct node { int a; int *p; long tag; struct node *next; };

static struct node g_n;        /* far struct with two pointer members */
static int         g_data[3];

int main(void)
{
	int *q;

	g_data[0] = 10;
	g_data[1] = 20;
	g_data[2] = 30;

	/* Write a pointer member of a far struct: storefl to the member's far
	 * address (was a near store before the fix). */
	g_n.a = 5;
	g_n.p = &g_data[1];
	g_n.tag = 777L;
	g_n.next = &g_n;          /* self-referential pointer member */

	/* Read the pointer member back via member access (loadfar) and deref it. */
	if (g_n.a == 5)        printf("a ok\r\n");        else printf("a FAIL %d\r\n", g_n.a);
	if (*g_n.p == 20)      printf("p ok\r\n");        else printf("p FAIL\r\n");
	if (g_n.tag == 777L)   printf("tag ok\r\n");      else printf("tag FAIL %ld\r\n", g_n.tag);

	/* Deref through the self-pointer member: g_n.next must point back at g_n,
	 * so g_n.next->a reads the same far storage. */
	if (g_n.next->a == 5)  printf("next ok\r\n");     else printf("next FAIL\r\n");

	/* Pull the pointer member into a local and deref (loadfar of a far ptr
	 * member into a near local, then a far deref of the loaded value). */
	q = g_n.p;
	if (*q == 20)          printf("q ok\r\n");        else printf("q FAIL\r\n");
	q[1] = 99;             /* write through the recovered pointer: g_data[2]=99 */
	if (g_data[2] == 99)   printf("wr ok\r\n");       else printf("wr FAIL %d\r\n", g_data[2]);

	return 0;
}
