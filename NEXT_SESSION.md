# Next session (§7g — continue Phase 6 / open compiler tracks.  §7f [2026-06-14, this session] closed the carried **TOP-PRIORITY QBE bug** from §7e — the `assoccon` SIGABRT (`Assertion failed: (KWIDE(i2->cls) >= KWIDE(i1->cls)), function assoccon, file gvn.c, line 210`) that crashed the `qbe -t i8086 -m medium` step on the minimal NON-aoa repro `build/normal_ptrsub.c` (`static int a[6]; … (char*)&a[i]-(char*)&a[0]` in a loop).  **House rule honored — checked `upstream` FIRST:** `git show upstream/master:gvn.c`'s `assoccon` is BYTE-IDENTICAL to ours (in sync through `e786f06`), so this was NOT a known-fixed upstream gvn bug; the trigger was malformed i8086 IR produced by minic.  **Root cause (minic):** `prom()` (minic.y ~2150) has TWO `'-'` PTR−PTR handlers, and the FIRST one (reached before the same-kind early return at ~2159) returned **`LNG` unconditionally**, ignoring near/far — so a near `char*` difference was typed `l` (32-bit ptrdiff) even though near pointers are 16-bit.  That emitted `%t =l sub %tw1, %tw2` (a 32-bit subtract of two `w` operands); after GVN forwards the `loadw`s, the `l sub` ends up consuming a `w add` near-pointer def, and `assoccon` (`gvn.c:185-229`, which folds an associative pair `i1=(t2 op c1)`, `i2=t2->def=(x op c2)`) ASSERTS the inner def is at least as wide as the outer op → `KWIDE(w)=0 >= KWIDE(l)=1` is false → SIGABRT.  (The SECOND `'-'` handler at ~2187 already carried the correct `ISFAR(l->ctyp) ? LNG : INT`, but it is shadowed for the homogeneous PTR−PTR case by the same-kind return, which is exactly why the first handler exists — to intercept before it.)  **Two fixes landed, both gated:** (1) **minic near-ptrdiff typing** — the first handler now `return ISFAR(l->ctyp) ? LNG : INT;`, mirroring the far-aware second handler, so near ptrdiff is `INT`/Kw (16-bit) and far stays `LNG`/Kl (32-bit); the repro IR becomes a clean `%t =w sub …`.  (2) **QBE gvn `assoccon` robustness** — replaced the width `assert(KWIDE(i2->cls) >= KWIDE(i1->cls))` with `if (KWIDE(i2->cls) < KWIDE(i1->cls)) return;`: a malformed associative chain whose inner def is narrower than the outer op must NOT fold (importing the narrower value would be wrong) and a backend must NEVER SIGABRT on width-mismatched input.  **Semantics-preserving / byte-identical:** the minic change only alters the near PTR−PTR result class (was always `LNG`, now `INT` for near; far models compact/large/huge keep `LNG` since `ISFAR` is true → their codegen is unchanged), and the gvn bail fires only on width-mismatched chains that well-typed IR never produces — so the change is a no-op for all valid IR, proven by **MP compact rebuilding to a body of EXACTLY 731,088 bytes, byte-identical to the documented golden** (image 751,664, header 20,576 + body 731,088).  `make check` green; grammar conflicts UNCHANGED (the minic edit is pure C inside `prom()`, no productions).  **Gated bug-loud** with a new `minic/dos/examples/ptrdiff_probe.c` (+ `minic/dos/tests/ptrdiff_probe.golden.txt`), wired into `tools/test-dos.sh` at SMALL + MEDIUM + COMPACT + LARGE: it exercises the original crashing loop form (`(char*)&a[i]-(char*)&a[0]`), char-array byte differences, typed `int*` element-count differences, and `struct*` element + byte differences (output model-independent since `sizeof(int)==2` on every model).  **Verified bug-loud:** git-stashing BOTH fixes and rebuilding minic & qbe makes the probe build ABORT (Abort trap 6) in the `qbe -t i8086 -m medium` step — a compiler crash is the loudest possible gate; restoring the fixes gives byte-exact-vs-golden on all four models in DOSBox.  **test-dos 309/309 → 313/313** (the four new entries `[ok]`, every prior entry unchanged).  Since `gvn.c` is middle-end (not `i8086/emit.c`) and the MP byte-identical rebuild proves codegen did NOT shift, the emit-bracket audit was NOT required and NO Victor run was needed.  The TOP-PRIORITY QBE `assoccon` open track is now CLOSED, and there is **no QBE backend bug currently open** — the carried tracks below are all minic/backend feature gaps or Phase-6 harness work.  Next: pick a carried track — huge `_qbe_huge_add` ≥0x8000 variable-index gap (§4i — pure i8086 backend, needs the emit audit after); far static-DATA-ptr reloc (§1g); Kw spill-slot sharing (frame-size lever, no consumer pain); the bounded aoa init/multi-declarator gap (§7e — brace-init `jmp_buf x[2]={…}` / multi-decl `jmp_buf a[2], b[2]` still ignore `g_td_arraydim`, no realistic consumer) — OR resume Phase-6 newlibc gating: `serial_loopback_test` remains the only tractable bm_testhost candidate but needs real new harness plumbing (channel-A polled RX + an rs232a TXD→RXD MAME loopback colliding with the rs232a `null_modem` capture → move the gate's serial capture to channel B, plus RX-timing determinism on the 5 MHz 8088); `interrupt_test` stays SKIPPED per §6v.)

