# Next session (§7e — continue Phase 6 / open compiler tracks.  §7d [2026-06-13, this session] closed the carried minic front-end track **"param/static-local shadowing a global"** — a function PARAMETER or a function-local `static` whose name collided with a file-scope binding (a global variable, a declared function, an enum constant, or a different-typed outer local) died with `double definition`, even though an ordinary block local (§6a) and a multi-declarator block local (§7b/§7c) with the same collision already alpha-renamed cleanly.  **Root cause:** the §6a/§7b alpha-rename lives in `block_scope_decl()` (mint `name$N`, register a lexer rename so subsequent uses resolve to it), and every block-local rule routes through it before `varadd` — but `param()` (the ANSI parameter builder, line ~5312) and `emit_static_local()` (the function-local-static lowering, line ~1826) called `varadd()` **directly**, so a colliding param/static name hit `varadd`'s `die("double definition")` instead of shadowing.  Reduced bug-loud first: `int count; int addone(int count){return count+1;}` → `error:25: double definition`; `int count; int f(void){static int count;…}` → `double definition`; while the plain-local `int count; int f(void){int count;…}` form already compiled and renamed.  **The fix factors `block_scope_decl` into a char-buffer core `block_scope_rename(char *v, ctyp, isarray)`** (the same collision test + rename-registration + in-place buffer mutation, just operating on a name buffer instead of a `Node`; `block_scope_decl` becomes a one-line wrapper passing `node->u.v`, so all 20-odd existing callers are untouched), then routes both new sites through it: (1) `param()` reordered to `strcpy(n->u.v, v)` → `block_scope_decl(n, ctyp, 0)` → `varadd(n->u.v, ...)`, so the param-chain node carries the mangled name and the later `varget`/`bind_param` in `ansi_func_proto` resolve the renamed slot, and the registered rename makes body uses of the source name resolve to the param; (2) `emit_static_local()` computes the internal storage symbol (`_<fn>_<name>`) from the ORIGINAL source name FIRST (so the emitted global stays `$`-free for nasm), then `block_scope_rename`s a copy of the source name and registers the symtab entry + `isstaticlocal` flag under the (possibly mangled) name — uses of the source name resolve via the lexer rename to that entry, whose `glo` points at the unchanged storage symbol.  **Param-rename lifetime is correct:** params parse at `brace_depth==0` (before the body `{`), so the rename records depth 0 and is never popped by `rename_pop_closed` (`depth>brace_depth` never true) but IS cleared by the next function's `init_ansi`→`varclr()` (`renamestksp=0`) — exactly a whole-function shadow; proto-only `'(' init_ansi par0 ')'` then `varclr()` likewise clears it immediately.  Static locals sit at `brace_depth>=1` (inside the body / nested blocks) and pop at their enclosing block's close like any §6a local.  **Semantics-preserving:** `block_scope_rename` mutates/renames ONLY on a real collision — the no-collision path returns the name unchanged, so MP/stevie/the gate corpus (which contain no param-or-static-vs-global collisions) generate byte-identical code.  Grammar conflicts UNCHANGED (115 s/r, 0 r/r).  **Gated bug-loud** with a new `minic/dos/examples/param_static_shadow_probe.c` (+ `minic/dos/tests/param_static_shadow_probe.golden.txt`), the param/static counterpart to `local_shadow_probe.c`/`multi_decl_shadow_probe.c`, wired into `tools/test-dos.sh` at SMALL + MEDIUM (frontend-only / model-agnostic): (a) a param shadows a same-typed global (`addone(int count)`; global intact in `main`); (b) params shadow a different-typed global, a function name, and an enum constant simultaneously (`mix(int tag, int helper, int LIMIT)`); (c) a pointer param shadows a global, mutating the arg across a deref (`viaptr(int *count)`); (d) a `static int count` shadows the global and persists across two calls independently of it; (e) a param shadow with an inner-block re-shadow of the same name, proving rename depth/pop (`nested(int count)` → inner block uses its own slot, the param is visible again after the block).  Verified bug-loud: the UNFIXED minic (git stash + rebuild) errors `error:25: double definition` on the first `addone(int count)` param.  **test-dos 304/304 → 306/306** (the two new SMALL+MEDIUM entries `[ok]`, every prior entry unchanged; byte-exact under both models in DOSBox).  Since this is a `minic.y` frontend change (NOT i8086/emit.c), the emit-bracket audit is NOT required; the required toolchain check is the MP byte-compare, and **MP compact rebuilt to a body of EXACTLY 731,088 bytes — byte-identical to the documented golden** (image 751,664, header 20,576 + body 731,088), confirming codegen is unchanged → no Victor run needed (stevie's medium-.EXE size gate inside test-dos also still `[ok]`).  The "param/static-local shadowing a global" open track is now CLOSED.  Next: pick another carried compiler track (huge `_qbe_huge_add` ≥0x8000 §4i; far static-DATA-ptr reloc §1g; Kw spill-slot sharing; `jmp_buf bufs[6]` cross-frame longjmp §4v — unreduced, reduce first) OR resume Phase-6 newlibc gating — `serial_loopback_test` remains the only tractable bm_testhost candidate but needs real new harness plumbing (channel-A polled RX in bm_console + an rs232a TXD→RXD MAME loopback colliding with the rs232a `null_modem` capture → move the gate's serial capture to channel B, plus RX-timing determinism on the 5 MHz 8088); `interrupt_test` stays SKIPPED per §6v.)

