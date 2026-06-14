# Next session (§7c — continue Phase 6 / open compiler tracks.  §7b [2026-06-13, this session] fixed the carried minic front-end track **"multi-decl items after the first skip `block_scope_decl` (loud 'double definition')"** — a block-scope local declared through a MULTI-declarator list (`T a, b, c;`) that shadowed a global / declared function / enum constant / different-typed outer local died with `double definition`, whereas the SINGLE-declarator equivalent (`T a; T b;`) compiled fine.  **Root cause:** the §6a/§1k inner-block alpha-rename lives in `block_scope_decl()` (it mints a unique `name$N` and registers a rename so subsequent uses resolve to it), and every SINGLE-decl `dcls`/stmt rule routes its declarator through it before `varadd` — but the multi-declarator helpers `emit_local_multi_decl()` / `emit_local_multi_decl_full()` (and the `type IDENT '=' expr ',' init_decllist` first-has-init rule's tail loop) called `varadd()` **directly**, so EVERY declarator in a comma list (the first item included — the track note's "after the first" was imprecise; the whole list path skipped it) bypassed the rename and a colliding name hit `varadd`'s `die("double definition")`.  Reduced bug-loud first: `int count; int main(){ int count, total; … }` → `error:2: double definition`, while the single-decl `int count; int total;` form compiled and renamed `count`→`count$1`.  **The fix routes each storage-allocating declarator through `block_scope_decl` in all three sites:** (1) `emit_local_multi_decl` — signature changed from `char *first` to `Node *firstnode` (two call sites updated from `$N->u.v` to `$N`) so the first item can be renamed in place, plus `block_scope_decl(n, t, isarray)` before `varadd` for each `'B'`/`'P'`/plain/`'A'` loop item (re-reading the possibly-renamed `v` after, so the alloc, `varadd`, and `multi_decl_chain_init` all target the renamed slot); (2) `emit_local_multi_decl_full` (decorated-first forms — `int a[5], b;` at function top) — same per-item rename; (3) the `int a = 1, b = 2;`-in-a-block rule's `init_decllist` loop.  Function-prototype items (`op=='F'`/`'G'`, e.g. `char *initstr, *getenv();`) keep their direct `varadd(v,1,FUNC,0)` — those register functions, not storage, and a same-typed re-proto is already accepted.  **Semantics-preserving for all currently-compiling code:** the only cases `block_scope_decl` newly renames are exactly the ones `varadd` previously KILLED (different-typed local collision, or any global/extern/function/enum collision) — so MP/stevie/the gate corpus, which compile today, contain no such multi-decls and are byte-identical; same-typed sibling-block re-declaration still folds to one slot (block_scope_decl returns the name unchanged → `varadd`'s same-type rebind path), matching the single-decl behavior.  Grammar conflicts UNCHANGED (115 s/r, 0 r/r, 10-never-reduced baseline).  **Gated bug-loud** with a new `minic/dos/examples/multi_decl_shadow_probe.c` (+ golden), the multi-decl counterpart to the single-decl `local_shadow_probe.c`, wired into `tools/test-dos.sh` at SMALL + MEDIUM (frontend-only / model-agnostic, like its sibling): (a) a multi-decl whose FIRST item shadows a same-typed global and later items shadow a different-typed global / a function / an enum constant; (b) an inner-block `char v, w;` shadowing a different-typed outer `long v` (outer survives the block via deferred rename-pop); (c) the `int gflag = 2, q = 3;` first-has-init form where an item shadows a global; (d) a pointer-decorated `int *counter, n;` shadowing a global, used across a deref — each prints values proving the inner names rebind correctly AND the shadowed global/function/enum is untouched afterward.  Verified bug-loud: the UNFIXED minic (git stash + rebuild) errors `error:37: double definition` on the first `int counter, x;` line; the array-first stmt-scope form `int arr[3], *counter;` does NOT parse (a SEPARATE pre-existing grammar gap — no stmt-context array-first multi-decl production — left untouched and out of scope).  **test-dos 300/300 → 302/302** (the two new SMALL+MEDIUM entries `[ok]`, every prior entry unchanged).  Since this is a `minic.y` grammar/frontend change (NOT i8086/emit.c), the emit-bracket audit is NOT required; the required toolchain check is the MP byte-compare, and **MP compact rebuilt to a body of EXACTLY 731,088 bytes — byte-identical to the documented golden** (image 751,664, header 20,576 + body 731,088), confirming codegen is unchanged → no Victor run needed (stevie's medium-.EXE size gate inside test-dos also still `[ok]`).  The "multi-decl items after the first skip `block_scope_decl`" open track is now CLOSED.  Next: pick another carried compiler track (huge `_qbe_huge_add` ≥0x8000 §4i; far static-DATA-ptr reloc §1g; param/static-local shadowing a global; Kw spill-slot sharing; `jmp_buf bufs[6]` cross-frame longjmp §4v — unreduced, reduce first; the stmt-context array-first multi-decl grammar gap surfaced this session) OR resume Phase-6 newlibc gating — `serial_loopback_test` remains the only tractable bm_testhost candidate but needs real new harness plumbing (channel-A polled RX in bm_console + an rs232a TXD→RXD MAME loopback device that collides with the rs232a `null_modem` capture, so the gate's serial capture must move to channel B, plus RX-timing determinism on the 5 MHz 8088); `interrupt_test` stays SKIPPED per §6v.)