## §7f session notes (2026-06-14)

### The bug (the TOP-PRIORITY QBE track from §7e, now CLOSED)
- QBE **SIGABRT** `Assertion failed: (KWIDE(i2->cls) >= KWIDE(i1->cls)),
  function assoccon, file gvn.c, line 210` on the minimal NON-aoa repro
  `build/normal_ptrsub.c` (`static int a[6]; … (char*)&a[i]-(char*)&a[0]`
  in a loop) under `--model=medium` — Abort trap 6 in the `qbe -t i8086`
  step.
- **House rule honored:** checked `upstream` FIRST — `git show
  upstream/master:gvn.c` `assoccon` is BYTE-IDENTICAL to ours, so this is
  NOT a known-fixed upstream gvn bug; the trigger is malformed i8086 IR.
- **Root cause (minic):** `prom()` ([[minic.y]] ~2150) has TWO `'-'`
  PTR−PTR handlers.  The FIRST one (reached before the same-kind early
  return) returned **`LNG` unconditionally**, ignoring near/far — so a near
  `char*` difference was typed `l` (32-bit).  In a near-data model the
  operands are 16-bit (`w`), giving `%t =l sub %tw1, %tw2`.  After GVN
  forwards the `loadw`s, the `l sub` consumes a `w add` near-pointer def;
  `assoccon` folds the associative pair and ASSERTS the inner def is at
  least as wide as the outer op → `KWIDE(w)=0 >= KWIDE(l)=1` is false →
  abort.  (The SECOND `'-'` handler at ~2187 already had the correct
  `ISFAR ? LNG : INT`, but is shadowed by the same-kind return.)

### The two fixes (both gated)
1. **minic prom() near-ptrdiff typing** (root cause): the first handler now
   `return ISFAR(l->ctyp) ? LNG : INT;` — near ptrdiff is `INT`/Kw (16-bit),
   far stays `LNG`/Kl (32-bit).  IR for the repro becomes `%t =w sub …`.
2. **QBE gvn `assoccon` robustness** (`gvn.c:210`): replaced the width
   `assert` with `if (KWIDE(i2->cls) < KWIDE(i1->cls)) return;` — a
   malformed associative chain whose inner def is narrower than the outer
   op must NOT fold (importing the narrower value would be wrong) and must
   NEVER SIGABRT.  Well-typed IR always satisfies the invariant, so this is
   a no-op for valid input (proven by the MP byte-identical rebuild).

### Why it's safe / byte-identical
- minic fix only changes near PTR−PTR result class (was always LNG, now
  INT for near); far models (compact/large/huge — ISFAR true) keep LNG, so
  their codegen is unchanged.  Near models had been emitting class-
  inconsistent IR that either crashed or was wrong.
- gvn fix bails only on width-mismatched chains, which valid IR never
  produces → MP compact body **731,088 bytes, byte-identical** to the
  golden, confirming codegen unchanged across the whole corpus.
- `make check` green.  Grammar conflicts UNCHANGED (no productions touched
  — the minic change is pure C in `prom()`).

