/*
 * declgram_probe.c -- Phase 1 Tier 1 declaration-grammar gate.
 *
 * Pins the three universal MicroPython declaration-grammar blockers
 * closed this session (see NEXT_SESSION.md / MICROPYTHON_PORT.md):
 *
 *   1. Abstract (unnamed) parameters in regular prototypes:
 *        int addthem(int, int);   void touch(const char *, int);
 *   2. Forward `typedef struct Tag Alias;` (incomplete struct tags),
 *      in both the MicroPython distinct-name form (struct _point /
 *      Point) and the same-name idiom (struct Node / Node) -- the
 *      latter exercises the tag-vs-typedef namespace split in the lexer.
 *   3. Flexible array members `Type name[];` (contribute 0 bytes,
 *      sit at the current offset).
 *
 * These are grammar features but they also exercise codegen: struct
 * layout (member offsets / SIZE), pointer-through-incomplete deref,
 * and a real call through an abstract-param prototype.  So this is a
 * runtime probe, not just a parse check.
 *
 * Build:  tools/build-example.sh --model=medium minic/dos/examples/declgram_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/declgram_probe/declgram_probe.exe \
 *             | diff - minic/dos/tests/declgram_probe.golden.txt
 *
 * Wired into tools/test-dos.sh under the medium runtime section.
 */

#include <stdio.h>

/* --- Feature 1: abstract (unnamed) parameters in prototypes --- */
int addthem(int, int);
int firstof(const char *, int);

/* --- Feature 2: forward typedef, distinct-name (MicroPython style) --- */
typedef struct _point Point;
struct _point { int x; int y; };

/* --- Feature 2: forward typedef, same-name idiom --- */
typedef struct Node Node;
struct Node { int v; Node *next; };

/* --- Feature 3: flexible array member --- */
struct fam { int n; char data[]; };

/* Definitions of the abstract-param prototypes (now with names). */
int addthem(int a, int b) { return a + b; }
int firstof(const char *s, int dflt) { return s ? s[0] : dflt; }

int main()
{
	Point pt;
	Node n2;
	Node n1;
	Node *p;
	struct fam fv;
	int sum;
	long off;

	/* Feature 1: call through abstract-param prototypes. */
	printf("add=%d (want 7)\r\n", addthem(3, 4));
	printf("first=%d (want 90)\r\n", firstof("Z", -1));   /* 'Z' == 90 */

	/* Feature 2a: distinct-name forward typedef, struct fully usable. */
	pt.x = 11;
	pt.y = 31;
	printf("pt=%d (want 42)\r\n", pt.x + pt.y);

	/* Feature 2b: same-name forward typedef + self-referential pointer. */
	n2.v = 20;
	n2.next = 0;
	n1.v = 22;
	n1.next = &n2;
	sum = 0;
	for (p = &n1; p; p = p->next)
		sum += p->v;
	printf("list=%d (want 42)\r\n", sum);

	/* Feature 3: flexible array member contributes 0 bytes; sizeof is
	 * just the head, and data[] sits right after n. */
	printf("famsz=%d (want %d)\r\n", (int)sizeof(struct fam), (int)sizeof(int));
	off = (long)(char *)&fv.data - (long)(char *)&fv;
	printf("famoff=%d (want %d)\r\n", (int)off, (int)sizeof(int));

	return 0;
}