## §7b session notes (2026-06-13)

### The bug: multi-declarator locals bypass block_scope_decl
- The inner-block alpha-rename (§6a/§1k) lives in `block_scope_decl()`:
  it mints `name$N` and registers a rename for a declarator that collides
  with a global / extern / function / enum constant, or a different-typed
  outer local.  Every SINGLE-decl rule routes through it before `varadd`.
- The MULTI-declarator helpers `emit_local_multi_decl` /
  `emit_local_multi_decl_full`, and the `int a=1, b=2;` first-has-init
  rule's `init_decllist` loop, called `varadd()` DIRECTLY — so EVERY item
  in a comma list (the first included) skipped the rename and a colliding
  name hit `varadd`'s `die("double definition")`.
- Bug-loud reduction: `int count; int main(){ int count, total; ... }`
  → `error:2: double definition`; the single-decl `int count; int total;`
  form compiled (renamed `count`→`count$1`).

### The fix: route every storage declarator through block_scope_decl
- `emit_local_multi_decl`: signature `char *first` → `Node *firstnode`
  (two call sites updated `$N->u.v` → `$N`) so the FIRST item renames in
  place; `block_scope_decl(n, t, isarray)` before `varadd` for each
  `'B'`/`'P'`/plain/`'A'` loop item, re-reading the renamed `v` so the
  alloc, varadd, and multi_decl_chain_init all hit the renamed slot.
- `emit_local_multi_decl_full`: same per-item rename (covers the
  decorated-first `int a[5], b;` function-top forms + the dcls
  array/func-first rules that build a `first` node).
- `type IDENT '=' expr ',' init_decllist ';'` rule: its tail loop over
  `init_decllist` now renames each item too (the first already did).
- Function-PROTOTYPE items (`op=='F'`/`'G'`) keep direct
  `varadd(v,1,FUNC,0)` — they register functions not storage; renaming
  one would break calls to it, and same-typed re-proto is already OK.

### Why it's semantics-preserving
- `block_scope_decl` newly renames ONLY the cases `varadd` previously
  KILLED (different-typed local collision, or any global/extern/function/
  enum collision).  Code that compiles today has no such multi-decls, so
  MP/stevie/gate corpus are byte-identical.