### Gate (bug-loud) + toolchain checks
- `minic/dos/examples/ptrdiff_probe.c` + golden, wired into
  `tools/test-dos.sh` at SMALL + MEDIUM + COMPACT + LARGE.  Exercises the
  original loop form (`(char*)&a[i]-(char*)&a[0]`), char-array byte diffs,
  typed `int*` element-count diffs, and `struct*` element + byte diffs —
  output model-independent (sizeof(int)==2 everywhere).
- **Bug-loud verified:** git-stash both fixes + rebuild minic & qbe → the
  probe build ABORTS (Abort trap 6) in the `qbe -t i8086 -m medium` step;
  restore → byte-exact vs golden on all four models in DOSBox.  A compiler
  crash is the loudest possible gate.
- **test-dos 309 → 313** (four new entries `[ok]`, every prior unchanged).
- gvn.c is middle-end, but the MP byte-identical rebuild proves codegen
  did NOT shift → emit-bracket audit NOT required, NO Victor run.

### ⇒ Next session (§7g): carried tracks (no QBE bug currently open)
- huge `_qbe_huge_add` ≥0x8000 variable-index gap (§4i — pure i8086
  backend, needs the emit audit after).
- far static-DATA-ptr reloc (§1g).
- Kw spill-slot sharing (frame-size lever, no consumer pain).
- Bounded aoa gap (§7e): brace-init / multi-declarator array-of-array-
  typedef still ignore `g_td_arraydim`; no realistic consumer.
- Phase-6 newlibc `serial_loopback_test` (needs NEW harness plumbing —
  channel-A polled RX + rs232a TXD→RXD loopback, move gate capture to
  channel B, RX-timing determinism); `interrupt_test` stays SKIPPED (§6v).

---