## §7d session notes (2026-06-13)

### The bug: params and static locals bypass block_scope_decl
- The §6a/§7b alpha-rename (mint `name$N`, register a lexer rename) lives
  in `block_scope_decl()`; every block-local rule routes through it before
  `varadd`.  But `param()` (~5312) and `emit_static_local()` (~1826) called
  `varadd()` DIRECTLY, so a param/static name colliding with a global /
  function / enum / different-typed outer local hit `die("double
  definition")` instead of shadowing.
- Bug-loud reductions: `int count; int addone(int count){...}` →
  `error:25: double definition`; `int count; static int count;` in a fn →
  `double definition`.  Plain-local `int count;` in a fn already worked.

### The fix: a char-buffer rename core, routed from both sites
- Factored `block_scope_decl(Node*)` into a core
  `block_scope_rename(char *v, ctyp, isarray)` (same collision test +
  rename registration + in-place buffer mutation); `block_scope_decl` is
  now a one-line wrapper → all existing callers untouched.
- `param()`: `strcpy(n->u.v, v)` → `block_scope_decl(n, ctyp, 0)` →
  `varadd(n->u.v, ...)`.  The chain node carries the mangled name so
  `ansi_func_proto`'s `varget`/`bind_param` hit the renamed slot, and the
  registered rename resolves body uses.
- `emit_static_local()`: build the storage symbol `_<fn>_<name>` from the
  ORIGINAL name first (keeps the emitted global `$`-free for nasm), then
  `block_scope_rename` a copy of the source name and register the symtab
  entry + `isstaticlocal` under it; uses resolve via the lexer rename, and
  the entry's `glo` points at the unchanged storage symbol.

### Why the lifetimes are correct
- Params parse at `brace_depth==0` (before the body `{`) → rename recorded
  at depth 0, never popped by `rename_pop_closed`, but cleared by the next
  function's `init_ansi`→`varclr()` (`renamestksp=0`): a whole-function
  shadow.  Proto-only `'(' init_ansi par0 ')'`+`varclr()` clears it at once.
- Static locals sit at `brace_depth>=1` and pop at their enclosing block's
  close, like any §6a local.

### Why it's semantics-preserving
- `block_scope_rename` renames ONLY on a real collision; the no-collision
  path returns the name unchanged → MP/stevie/gate corpus byte-identical.
- Grammar conflicts UNCHANGED (115 s/r, 0 r/r).

### Gate (bug-loud) + toolchain checks
- `minic/dos/examples/param_static_shadow_probe.c` + golden — SMALL +
  MEDIUM (frontend-only).  Cases: (a) param shadows same-typed global;
  (b) params shadow diff-typed global / function / enum at once; (c)
  pointer param shadows a global across a deref; (d) `static int count`
  shadows + persists independently of the global; (e) param shadow + inner
  re-shadow (rename depth/pop).
- Bug-loud verified: unfixed minic → `error:25: double definition` on the
  first `addone(int count)`.
- **test-dos 304 → 306** (both new entries [ok]; byte-exact small + medium).
- minic.y/frontend change (NOT emit.c) → NO emit audit required.
- MP compact rebuilt: body EXACTLY **731,088 bytes**, byte-identical to the
  golden → codegen unchanged, NO Victor run.  stevie medium-.EXE size gate
  still [ok].

### Closed track + carried tracks
- CLOSED: "param/static-local shadowing a global" (block_scope_decl family
  §6a/§7b/§7c/§7d now complete: plain, multi-decl, array-first, param,
  static).
- Carried compiler: huge `_qbe_huge_add` >=0x8000 (§4i); far static-DATA-ptr
  reloc (§1g); Kw spill-slot sharing; `jmp_buf bufs[6]` cross-frame longjmp
  (§4v, unreduced — reduce first).
- Phase-6 newlibc: `serial_loopback_test` (only tractable bm_testhost
  candidate left, but real harness work — channel-A polled RX in bm_console
  + rs232a TXD→RXD MAME loopback colliding with the rs232a null_modem
  capture → move gate capture to channel B + RX-timing determinism);
  `interrupt_test` stays SKIPPED; display-only/`hlt`-loop tests already
  covered by hand-mirrored `bm_*` ports; newlibc-under-far-DATA-models
  (compact/large) stdio when a far-DATA consumer appears.

---

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

Older session headers (§7b and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