- Same-typed sibling-block re-decl still folds to one slot
  (block_scope_decl returns the name unchanged → varadd's rebind path),
  matching single-decl behavior.
- Grammar conflicts UNCHANGED (115 s/r, 0 r/r, 10-never-reduced baseline).

### Gate (bug-loud) + toolchain checks
- `minic/dos/examples/multi_decl_shadow_probe.c` + golden — the multi-decl
  counterpart to `local_shadow_probe.c`; SMALL + MEDIUM (frontend-only,
  model-agnostic).  Cases: (a) first item shadows same-typed global +
  later items shadow different-typed global / function / enum; (b)
  inner-block `char v,w;` over a `long v` outer (outer survives); (c)
  `int gflag=2, q=3;` first-has-init shadowing a global; (d)
  `int *counter, n;` pointer-decorated shadow used across a deref.
- Bug-loud verified: unfixed minic (stash+rebuild) → `error:37: double
  definition` on the first `int counter, x;` line.
- **test-dos 300 → 302** (both new entries [ok], all prior unchanged).
- minic.y/frontend change (NOT emit.c) → NO emit audit required.
- MP compact rebuilt: body EXACTLY **731,088 bytes**, byte-identical to
  the golden → codegen unchanged, NO Victor run.  stevie medium-.EXE
  size gate (inside test-dos) still [ok].

### Closed track + a newly-surfaced gap
- CLOSED: "multi-decl items after the first skip block_scope_decl".
- NOTED (separate, pre-existing, out of scope): the array-first
  stmt-context multi-decl `int arr[3], *counter;` does NOT parse — there
  is no stmt-context array-first multi-decl production (pointer-first
  `int *p, n;` and follow-item `int n, *p;` both parse fine).

### Open tracks (carried)
- Compiler: huge `_qbe_huge_add` >=0x8000 (§4i); far static-DATA-ptr
  reloc (§1g); param/static-local shadowing a global; Kw spill-slot
  sharing; `jmp_buf bufs[6]` cross-frame longjmp (§4v, unreduced —
  reduce first); stmt-context array-first multi-decl grammar gap (new).
- Phase-6 newlibc: `serial_loopback_test` (only tractable bm_testhost
  candidate left, but real harness work — channel-A polled RX in
  bm_console + rs232a TXD→RXD MAME loopback colliding with the rs232a
  null_modem capture → move gate capture to channel B + RX-timing
  determinism); `interrupt_test` stays SKIPPED; display-only/`hlt`-loop
  tests already covered by hand-mirrored `bm_*` ports; newlibc-under-
  far-DATA-models (compact/large) stdio when a far-DATA consumer appears.

---

# Next session (§7b — continue Phase 6 / open compiler tracks.  §7a [2026-06-13, this session] implemented **near (small/tiny-model) `setjmp`/`longjmp`** — the carried "small setjmp/longjmp (newlibc may want it)" open track, chosen by the user.  Until now the small `.EXE` model had NO setjmp at all: `tools/libstub_to_exe.py`'s `build_epilogue()` DROPPED `SETJMP_EXE` for near-code models (tiny/small) because that helper is structurally FAR — its `jmp_buf` saves a 4-byte CS:IP return address and `longjmp` exits via `retf` — and it CANNOT be produced by `unfar_epilogue()` (the `retf→ret` / `[bp+N≥6]−2` reverse transform the other EXE epilogue blocks use), because that transform drops 2 from EVERY `[bp+N≥6]`, which would silently corrupt the `jmp_buf` INTERNAL offsets `[bx+10]`/`[bx+12]` along with the call-frame offsets.  So **any small-model program that referenced `setjmp`/`longjmp` failed to LINK** — confirmed bug-loud: `tools/build-example.sh --model=small minic/dos/examples/setjmp_probe.c` → `omf_link: error: undefined symbols: _setjmp, _longjmp`.  **The fix is a new hand-written `NEAR_SETJMP_EXE` string** in `libstub_to_exe.py`, mirroring the proven medium `SETJMP_EXE` with the CS word removed: a near `call` pushes only a 2-byte return IP, so the frame at setjmp entry is `[bp+0]` saved BP / `[bp+2]` ret IP / `[bp+4]` env (one word lower than the far form's `[bp+6]` after the extra CS word), the caller's resume SP is `lea [bp+4]`, the `jmp_buf` is 6 words (`[0]` BP, `[2]` resume SP, `[4]` SI, `[6]` DI, `[8]` BX, `[10]` ret IP — NO CS word; the C `jmp_buf` is `int[8]`=16 B so `[12]`/`[14]` stay spare), and `longjmp` restores SP, pushes the IP only, and exits via a near `ret` (vs the far form's push-CS+IP / `retf`).  Near-data (DS==SS) reaches a stack-allocated env via DS:BX — no ES involved (so it is simpler than even the medium near-DATA `SETJMP_EXE`, which still used the far call ABI).  It is authored directly in near ABI / `segment _TEXT` and appended **raw** to the `near_code_model` branch of `build_epilogue()` (NOT through `unfar_epilogue`, precisely to avoid the `[bx+N]` corruption described above).  **The two existing setjmp probes were reused as the gate** — both are model-independent (program output only), so no new probe/golden was authored: `setjmp_probe.c` (case 1 direct=0, case 2 `longjmp(env,7)`, case 3 the C `0→1` fixup, cases 4/5 a DEEP 3-frame nested unwind via an NLR clone + callee-saved BX/SI/DI/BP guard restore, case 6 chained-buffer NLR popping to the right level) and `setjmp_clobber_probe.c` (the `calls_setjmp()`-forces-AEsc guard: a local modified AFTER setjmp must survive the longjmp).  Both build and run **byte-exact vs their existing goldens under small** in DOSBox — proving the resume-SP arithmetic, the near `ret` target, and the callee-saved-register save/restore are all correct.  Wired `:small` entries for both into `tools/test-dos.sh` (alongside their existing medium/compact/large entries): **test-dos 298 → 300, all [ok]**.  This is a `libstub_to_exe.py` (toolchain) change, NOT an i8086/emit.c change, so per house rules **no emit-bracket audit was required**; the required check is the MP byte-compare, and **MP compact rebuilt to a body of EXACTLY 731,088 bytes — byte-identical to the documented golden** (image 751,664, header 20,576 + body 731,088) — as expected since the change touches ONLY the `near_code` branch and MP builds compact (far-data), which never hits it → codegen unchanged, no Victor run needed.  The track note "small-model setjmp/longjmp — only if a small-model consumer needs it (newlibc may)" is now CLOSED: the capability exists and is gated; if/when a small-model newlibc consumer appears (e.g. an NLR-using test that fits the 64 KB single-`_TEXT` ceiling), `setjmp`/`longjmp` resolve by real name (near-data models do not `far_stdlib`-mangle, so minic calls `setjmp`→asm `_setjmp` directly).  Next: pick another carried compiler track (huge `_qbe_huge_add` ≥0x8000 §4i; multi-decl items after the first skip `block_scope_decl`; far static-DATA-ptr reloc §1g; param/static-local shadowing a global; Kw spill-slot sharing; `jmp_buf bufs[6]` cross-frame longjmp §4v — unreduced, reduce first) OR resume Phase-6 newlibc gating (`serial_loopback_test` is the only remaining tractable bm_testhost candidate but needs real new harness plumbing — an rs232a TXD→RXD loopback attach distinct from the §6e `V9K_SERIAL_IN` rs232b null_modem, gate serial capture moved to channel B, plus RX-timing determinism on the 5 MHz 8088; `interrupt_test` stays SKIPPED per §6v).)

## §7a session notes (2026-06-13)

### The gap: small/tiny model had no setjmp at all (link failure)
- `libstub_to_exe.py build_epilogue()` DROPPED `SETJMP_EXE` for near-code
  models (tiny/small) — it is structurally FAR (jmp_buf saves 4-byte CS:IP,
  longjmp exits via `retf`) and `unfar_epilogue()` CANNOT convert it: that
  transform drops 2 from EVERY `[bp+N>=6]`, which would corrupt the jmp_buf
  INTERNAL offsets `[bx+10]`/`[bx+12]` along with the frame offsets.
- So any small-model program referencing setjmp/longjmp failed to LINK.
  Bug-loud confirmed: `build-example.sh --model=small setjmp_probe.c` →
  `omf_link: error: undefined symbols: _setjmp, _longjmp`.

### The fix: hand-written NEAR_SETJMP_EXE (libstub_to_exe.py)
- Mirrors the medium `SETJMP_EXE` with the CS word removed (near `call`
  pushes only a 2-byte IP):
    - frame at setjmp entry: `[bp+0]` BP, `[bp+2]` ret IP, `[bp+4]` env
      (one word lower than the far `[bp+6]`); resume SP = `lea [bp+4]`.
    - jmp_buf (6 words): `[0]` BP, `[2]` resume SP, `[4]` SI, `[6]` DI,
      `[8]` BX, `[10]` ret IP — NO CS word (C jmp_buf is int[8]=16B, so
      `[12]`/`[14]` spare).
    - longjmp: restore SP, push IP only, near `ret` (vs far push-CS+IP /
      `retf`).  Near-data DS==SS reaches env via DS:BX — no ES.
- Authored directly in near ABI / `segment _TEXT`, appended RAW to the
  `near_code_model` branch of `build_epilogue()` (NOT via `unfar_epilogue`,
  to avoid the `[bx+N]` corruption above).

### Gate (bug-loud) + toolchain checks
- Reused the two existing model-independent setjmp probes (no new
  probe/golden): `setjmp_probe.c` (direct=0, val=7, 0->1 fixup, deep 3-frame
  nested unwind + callee-saved guard restore, chained-buffer NLR pop) and
  `setjmp_clobber_probe.c` (calls_setjmp AEsc guard).  Both byte-exact vs
  their goldens under small in DOSBox.
- Wired `:small` entries for both into `tools/test-dos.sh`.
- **test-dos 298 -> 300** (both new small entries [ok], all prior unchanged).
- `libstub_to_exe.py` change (NOT emit.c) -> NO emit audit required.
- MP compact rebuilt: body EXACTLY **731,088 bytes**, byte-identical to the
  documented golden (only the `near_code` branch changed; MP is
  compact/far-data) -> codegen unchanged, NO Victor run.

### Closed track
- "small-model setjmp/longjmp (newlibc may want it)" is CLOSED: capability
  exists + gated.  Near-data models don't `far_stdlib`-mangle, so a future
  small-model newlibc consumer calls `setjmp`->asm `_setjmp` by real name.

### Open tracks (carried)
- Compiler: huge `_qbe_huge_add` >=0x8000 (§4i); multi-decl items after the
  first skip `block_scope_decl`; far static-DATA-ptr reloc (§1g);
  param/static-local shadowing a global; Kw spill-slot sharing; `jmp_buf
  bufs[6]` cross-frame longjmp (§4v, unreduced — reduce first).
- Phase-6 newlibc: `serial_loopback_test` (only tractable bm_testhost
  candidate left, but real harness work — channel-A polled RX in bm_console +
  rs232a TXD→RXD MAME loopback device colliding with the rs232a null_modem
  capture → move gate capture to channel B + RX-timing determinism);
  `interrupt_test` stays SKIPPED; display-only/`hlt`-loop tests already
  covered by hand-mirrored `bm_*` ports; newlibc-under-far-DATA-models
  (compact/large) stdio when a far-DATA consumer appears.

---

Older session headers (§6z and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
