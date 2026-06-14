# Next session (§7h — continue Phase 6 / open compiler tracks.  §7g [2026-06-14, this session] closed the carried **huge `_qbe_huge_add` ≥0x8000 variable-index gap (§4i)** — the user picked it.  **The surprise: the libstub helper `_qbe_huge_add` was CORRECT all along; the bug was the CALLER.**  minic's Scale path (`prom()`, minic.y ~2209, the shared pointer-arithmetic index-scaling code) UNCONDITIONALLY `sext`s a sub-`long` index before the `=l mul <sz>` that scales it by the element size — `sext()` always emits `extsw` regardless of source signedness (its signedness-aware sibling is `widen_int_to_long()`, which picks `extuw`/`extsw`).  So an UNSIGNED `size_t` byte offset whose 16-bit value is ≥0x8000 (the canonical case: MicroPython `gc_alloc`'s `pool_start + start_block*BYTES_PER_BLOCK` on a heap >32 KB, where `start_block≥2048` → offset ≥32768) was sign-extended to a NEGATIVE 32-bit value, then handed to `_qbe_huge_add(ptr, offset)` which faithfully added it to the 20-bit linear address — landing BELOW the object.  **Why compact/large never saw this (and why §4i's fix didn't reach it):** under compact/large `far_ptr ± idx` routes through the dedicated offset-only `addfo`/`subfo` ops, which read ONLY arg1's low 16 bits, where `extsw` and `extuw` agree bit-for-bit — so the sign-extension is harmless there and §4i deliberately left the Scale path's `sext` untouched to keep MP byte-identical.  Under HUGE, objects can exceed 64 KB so a genuine segment carry is required: minic routes the SAME indexing through `huge_ptr_binop` → `_qbe_huge_add`, which uses the FULL 32-bit scaled value — so the sign now matters, and the gap that §4i flagged as "pre-existing, in the helper" was actually in the index typing one level up.  **The fix (one site, minic.y Scale path):** under `memmodel == MHuge` the non-`Con` index is widened with `widen_int_to_long(r)` (source-signedness: `extuw` for unsigned, `extsw` for signed) instead of the unconditional `sext(r)`; compact/large/near keep the uniform `sext` (the `else` branch), so the change is gated to huge and the MP-compact corpus is byte-identical BY CONSTRUCTION.  **Verified:** the unfixed huge build printed `direct=0` for b≥2048 + `FAIL`; the SSA showed `%t157 =l extsw %t156` (off = an unsigned `size_t` `loadw`) feeding `=l mul 1, …` then `$qbe_huge_add`.  Post-fix that line is `%t157 =l extuw %t156` (the signed `int i` blocks-index correctly STAYS `extsw`), and the huge build prints `direct=0x41+i` for every block + `ALL OK`, byte-exact vs the existing `gc_bigheap_probe.golden.txt` (the probe output is model-independent, `sizeof(int)==2` everywhere).  **Gated bug-loud** by adding the `:huge` model to the existing `gc_bigheap_probe` entry in `tools/test-dos.sh` (it was compact+large only; the probe header + the test-dos comment block were updated to record the huge gate and that the helper was correct).  **test-dos 313/313 → 314/314** (the new `huge runtime (gc_bigheap_probe)` entry `[ok]`, every prior entry unchanged; compact+large re-verified byte-exact vs golden).  Toolchain checks: `make check` green; grammar conflicts UNCHANGED (pure C inside `prom()`, no productions); **MP compact rebuilt to a body of EXACTLY 731,088 bytes — byte-identical to the documented golden** (image 751,664, header 20,576 + body 731,088), confirming compact codegen did not shift → no Victor run; and since this is a `minic.y` frontend change (NOT i8086/emit.c) the emit-bracket audit was NOT required.  The "huge `_qbe_huge_add` ≥0x8000 variable-index gap (§4i)" open track is now CLOSED.  Next: pick a carried track — far static-DATA-ptr reloc (§1g); Kw spill-slot sharing (frame-size lever, no consumer pain); the bounded aoa init/multi-declarator gap (§7e — brace-init `jmp_buf x[2]={…}` / multi-decl `jmp_buf a[2], b[2]` still ignore `g_td_arraydim`, no realistic consumer) — OR resume Phase-6 newlibc gating: `serial_loopback_test` remains the only tractable bm_testhost candidate but needs real new harness plumbing (channel-A polled RX + an rs232a TXD→RXD MAME loopback colliding with the rs232a `null_modem` capture → move the gate's serial capture to channel B, plus RX-timing determinism on the 5 MHz 8088); `interrupt_test` stays SKIPPED per §6v.  There is NO QBE backend bug currently open — the carried tracks are all minic/backend feature gaps or Phase-6 harness work.)

