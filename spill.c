#include "all.h"

static void
aggreg(Blk *hd, Blk *b)
{
	int k;

	/* aggregate looping information at
	 * loop headers */
	bsunion(hd->gen, b->gen);
	for (k=0; k<2; k++)
		if (b->nlive[k] > hd->nlive[k])
			hd->nlive[k] = b->nlive[k];
}

static void
tmpuse(Ref r, int use, int loop, Fn *fn)
{
	Mem *m;
	Tmp *t;

	if (rtype(r) == RMem) {
		m = &fn->mem[r.val];
		tmpuse(m->base, 1, loop, fn);
		tmpuse(m->index, 1, loop, fn);
	}
	else if (rtype(r) == RTmp && r.val >= Tmp0) {
		t = &fn->tmp[r.val];
		t->nuse += use;
		t->ndef += !use;
		t->cost += loop;
	}
}

/* evaluate spill costs of temporaries,
 * this also fills usage information
 * requires rpo, preds
 */
void
fillcost(Fn *fn)
{
	int n;
	uint a;
	Blk *b;
	Ins *i;
	Tmp *t;
	Phi *p;

	loopiter(fn, aggreg);
	if (debug['S']) {
		fprintf(stderr, "\n> Loop information:\n");
		for (b=fn->start; b; b=b->link) {
			for (a=0; a<b->npred; ++a)
				if (b->id <= b->pred[a]->id)
					break;
			if (a != b->npred) {
				fprintf(stderr, "\t%-10s", b->name);
				fprintf(stderr, " (% 3d ", b->nlive[0]);
				fprintf(stderr, "% 3d) ", b->nlive[1]);
				dumpts(b->gen, fn->tmp, stderr);
			}
		}
	}
	for (t=fn->tmp; t-fn->tmp < fn->ntmp; t++) {
		t->cost = t-fn->tmp < Tmp0 ? UINT_MAX : 0;
		t->nuse = 0;
		t->ndef = 0;
	}
	for (b=fn->start; b; b=b->link) {
		for (p=b->phi; p; p=p->link) {
			t = &fn->tmp[p->to.val];
			tmpuse(p->to, 0, 0, fn);
			for (a=0; a<p->narg; a++) {
				n = p->blk[a]->loop;
				t->cost += n;
				tmpuse(p->arg[a], 1, n, fn);
			}
		}
		n = b->loop;
		for (i=b->ins; i<&b->ins[b->nins]; i++) {
			tmpuse(i->to, 0, n, fn);
			tmpuse(i->arg[0], 1, n, fn);
			tmpuse(i->arg[1], 1, n, fn);
		}
		tmpuse(b->jmp.arg, 1, n, fn);
	}
	if (debug['S']) {
		fprintf(stderr, "\n> Spill costs:\n");
		for (n=Tmp0; n<fn->ntmp; n++)
			fprintf(stderr, "\t%-10s %d\n",
				fn->tmp[n].name,
				fn->tmp[n].cost);
		fprintf(stderr, "\n");
	}
}

static BSet *fst; /* temps to prioritize in registers (for tcmp1) */
static Tmp *tmp;  /* current temporaries (for tcmpX) */
static int ntmp;  /* current # of temps (for limit) */
static int locs;  /* stack size used by locals */
static int slot4; /* next slot of 4 bytes */
static int slot8; /* ditto, 8 bytes */
static BSet mask[2][1]; /* class masks */

static int
tcmp0(const void *pa, const void *pb)
{
	uint ca, cb;

	ca = tmp[*(int *)pa].cost;
	cb = tmp[*(int *)pb].cost;
	return (cb < ca) ? -1 : (cb > ca);
}

static int
tcmp1(const void *pa, const void *pb)
{
	int c;

	c = bshas(fst, *(int *)pb) - bshas(fst, *(int *)pa);
	return c ? c : tcmp0(pa, pb);
}

static Ref
slot(int t)
{
	int s;

	assert(t >= Tmp0 && "cannot spill register");
	s = tmp[t].slot;
	if (s == -1) {
		/* specific to NAlign == 3 */
		/* nice logic to pack stack slots
		 * on demand, there can be only
		 * one hole and slot4 points to it
		 *
		 * invariant: slot4 <= slot8
		 */
		if (KWIDE(tmp[t].cls)) {
			s = slot8;
			if (slot4 == slot8)
				slot4 += 2;
			slot8 += 2;
		} else {
			s = slot4;
			if (slot4 == slot8) {
				slot8 += 2;
				slot4 += 1;
			} else
				slot4 = slot8;
		}
		s += locs;
		tmp[t].slot = s;
	}
	return SLOT(s);
}