# Next session (§7f — continue Phase 6 / open compiler tracks.  §7e [2026-06-13, this session] reduced AND fixed the carried **`jmp_buf bufs[6]` cross-frame longjmp** track (§4v, unreduced for many sessions) — the user picked it.  **Reduction (bug-loud):** `jmp_buf` is `int[8]`, so `jmp_buf bufs[N]` is an array whose ELEMENT is itself an array typedef.  A recursive probe that set `bufs[0..5]` then `longjmp(bufs[target], …)` with a runtime `target=2` resumed the WRONG frame (`caught 5`, the deepest, instead of `caught 2`); a stride probe showed `&bufs[i]-&bufs[0]==0` for every i, and the generated `data` block was sized **12 bytes for N=6, not 96**.  **Root cause:** minic's flat type system can't represent `int (*)[8]`, and EVERY array-declarator rule ignored the typedef's inner dimension (`g_td_arraydim`): `bufs[N]` was sized as `int[N]` and a subscript `bufs[i]` was lowered as a SCALAR-int access — `@(bufs + i*sizeof(int))`, i.e. stride 2 **and a value load** — instead of the row ADDRESS `bufs + i*16`.  So every `setjmp(bufs[i])` aliased `bufs[0]` (last writer = the deepest frame) and the cross-frame `longjmp` resumed it.  (minic has NO true 2-D arrays at all — `int x[6][8]` is a hard parse error — so an array-typedef element is the only door into this shape, and nothing in MP/stevie/the corpus uses it, which is why it sat latent.)  **The fix** adds a `varh.aoa_dim` flag (the inner dimension D) set at the three array-of-array-typedef declaration sites — file-scope global (`'[' expr ']' ';'`), block-local (`dcls type IDENT '[' expr ']' ';'`), and function-local static (`STATIC type IDENT '[' expr ']' ';'`) — each of which, when `g_td_arraydim > 0`, now registers the variable as `IDIR(g_td_arrayelem)` (e.g. `int*`) with the CORRECT `N*D*sizeof(elem)` byte size (and `iralign(elem)`).  Then `mkidx()` desugars a one-level subscript on an aoa variable to the **bare pointer add `bufs + (i*D)` with NO deref** (instead of the normal `@(bufs + i)`): the existing `'+'` Scale path multiplies by `sizeof(elem)`, giving byte offset `i*D*sizeof(elem)` = the `int*` row address — which reuses `far_ptr_offset_binop` for free under compact/large, and COMPOSES naturally (`bufs[i][j]` takes the ordinary `@(+ . j)` path on the resulting `int*`, and `setjmp(bufs[i])` gets the row pointer it wants).  The new `mkidx` branch only fires when `var_aoa_dim(name) > 0`, so all non-aoa code is byte-identical.  **Semantics-preserving:** the aoa path is a brand-new construct (previously miscompiled), and the non-aoa else-branches were left textually identical (the block-local rule keeps `v = $3->u.v`, no new `block_scope_decl` routing) → MP/stevie/the gate corpus generate byte-identical code.  Grammar conflicts UNCHANGED (115 s/r, 0 r/r, 10 never-reduced — only C action-body code + helpers + a `varh` field changed, no productions).  **A note for whoever revisits:** the QBE `assoccon` ABORT (`gvn.c:210`) that the row-to-row pointer-subtraction stride diagnostic hit is a **PRE-EXISTING, UNRELATED QBE bug** — it reproduces on `(char*)&a[i]-(char*)&a[0]` for a plain `int a[6]` too (nested const-mul + i8086 `l`/`w` ptrdiff class mix), and is NOT triggered by any realistic setjmp-array usage; left untouched.  **Gated bug-loud** with a new `minic/dos/examples/arr_jmpbuf_probe.c` (+ `minic/dos/tests/arr_jmpbuf_probe.golden.txt`), the array-of-jmp_buf counterpart to `setjmp_probe.c`, wired into `tools/test-dos.sh` at MEDIUM + COMPACT + LARGE (matching setjmp_probe; model-independent output): case A cross-frame `longjmp(bufs[target])` into a runtime-indexed FILE-SCOPE array (`caught 2` then unwinds 1,0); case B a BLOCK-LOCAL `jmp_buf lb[3]` runtime-indexed in-frame; case D a FUNCTION-LOCAL STATIC `static jmp_buf sb[3]` runtime-indexed; case C a composing double-subscript `dd[i][j]` write/read over a `typedef int[8]` element.  Verified bug-loud: the UNFIXED minic (git stash + rebuild) prints `caught 5` and `dd0_0=30` (the alias + stride corruption); the fixed minic is byte-exact vs the golden under ALL THREE models in DOSBox, and the pre-existing `setjmp_probe` stays byte-identical on medium/compact/large.  **test-dos 306/306 → 309/309** (the three new entries `[ok]`, every prior entry unchanged).  Since this is a `minic.y` frontend change (NOT i8086/emit.c), the emit-bracket audit is NOT required; the required toolchain check is the MP byte-compare, and **MP compact rebuilt to a body of EXACTLY 731,088 bytes — byte-identical to the documented golden** (image 751,664, header 20,576 + body 731,088), confirming codegen is unchanged → no Victor run needed (stevie's medium-.EXE size gate inside test-dos also still `[ok]`); `make check` green.  The "`jmp_buf bufs[6]` cross-frame longjmp (§4v)" open track is now CLOSED.  **One bounded gap remains** (documented, not a regression — the pre-existing status quo for those forms): array-of-array-typedef in a brace-INITIALIZED (`jmp_buf x[2] = {…}`, nonsensical for jmp_buf) or MULTI-declarator (`jmp_buf a[2], b[2]`) declaration still ignores `g_td_arraydim` and would miscompile; no realistic consumer uses them, and they'd need the same `aoa_dim` treatment if one ever appears.  **⇒ TOP PRIORITY NEXT SESSION — a REAL QBE BUG surfaced this session (this is the whole point of the project: minic/MP/newlibc exist to surface QBE backend bugs; finding one is the win, not a footnote).**  QBE **SIGABRTs** (`Assertion failed: (KWIDE(i2->cls) >= KWIDE(i1->cls)), function assoccon, file gvn.c, line 210`) on a MINIMAL, NON-aoa input: `static int a[6]; … (char*)&a[i] - (char*)&a[0]` in a loop under `--model=medium` (saved as `build/normal_ptrsub.c`).  Two angles, both worth fixing: (1) **QBE robustness** — `assoccon` (`gvn.c:185-229`) folds an associative pair `i1=(t2 op c1)`, `i2=t2->def=(x op c2)` and ASSERTS the inner class is at least as wide as the outer; on i8086 a `w`/`l` mix in the chain violates that and crashes instead of bailing.  A backend should NEVER SIGABRT on well-formed-looking input.  (2) **minic medium-model ptrdiff typing** — the SSA shows `%t25 =l sub %t27, %t29` where BOTH operands are `=w loadw` (16-bit NEAR pointers): minic types a near-pointer subtraction as `l` (32-bit LNG ptrdiff) in the medium model — class-inconsistent IR (`l` result, `w` operands) — when near ptrdiff should be `w`/INT (`prom()` returns `ISFAR(l->ctyp)?LNG:INT`, so something is marking these near `char*` as FAR, or the `(char*)`-cast/global-`&a[i]` address path sets it).  House rule: **check `upstream` (c9x.me/qbe) FIRST** for the assoccon assert before touching it — it may be a known generic gvn bug.  Plan: reduce both sides; decide whether the IR is valid (→ QBE assoccon must handle the width mismatch, not assert) or invalid (→ fix minic's medium near-ptrdiff class to `w`, AND QBE still shouldn't crash); GATE bug-loud with a probe doing array-element pointer subtraction that currently CRASHES the compiler (a compiler crash is the loudest possible gate); after any QBE change run `make check`, the i8086 emit audit if `emit.c`/middle-end codegen shifts, and the MP compact byte-compare.  THEN, only if that's closed, the other carried tracks: huge `_qbe_huge_add` ≥0x8000 (§4i — pure i8086 backend, needs emit audit); far static-DATA-ptr reloc (§1g); Kw spill-slot sharing; the bounded aoa init/multi-decl gap above; OR Phase-6 newlibc `serial_loopback_test` (needs harness work — channel-A polled RX + rs232a TXD→RXD loopback, move gate capture to channel B, RX-timing determinism; `interrupt_test` stays SKIPPED per §6v).)