## §7g session notes (2026-06-14)

### The bug (carried open track 1, §4i — the huge half of the far-ptr unsigned-index family)
- Under `--model=huge`, `gc_bigheap_probe` printed `direct=0` for every block
  with byte offset ≥0x8000 (b≥2048) + `FAIL`, while `rt` (ptr−ptr DIFFERENCE,
  via `_qbe_huge_cmp`) and `vp` (ptr COMPARE) round-tripped correctly.  So the
  failure was isolated to the `pool[off]` indexing path → `huge_ptr_binop` →
  `_qbe_huge_add`.
- **The helper was NOT the bug.**  `_qbe_huge_add` (libstub.asm) correctly
  computes `linear = seg<<4 + off + offset`, renormalises, returns seg:off.
  The bug was the OFFSET it was handed.
- **Root cause (minic.y Scale path, `prom()` ~2209):** scaling a non-`Con`
  index for a far (`l`) pointer called `sext(r)` UNCONDITIONALLY before
  `=l mul <sz>`.  `sext()` always emits `extsw` (its doc even contrasts the
  signedness-aware `widen_int_to_long()`).  An unsigned `size_t` offset ≥0x8000
  → sign-extended NEGATIVE 32-bit → `_qbe_huge_add(ptr, <negative>)` →
  addresses below the object.  Smoking-gun SSA: `%t157 =l extsw %t156` where
  `%t156 =w loadw %off` and `off` is `size_t`.

### Why compact/large were immune (and §4i never reached this)
- compact/large lower `far_ptr ± idx` to the offset-only `addfo`/`subfo` ops,
  which read ONLY arg1's low 16 bits — and `extsw`/`extuw` agree on the low 16
  bits.  So §4i deliberately left the Scale `sext` alone to keep MP
  byte-identical; the sign only matters under huge, where the FULL 32-bit
  scaled value is added to the 20-bit linear address via `_qbe_huge_add`.

### The fix (one site, gated to huge)
- In the Scale path, when `memmodel == MHuge`, widen the index with
  `widen_int_to_long(r)` (source-signedness: `extuw` unsigned, `extsw` signed)
  instead of `sext(r)`.  compact/large/near keep the `else sext(r)` branch.
- Gated to huge ⇒ compact (MP's model) is the unchanged branch ⇒ MP-compact
  byte-identical by construction.

### Gate (bug-loud) + toolchain checks
- Added `:huge` to the existing `gc_bigheap_probe` entry in `tools/test-dos.sh`
  (was compact+large).  Bug-loud verified: pre-fix huge → `direct=0`/`FAIL`;
  post-fix → `direct=0x41+i` + `ALL OK`, byte-exact vs the (model-independent)
  golden on huge, AND compact+large re-verified byte-exact.
- **test-dos 313 → 314.**  `make check` green.  Grammar conflicts UNCHANGED
  (pure C in `prom()`, no productions).
- minic.y frontend (NOT emit.c) → NO emit audit.  MP compact body EXACTLY
  **731,088 bytes**, byte-identical → codegen unchanged, NO Victor run.

### ⇒ Next session (§7h): carried tracks (no QBE bug currently open)
- far static-DATA-ptr reloc (§1g).
- Kw spill-slot sharing (frame-size lever, no consumer pain).
- Bounded aoa gap (§7e): brace-init / multi-declarator array-of-array-typedef
  still ignore `g_td_arraydim`; no realistic consumer.
- Phase-6 newlibc `serial_loopback_test` (needs NEW harness plumbing —
  channel-A polled RX + rs232a TXD→RXD loopback, move gate capture to channel
  B, RX-timing determinism); `interrupt_test` stays SKIPPED (§6v).

---

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

Older session headers (§7e and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