/* restricts b to hold at most k
 * temporaries, preferring those
 * present in f (if given), then
 * those with the largest spill
 * cost
 */
static void
limit(BSet *b, int k, BSet *f)
{
	static int *tarr, maxt;
	int i, t, nt;

	nt = bscount(b);
	if (nt <= k)
		return;
	if (nt > maxt) {
		free(tarr);
		tarr = emalloc(nt * sizeof tarr[0]);
		maxt = nt;
	}
	for (i=0, t=0; bsiter(b, &t); t++) {
		bsclr(b, t);
		tarr[i++] = t;
	}
	if (nt > 1) {
		if (!f)
			qsort(tarr, nt, sizeof tarr[0], tcmp0);
		else {
			fst = f;
			qsort(tarr, nt, sizeof tarr[0], tcmp1);
		}
	}
	for (i=0; i<k && i<nt; i++)
		bsset(b, tarr[i]);
	for (; i<nt; i++)
		slot(tarr[i]);
}

/* spills temporaries to fit the
 * target limits using the same
 * preferences as limit(); assumes
 * that k1 gprs and k2 fprs are
 * currently in use
 */
static void
limit2(BSet *b1, int k1, int k2, BSet *f)
{
	BSet b2[1];

	bsinit(b2, ntmp); /* todo, free those */
	bscopy(b2, b1);
	bsinter(b1, mask[0]);
	bsinter(b2, mask[1]);
	limit(b1, T.ngpr - k1, f);
	limit(b2, T.nfpr - k2, f);
	bsunion(b1, b2);
}

static void
sethint(BSet *u, bits r)
{
	int t, hr;
	Tmp *p;

	for (t=Tmp0; bsiter(u, &t); t++) {
		p = &tmp[phicls(t, tmp)];
		p->hint.m |= r;
		/* If a previous pass set hint.r (preferred single register)
		 * to a register now in the avoid mask, clear it.  Otherwise
		 * rega's `r = *hint(t)` path uses the preferred register
		 * directly and bypasses hint.m, causing live-across-call
		 * temps to land in caller-save regs and get clobbered. */
		hr = p->hint.r;
		if (hr != -1 && (r & BIT(hr)))
			p->hint.r = -1;
	}
}

/* reloads temporaries in u that are
 * not in v from their slots
 */
static void
reloads(BSet *u, BSet *v)
{
	int t;

	for (t=Tmp0; bsiter(u, &t); t++)
		if (!bshas(v, t))
			emit(Oload, tmp[t].cls, TMP(t), slot(t), R);
}

static void
store(Ref r, int s)
{
	if (s != -1)
		emit(Ostorew + tmp[r.val].cls, 0, R, r, SLOT(s));
}

static int
regcpy(Ins *i)
{
	return i->op == Ocopy && isreg(i->arg[0]);
}

static Ins *
dopm(Blk *b, Ins *i, BSet *v)
{
	int n, t;
	BSet u[1];
	Ins *i1;
	bits r;

	bsinit(u, ntmp); /* todo, free those */
	/* consecutive copies from
	 * registers need to be handled
	 * as one large instruction
	 *
	 * fixme: there is an assumption
	 * that calls are always followed
	 * by copy instructions here, this
	 * might not be true if previous
	 * passes change
	 */
	i1 = ++i;
	do {
		i--;
		t = i->to.val;
		if (!req(i->to, R))
		if (bshas(v, t)) {
			bsclr(v, t);
			store(i->to, tmp[t].slot);
		}
		bsset(v, i->arg[0].val);
	} while (i != b->ins && regcpy(i-1));
	bscopy(u, v);
	if (i != b->ins && iscall((i-1)->op)) {
		v->t[0] &= ~T.retregs((i-1)->arg[1], 0);
		/* Same callee-save fit as the main-loop iscall path.
		 * See feedback memory qbe-gcm-sinks-load-past-call. */
		limit2(v, T.nrsave[0] + T.nrglob, T.nrsave[1], 0);
		for (n=0, r=0; T.rsave[n]>=0; n++)
			r |= BIT(T.rsave[n]);
		v->t[0] |= T.argregs((i-1)->arg[1], 0);
	} else {
		limit2(v, 0, 0, 0);
		r = v->t[0];
	}
	sethint(v, r);
	reloads(u, v);
	do
		emiti(*--i1);
	while (i1 != i);
	return i;
}

