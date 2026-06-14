# Next session (§7d — continue Phase 6 / open compiler tracks.  §7c [2026-06-13, this session] closed the carried minic front-end gap surfaced in §7b: a **statement-context multi-declarator whose FIRST declarator is a sized array** — `int arr[3], *counter;` appearing MID-BLOCK (after an executable statement) — was a hard `parse error`, even though the SAME declaration at function-top (`dcls` prologue) parsed fine, and the stmt-context pointer-first `int *p, n;` and follow-item `int n, *p;` forms both already parsed.  **Root cause:** the stmt-context multi-decl production was only `type IDENT ',' ext_decllist ';'` (line ~8739) — its FIRST declarator must be a bare IDENT; an array-decorated first declarator (`IDENT '[' expr ']'`) had no stmt-context production, so it fell through to `parse error`.  The `dcls`-context grammar already had the array-first form (`dcls type IDENT '[' expr ']' ',' ext_decllist ';'`, line ~8032) built from `kr_array_node()` (a 'B' node carrying name + const dim) + `emit_local_multi_decl_full()` (which handles every declarator — the 'B' array, the 'P'/plain/`[N]` followers — and already routes each through the §7b `block_scope_decl` shadow rename).  **The fix adds the missing stmt-context production** `type IDENT '[' expr ']' ',' ext_decllist ';'` mirroring the dcls rule, but DEFERS the returned initializer chain as `mkstmt(Expr, ch, 0, 0)` so a later item's initializer (`int arr[3], *q = arr;`) runs in control-flow order, matching the stmt-context multi-decl convention (the sibling `type IDENT ',' ext_decllist` rule does the same).  Inserted directly before that sibling rule, after the existing array stmt rules (`type IDENT '[' expr ']' ';'` / `… '=' '{' initlist '}' ';'`).  **No new grammar conflicts** — after `type IDENT '[' expr ']'` the lookahead disambiguates cleanly between `;`, `=`, and now `,`; count UNCHANGED (115 s/r, 0 r/r, 10-never-reduced baseline, verified post-rebuild).  **Semantics-preserving:** the production only fires on token sequences that previously had NO valid parse, so every input that already parsed produces an identical AST; the array-first 'B' item and its pointer/scalar followers each route through `block_scope_decl` exactly like §7b, so an array-first item shadowing a global (`int counter[2], n;` next to a global `int counter`) is alpha-renamed (`counter$1`) rather than colliding.  **Gated bug-loud** with a new `minic/dos/examples/arrayfirst_multidecl_probe.c` (+ `minic/dos/tests/arrayfirst_multidecl_probe.golden.txt`), wired into `tools/test-dos.sh` at SMALL + MEDIUM (frontend-only / model-agnostic, like its §7b sibling): three MID-BLOCK cases — (a) plain `int arr[3], *p;` with `p = arr` then `p[0..2]`; (b) `int counter[2], n = 7;` where the array-first item shadows a same-named global (proves the 'B'-path rename + the deferred later-item init) and the global is intact afterward; (c) `int vals[2], *q = vals;` exercising a pointer-second WITH initializer plus a `*q` deref — each forced into statement context by a preceding `touch = …;` so it cannot fall into the dcls prologue.  Verified bug-loud: the UNFIXED minic (git stash + rebuild) errors `error:32: parse error` on the first array-first mid-block line; output is byte-exact vs the golden under BOTH small and medium in DOSBox (`a=15 / b=98 / 100 / c=48`).  **test-dos 302/302 → 304/304** (the two new SMALL+MEDIUM entries `[ok]`, every prior entry unchanged).  Since this is a `minic.y` grammar/frontend change (NOT i8086/emit.c), the emit-bracket audit is NOT required; the required toolchain check is the MP byte-compare, and **MP compact rebuilt to a body of EXACTLY 731,088 bytes — byte-identical to the documented golden** (image 751,664, header 20,576 + body 731,088), confirming codegen is unchanged → no Victor run needed (stevie's medium-.EXE size gate inside test-dos also still `[ok]`).  The "stmt-context array-first multi-decl grammar gap" open track is now CLOSED.  Next: pick another carried compiler track (huge `_qbe_huge_add` ≥0x8000 §4i; far static-DATA-ptr reloc §1g; param/static-local shadowing a global — same `block_scope_decl` family as §7b/§7c, needs a reduced bug-loud repro first; Kw spill-slot sharing; `jmp_buf bufs[6]` cross-frame longjmp §4v — unreduced, reduce first) OR resume Phase-6 newlibc gating — `serial_loopback_test` remains the only tractable bm_testhost candidate but needs real new harness plumbing (channel-A polled RX in bm_console + an rs232a TXD→RXD MAME loopback device colliding with the rs232a `null_modem` capture → move the gate's serial capture to channel B, plus RX-timing determinism on the 5 MHz 8088); `interrupt_test` stays SKIPPED per §6v.)