## §7e session notes (2026-06-13)

### The bug (reduced from the §4v note)
- `jmp_buf` == `int[8]`, so `jmp_buf bufs[N]` is an array-of-array-typedef.
- minic's flat type system has no `int (*)[8]`; ALL array-declarator rules
  ignored the typedef inner dim `g_td_arraydim`.  Result: `bufs[N]` sized as
  `int[N]` (12 B for N=6, not 96) and `bufs[i]` lowered as a SCALAR int
  access `@(bufs + i*2)` (stride 2 + a load) instead of the row address
  `bufs + i*16`.
- So every `setjmp(bufs[i])` aliased `bufs[0]` (last writer wins) and a
  cross-frame `longjmp(bufs[target])` resumed the deepest frame.  Bug-loud:
  `recurse` setting bufs[0..5], `longjmp(bufs[2])` → `caught 5` not `caught 2`;
  `&bufs[i]-&bufs[0]==0` ∀i.  (`int x[6][8]` is a hard parse error — no true
  2-D arrays — so the typedef element is the only path in.)

### The fix
- New `varh.aoa_dim` (inner dim D) + `var_set_aoa_dim`/`var_aoa_dim` helpers
  (plain probe by node name, like `var_isarray`, so renamed locals resolve).
- Three decl sites set it when `g_td_arraydim > 0`: file-scope global
  (`'[' expr ']' ';'`), block-local (`dcls … '[' expr ']' ';'`), function-
  local static (`STATIC … '[' expr ']' ';'`) — each registers `IDIR(elem)`
  with size `N*D*sizeof(elem)` and `iralign(elem)`.
- `mkidx()`: when `var_aoa_dim(a) > 0`, desugar `a[i]` to the bare pointer
  add `mknode('+', a, i*D)` (NO `@` deref).  The existing `'+'` Scale scales
  by `sizeof(elem)` → byte offset `i*D*sizeof(elem)` = the `int*` row addr.
  Reuses `far_ptr_offset_binop` (compact/large) for free; composes:
  `bufs[i][j]` and `setjmp(bufs[i])` both work.

### Why it's safe / byte-identical
- The `mkidx` branch fires only for aoa variables (flag-gated); the non-aoa
  else-branches are textually unchanged (block-local keeps `v = $3->u.v`).
- aoa is a previously-miscompiled construct nothing in the corpus uses.
- Conflicts UNCHANGED (115 s/r, 0 r/r) — no grammar productions added.