static void
merge(BSet *u, Blk *bu, BSet *v, Blk *bv)
{
	int t;

	if (bu->loop <= bv->loop)
		bsunion(u, v);
	else
		for (t=0; bsiter(v, &t); t++)
			if (tmp[t].slot == -1)
				bsset(u, t);
}

/* spill code insertion
 * requires spill costs, rpo, liveness
 *
 * Note: this will replace liveness
 * information (in, out) with temporaries
 * that must be in registers at block
 * borders
 *
 * Be careful with:
 * - Ocopy instructions to ensure register
 *   constraints
 */
void
spill(Fn *fn)
{
	Blk *b, *s1, *s2, *hd, **bp;
	int j, l, t, k, lvarg[2];
	uint n;
	BSet u[1], v[1], w[1];
	Ins *i;
	Phi *p;
	Mem *m;
	bits r;
	int force_kl_slot;

	tmp = fn->tmp;
	ntmp = fn->ntmp;
	bsinit(u, ntmp);
	bsinit(v, ntmp);
	bsinit(w, ntmp);
	bsinit(mask[0], ntmp);
	bsinit(mask[1], ntmp);
	locs = fn->slot;
	slot4 = 0;
	slot8 = 0;
	for (t=0; t<ntmp; t++) {
		k = 0;
		if (t >= T.fpr0 && t < T.fpr0 + T.nfpr)
			k = 1;
		if (t >= Tmp0)
			k = KBASE(tmp[t].cls);
		bsset(mask[k], t);
	}

	/* On i8086, Kl (32-bit) values straddle two 16-bit registers (the
	 * DX:AX pair).  rega has no register-pair concept and assigns a Kl
	 * temp to a single 16-bit reg, silently dropping the high half on
	 * load/store/arith.  Force every Kl temp to be slot-resident so
	 * every Kl op reads/writes its slot directly via the two-word
	 * load/store paths already in i8086/emit.c.
	 * See feedback memory: i8086-kl-load-loses-high. */
	force_kl_slot = (strcmp(T.name, "i8086") == 0);
	if (force_kl_slot) {
		/* Alias each incoming Kl parameter temp to its ABI stack slot.
		 * selpar (i8086/abi.c) materializes a Kl param via
		 *   %t =l load SLOT(s)   with s < 0   (the param region above BP)
		 * and a negative slot only ever names a read-only incoming
		 * parameter.  Pre-setting tmp[%t].slot = s makes slot() below
		 * reuse it instead of carving a fresh below-BP slot, so the
		 * load lowers to a SLOT(s)<-SLOT(s) self-copy that emit elides
		 * (i8086/emit.c Oload handler) — removing the 4-instruction
		 * [bp+off]->[bp-N] materialization copy from every function with
		 * a Kl (far-pointer / long) parameter.  Safe because a param SSA
		 * temp is never reassigned (minic mutates params through a
		 * separate alloca), so reading [bp+off] always yields the
		 * originally-passed value.  Done here (after isel) rather than
		 * in abi.c because isel overloads a non-(-1) tmp[].slot to mean
		 * "fast-local alloca address" and would materialize &param. */
		for (b=fn->start, i=b->ins; i<&b->ins[b->nins]; i++)
			if (i->op == Oload && i->cls == Kl
			 && rtype(i->to) == RTmp
			 && rtype(i->arg[0]) == RSlot
			 && rsval(i->arg[0]) < 0)
				tmp[i->to.val].slot = rsval(i->arg[0]);
		for (t=Tmp0; t<ntmp; t++)
			if (tmp[t].cls == Kl)
				slot(t);
	}

	for (bp=&fn->rpo[fn->nblk]; bp!=fn->rpo;) {
		b = *--bp;
		/* invariant: all blocks with bigger rpo got
		 * their in,out updated. */

		/* 1. find temporaries in registers at
		 * the end of the block (put them in v) */
		curi = 0;
		s1 = b->s1;
		s2 = b->s2;
		hd = 0;
		if (s1 && s1->id <= b->id)
			hd = s1;
		if (s2 && s2->id <= b->id)
		if (!hd || s2->id >= hd->id)
			hd = s2;
		if (hd) {
			/* back-edge */
			bszero(v);
			hd->gen->t[0] |= T.rglob; /* don't spill registers */
			for (k=0; k<2; k++) {
				n = k == 0 ? T.ngpr : T.nfpr;
				bscopy(u, b->out);
				bsinter(u, mask[k]);
				bscopy(w, u);
				bsinter(u, hd->gen);
				bsdiff(w, hd->gen);
				if (bscount(u) < n) {
					j = bscount(w); /* live through */
					l = hd->nlive[k];
					limit(w, n - (l - j), 0);
					bsunion(u, w);
				} else
					limit(u, n, 0);
				bsunion(v, u);
			}
		} else if (s1) {
			/* avoid reloading temporaries
			 * in the middle of loops */
			bszero(v);
			liveon(w, b, s1);
			merge(v, b, w, s1);
			if (s2) {
				liveon(u, b, s2);
				merge(v, b, u, s2);
				bsinter(w, u);
			}
			limit2(v, 0, 0, w);
		} else {
			bscopy(v, b->out);
			if (rtype(b->jmp.arg) == RCall)
				v->t[0] |= T.retregs(b->jmp.arg, 0);
		}
		if (rtype(b->jmp.arg) == RTmp) {
			t = b->jmp.arg.val;
			assert(KBASE(tmp[t].cls) == 0);
			bsset(v, t);
			limit2(v, 0, 0, NULL);
			if (!bshas(v, t))
				b->jmp.arg = slot(t);
		}
		/* i8086: evict Kl temps from v before it becomes b->out.
		 * Otherwise rega's block-entry loop sees Kl temps in
		 * b->out and allocates registers for them — defeating
		 * the slot-resident-Kl invariant. */
		if (force_kl_slot) {
			int kt = Tmp0;
			while (bsiter(v, &kt)) {
				if (tmp[kt].cls == Kl) {
					bsclr(v, kt);
					slot(kt);
				}
				kt++;
			}
		}
		for (t=Tmp0; bsiter(b->out, &t); t++)
			if (!bshas(v, t))
				slot(t);
		bscopy(b->out, v);

		/* 2. process the block instructions */
		curi = &insb[NIns];
		for (i=&b->ins[b->nins]; i!=b->ins;) {
			i--;
			if (regcpy(i)) {
				i = dopm(b, i, v);
				continue;
			}
			bszero(w);
			if (!req(i->to, R)) {
				assert(rtype(i->to) == RTmp);
				t = i->to.val;
				if (bshas(v, t))
					bsclr(v, t);
				else {
					/* make sure we have a reg
					 * for the result */
					assert(t >= Tmp0 && "dead reg");
					bsset(v, t);
					bsset(w, t);
				}
			}
			j = T.memargs(i->op);
			for (n=0; n<2; n++)
				if (rtype(i->arg[n]) == RMem)
					j--;
			for (n=0; n<2; n++)
				switch (rtype(i->arg[n])) {
				case RMem:
					t = i->arg[n].val;
					m = &fn->mem[t];
					if (rtype(m->base) == RTmp) {
						bsset(v, m->base.val);
						bsset(w, m->base.val);
					}
					if (rtype(m->index) == RTmp) {
						bsset(v, m->index.val);
						bsset(w, m->index.val);
					}
					break;
				case RTmp:
					t = i->arg[n].val;
					lvarg[n] = bshas(v, t);
					bsset(v, t);
					if (j-- <= 0)
						bsset(w, t);
					break;
				}
			bscopy(u, v);
			if (iscall(i->op)
			&& (i+1 == &b->ins[b->nins] || !regcpy(i+1)))
				/* Void call (no following Ocopy register-to-temp
				 * already processed by dopm) — live-across-call
				 * temps must fit in actual callee-saves
				 * (ngpr - nrsave - nrglob).  Without this, rega's
				 * fallback bypasses the sethint() avoid mask when
				 * callee-saves are exhausted and a live temp lands
				 * in a caller-save register that the call clobbers.
				 * See feedback memory qbe-gcm-sinks-load-past-call. */
				limit2(v, T.nrsave[0] + T.nrglob, T.nrsave[1], w);
			else
				limit2(v, 0, 0, w);
			/* i8086: evict Kl temps from v and u so they never get
			 * register-allocated.  arg-rewrite below will turn Kl
			 * arg refs to RSlot, and reloads(u, v) will skip them
			 * (so no Oload Kl ?, slot, ? is inserted that would
			 * recreate a Kl RTmp).  slot() ensures the slot exists
			 * for the arg-rewrite. */
			if (force_kl_slot) {
				int kt;
				kt = Tmp0;
				while (bsiter(v, &kt)) {
					if (tmp[kt].cls == Kl) {
						bsclr(v, kt);
						slot(kt);
					}
					kt++;
				}
				kt = Tmp0;
				while (bsiter(u, &kt)) {
					if (tmp[kt].cls == Kl)
						bsclr(u, kt);
					kt++;
				}
			}
			for (n=0; n<2; n++)
				if (rtype(i->arg[n]) == RTmp) {
					t = i->arg[n].val;
					if (!bshas(v, t)) {
						/* do not reload if the
						 * argument is dead
						 */
						if (!lvarg[n])
							bsclr(u, t);
						i->arg[n] = slot(t);
					}
				}
			reloads(u, v);
			if (!req(i->to, R)) {
				t = i->to.val;
				/* i8086: rewrite Kl dest directly to its slot.
				 * The emit handler writes both AX and DX to
				 * slot+0/slot+2 via the two-word path, so we
				 * skip the lossy Ostorel RTmp→RSlot. */
				if (force_kl_slot && t >= Tmp0
				 && tmp[t].cls == Kl) {
					i->to = slot(t);
				} else {
					store(i->to, tmp[t].slot);
				}
				if (t >= Tmp0)
					/* in case i->to was a
					 * dead temporary */
					bsclr(v, t);
			}
			emiti(*i);
			r = v->t[0]; /* Tmp0 is NBit */
			/* For Call instructions not handled via dopm (e.g.,
			 * void-returning calls with no following Ocopy), tell
			 * the live-across-call temps to AVOID caller-save
			 * registers — those will be clobbered by the call.
			 * Without this, rega may pick AX/CX/DX for a temp
			 * needed after the call, producing wild writes when
			 * the post-call code reads garbage from the clobbered
			 * register. */
			if (iscall(i->op)) {
				int rs;
				for (rs=0; T.rsave[rs]>=0; rs++)
					r |= BIT(T.rsave[rs]);
			}
			/* Integer div/mul/rem that the backend emits in-place
			 * (i8086 idiv/imul/div) clobber a fixed reg pair (AX:DX)
			 * without isel modeling it.  Force temps live ACROSS
			 * such an op to avoid those regs, mirroring the call
			 * case above; otherwise a nested div/mul in one operand
			 * silently destroys a value the surrounding op still
			 * needs.  T.divclob is 0 on targets that decompose
			 * div/mul in isel (amd64/arm64/rv64), so this is inert
			 * there. */
			if (T.divclob
			 && (i->op == Odiv || i->op == Oudiv
			  || i->op == Orem || i->op == Ourem
			  || i->op == Omul))
				r |= T.divclob;
			if (r)
				sethint(v, r);
		}
		if (b == fn->start)
			assert(v->t[0] == (T.rglob | fn->reg));
		else
			assert(v->t[0] == T.rglob);

		for (p=b->phi; p; p=p->link) {
			assert(rtype(p->to) == RTmp);
			t = p->to.val;
			if (bshas(v, t)) {
				bsclr(v, t);
				store(p->to, tmp[t].slot);
			} else if (bshas(b->in, t))
				/* only if the phi is live */
				p->to = slot(p->to.val);
		}
		bscopy(b->in, v);
		idup(b, curi, &insb[NIns]-curi);
	}

	/* align the locals to a 16 byte boundary */
	/* specific to NAlign == 3 */
	slot8 += slot8 & 3;
	fn->slot += slot8;

	if (debug['S']) {
		fprintf(stderr, "\n> Block information:\n");
		for (b=fn->start; b; b=b->link) {
			fprintf(stderr, "\t%-10s (% 5d) ", b->name, b->loop);
			dumpts(b->out, fn->tmp, stderr);
		}
		fprintf(stderr, "\n> After spilling:\n");
		printfn(fn, stderr);
	}
}
