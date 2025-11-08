#include "all.h"

/* Instruction selection for 8086/286/386 16-bit mode
 *
 * This is a minimal implementation to get started.
 * It needs significant expansion to handle all QBE IR operations.
 */

static void
fixarg(Ref *r, int k, Ins *i, Fn *fn)
{
	Ref r0, r1;
	int s;

	(void)i; /* unused for now */
	r0 = r1 = *r;

	switch (rtype(r0)) {
	case RCon:
		/* Constants can be used directly in most x86 instructions */
		/* Only load into temp if absolutely necessary */
		break;
	case RTmp:
		/* Check if this is a stack slot */
		if (isreg(r0))
			break;
		s = fn->tmp[r0.val].slot;
		if (s != -1) {
			/* Stack slot addressing */
			r1 = SLOT(s);
			break;
		}
		/* Check class compatibility */
		if (k == Kw && fn->tmp[r0.val].cls == Kl) {
			/* Need to narrow */
			/* For now, just use as-is */
		}
		break;
	}
	*r = r1;
}

static void
sel(Ins i, Fn *fn)
{
	Ins *i0;

	/* Emit the instruction first, then fix args
	 * This follows the pattern from rv64/isel.c
	 */
	if (i.op != Onop) {
		emiti(i);
		i0 = curi; /* fixarg() can change curi */
		fixarg(&i0->arg[0], argcls(&i, 0), i0, fn);
		fixarg(&i0->arg[1], argcls(&i, 1), i0, fn);
	}
}

void
i8086_isel(Fn *fn)
{
	Blk *b, **sb;
	Ins *i;
	Phi *p;
	uint n;

	/* Process blocks in forward order */
	for (b = fn->start; b; b = b->link) {
		/* Reset instruction buffer for this block */
		curi = &insb[NIns];

		/* Process phi nodes */
		for (sb=(Blk*[3]){b->s1, b->s2, 0}; *sb; sb++)
			for (p=(*sb)->phi; p; p=p->link) {
				for (n=0; p->blk[n] != b; n++)
					assert(n+1 < p->narg);
				fixarg(&p->arg[n], p->cls, 0, fn);
			}

		/* Process jump instruction - handled in emit phase */
		/* seljmp(b, fn); */

		/* Process regular instructions in reverse */
		for (i = &b->ins[b->nins]; i != b->ins;)
			sel(*--i, fn);

		/* Copy instructions to block */
		idup(b, curi, &insb[NIns]-curi);
	}

	if (debug['I']) {
		fprintf(stderr, "\n> After instruction selection:\n");
		printfn(fn, stderr);
	}
}