## §7c session notes (2026-06-13)

### The gap: stmt-context array-first multi-decl did not parse
- The stmt-context multi-decl production was only
  `type IDENT ',' ext_decllist ';'` — FIRST declarator must be a bare
  IDENT.  An array-decorated first declarator (`int arr[3], *p;`) MID-BLOCK
  (after an executable statement, so it can't fall into the `dcls`
  prologue) had no production → `parse error`.
- The SAME form at function-top already parsed via the dcls rule
  `dcls type IDENT '[' expr ']' ',' ext_decllist ';'` (kr_array_node +
  emit_local_multi_decl_full).  Pointer-first `int *p, n;` and follow-item
  `int n, *p;` also already parsed; only array-FIRST stmt-context was gone.
- Bug-loud reduction: a mid-block `int arr[3], *counter;` → `parse error`.

### The fix: add the stmt-context array-first production
- New rule `type IDENT '[' expr ']' ',' ext_decllist ';'`, mirroring the
  dcls array-first rule: `first = kr_array_node($2->u.v, const_eval($4));
  first->r = $7; ch = emit_local_multi_decl_full($1, first);` — but DEFERS
  the init chain as `mkstmt(Expr, ch, 0, 0)` so a later item's initializer
  (`int arr[3], *q = arr;`) runs in control-flow order (the stmt-context
  multi-decl convention; the sibling plain rule does the same).
- emit_local_multi_decl_full already routes every declarator (the 'B'
  array, 'P'/plain/'[N]' followers) through the §7b `block_scope_decl`
  shadow rename — so an array-first item shadowing a global is alpha-renamed
  (`int counter[2], n;` → `counter$1`), not a collision.

### Why it's safe
- Fires ONLY on token sequences that previously had no valid parse → every
  already-parsing input yields an identical AST.
- Grammar conflicts UNCHANGED (115 s/r, 0 r/r, 10-never-reduced baseline;
  after `type IDENT '[' expr ']'` the `;`/`=`/`,` lookahead disambiguates).

### Gate (bug-loud) + toolchain checks
- `minic/dos/examples/arrayfirst_multidecl_probe.c` + golden — SMALL +
  MEDIUM (frontend-only, model-agnostic).  Three MID-BLOCK cases (each
  forced past the dcls prologue by a preceding statement): (a) plain
  `int arr[3], *p;`; (b) `int counter[2], n = 7;` array-first item shadows
  a same-named global (rename + deferred later-item init; global intact);
  (c) `int vals[2], *q = vals;` pointer-second WITH init + `*q` deref.
- Bug-loud verified: unfixed minic (stash+rebuild) → `error:32: parse
  error` on the first array-first mid-block line.
- **test-dos 302 → 304** (both new entries [ok]; byte-exact `a=15 / b=98 /
  100 / c=48` under small AND medium).
- minic.y/frontend change (NOT emit.c) → NO emit audit required.
- MP compact rebuilt: body EXACTLY **731,088 bytes**, byte-identical to the
  golden → codegen unchanged, NO Victor run.  stevie medium-.EXE size gate
  (inside test-dos) still [ok].

### Closed track + carried tracks
- CLOSED: "stmt-context array-first multi-decl grammar gap" (surfaced §7b).
- Carried compiler: huge `_qbe_huge_add` >=0x8000 (§4i); far static-DATA-ptr
  reloc (§1g); param/static-local shadowing a global (same block_scope_decl
  family as §7b/§7c — reduce a bug-loud repro first); Kw spill-slot sharing;
  `jmp_buf bufs[6]` cross-frame longjmp (§4v, unreduced — reduce first).
- Phase-6 newlibc: `serial_loopback_test` (only tractable bm_testhost
  candidate left, but real harness work — channel-A polled RX in bm_console
  + rs232a TXD→RXD MAME loopback colliding with the rs232a null_modem
  capture → move gate capture to channel B + RX-timing determinism);
  `interrupt_test` stays SKIPPED; display-only/`hlt`-loop tests already
  covered by hand-mirrored `bm_*` ports; newlibc-under-far-DATA-models
  (compact/large) stdio when a far-DATA consumer appears.

---

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

Older session headers (§7a and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