### Gate (bug-loud) + toolchain checks
- `minic/dos/examples/arr_jmpbuf_probe.c` + golden, MEDIUM+COMPACT+LARGE
  (matches setjmp_probe): A cross-frame longjmp into a runtime-indexed
  file-scope array; B block-local `jmp_buf lb[3]`; D function-local static
  `static jmp_buf sb[3]`; C composing `dd[i][j]` over a `typedef int[8]`.
- Bug-loud verified: unfixed minic → `caught 5` / `dd0_0=30`; fixed byte-exact
  vs golden on all three models; existing `setjmp_probe` still byte-identical.
- **test-dos 306 → 309**; `make check` green.
- minic.y/frontend (NOT emit.c) → NO emit audit.  MP compact body EXACTLY
  **731,088 bytes**, byte-identical → codegen unchanged, NO Victor run.

### ⇒ TOP PRIORITY NEXT SESSION: a REAL QBE bug (this is the mission)
The project exists to surface QBE backend bugs — minic/MP/newlibc are the
fuzzers.  §7e surfaced one and it is the headline for next session, not a
footnote.

**Symptom:** QBE SIGABRTs —
`Assertion failed: (KWIDE(i2->cls) >= KWIDE(i1->cls)), function assoccon,
file gvn.c, line 210`.

**Minimal repro (NON-aoa, plain int array):** `build/normal_ptrsub.c` —
```c
static int a[6];
int main(void){ int i; char *base=(char*)&a[0];
  for(i=0;i<6;i++){ char *p=(char*)&a[i]; printf("%d %d\n", i,(int)(p-base)); }
  return 0; }
```
`tools/build-example.sh --model=medium build/normal_ptrsub.c` → Abort trap 6
inside the `qbe -t i8086 -m medium` step.

**The smoking-gun IR** (`build/examples/normal_ptrsub/normal_ptrsub.ssa`):
`%t25 =l sub %t27, %t29` where `%t27`/`%t29` are both `=w loadw` — a 32-bit
(`l`) subtract of two 16-bit (`w`) NEAR pointers.  Class-inconsistent IR
(`l` result, `w` operands) trips `assoccon`'s width assert.

**Two fixes, both in scope:**
1. **QBE robustness (the real target):** `gvn.c:185-229` `assoccon` folds an
   associative pair `i1=(t2 op c1)`, `i2=t2->def=(x op c2)` and ASSERTS
   `KWIDE(i2->cls) >= KWIDE(i1->cls)`.  On i8086 a `w`/`l` mix violates it →
   SIGABRT.  A backend must not abort on this; bail (don't fold) or widen.
   **Check `upstream` (c9x.me/qbe) FIRST** (house rule) — may be a known gvn bug.
2. **minic medium ptrdiff typing:** near-pointer subtraction is typed `l`
   (32-bit) in medium; near ptrdiff should be `w` (`prom()` returns
   `ISFAR?LNG:INT`, so a near `char*` is being marked FAR — chase the
   `(char*)` cast / global `&a[i]` address path).

**Gate:** a probe doing array-element pointer subtraction — it currently
CRASHES the compiler, the loudest possible bug-loud gate.  After any QBE
change: `make check`, the i8086 emit audit if middle-end/`emit.c` codegen
shifts, and the MP compact byte-compare.

### Bounded aoa gap (NOT a regression, low priority)
- Brace-initialized (`jmp_buf x[2]={…}`) and multi-declarator
  (`jmp_buf a[2], b[2]`) array-of-array-typedef declarations still ignore
  `g_td_arraydim` and would miscompile.  No realistic consumer; same
  `aoa_dim` treatment if one ever appears.

### Closed track + other carried tracks
- CLOSED: "`jmp_buf bufs[6]` cross-frame longjmp" (§4v).
- Carried compiler (AFTER the QBE assoccon bug above): huge `_qbe_huge_add`
  ≥0x8000 (§4i, backend, needs emit audit); far static-DATA-ptr reloc (§1g);
  Kw spill-slot sharing; the bounded aoa init/multi-decl gap above.
- Phase-6 newlibc: `serial_loopback_test` (only tractable bm_testhost
  candidate, needs harness work — channel-A polled RX + rs232a TXD→RXD
  loopback, move gate capture to channel B, RX-timing determinism);
  `interrupt_test` stays SKIPPED per §6v.

---

Older session headers (§7d and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
