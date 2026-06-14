# Next session (§7f — continue Phase 6 / open compiler tracks.  §7e [2026-06-13, this session] reduced AND fixed the carried **`jmp_buf bufs[6]` cross-frame longjmp** track (§4v, unreduced for many sessions) — the user picked it.  **Reduction (bug-loud):** `jmp_buf` is `int[8]`, so `jmp_buf bufs[N]` is an array whose ELEMENT is itself an array typedef.  A recursive probe that set `bufs[0..5]` then `longjmp(bufs[target], …)` with a runtime `target=2` resumed the WRONG frame (`caught 5`, the deepest, instead of `caught 2`); a stride probe showed `&bufs[i]-&bufs[0]==0` for every i, and the generated `data` block was sized **12 bytes for N=6, not 96**.  **Root cause:** minic's flat type system can't represent `int (*)[8]`, and EVERY array-declarator rule ignored the typedef's inner dimension (`g_td_arraydim`): `bufs[N]` was sized as `int[N]` and a subscript `bufs[i]` was lowered as a SCALAR-int access — `@(bufs + i*sizeof(int))`, i.e. stride 2 **and a value load** — instead of the row ADDRESS `bufs + i*16`.  So every `setjmp(bufs[i])` aliased `bufs[0]` (last writer = the deepest frame) and the cross-frame `longjmp` resumed it.  (minic has NO true 2-D arrays at all — `int x[6][8]` is a hard parse error — so an array-typedef element is the only door into this shape, and nothing in MP/stevie/the corpus uses it, which is why it sat latent.)  **The fix** adds a `varh.aoa_dim` flag (the inner dimension D) set at the three array-of-array-typedef declaration sites — file-scope global (`'[' expr ']' ';'`), block-local (`dcls type IDENT '[' expr ']' ';'`), and function-local static (`STATIC type IDENT '[' expr ']' ';'`) — each of which, when `g_td_arraydim > 0`, now registers the variable as `IDIR(g_td_arrayelem)` (e.g. `int*`) with the CORRECT `N*D*sizeof(elem)` byte size (and `iralign(elem)`).  Then `mkidx()` desugars a one-level subscript on an aoa variable to the **bare pointer add `bufs + (i*D)` with NO deref** (instead of the normal `@(bufs + i)`): the existing `'+'` Scale path multiplies by `sizeof(elem)`, giving byte offset `i*D*sizeof(elem)` = the `int*` row address — which reuses `far_ptr_offset_binop` for free under compact/large, and COMPOSES naturally (`bufs[i][j]` takes the ordinary `@(+ . j)` path on the resulting `int*`, and `setjmp(bufs[i])` gets the row pointer it wants).  The new `mkidx` branch only fires when `var_aoa_dim(name) > 0`, so all non-aoa code is byte-identical.  **Semantics-preserving:** the aoa path is a brand-new construct (previously miscompiled), and the non-aoa else-branches were left textually identical (the block-local rule keeps `v = $3->u.v`, no new `block_scope_decl` routing) → MP/stevie/the gate corpus generate byte-identical code.  Grammar conflicts UNCHANGED (115 s/r, 0 r/r, 10 never-reduced — only C action-body code + helpers + a `varh` field changed, no productions).  **A note for whoever revisits:** the QBE `assoccon` ABORT (`gvn.c:210`) that the row-to-row pointer-subtraction stride diagnostic hit is a **PRE-EXISTING, UNRELATED QBE bug** — it reproduces on `(char*)&a[i]-(char*)&a[0]` for a plain `int a[6]` too (nested const-mul + i8086 `l`/`w` ptrdiff class mix), and is NOT triggered by any realistic setjmp-array usage; left untouched.  **Gated bug-loud** with a new `minic/dos/examples/arr_jmpbuf_probe.c` (+ `minic/dos/tests/arr_jmpbuf_probe.golden.txt`), the array-of-jmp_buf counterpart to `setjmp_probe.c`, wired into `tools/test-dos.sh` at MEDIUM + COMPACT + LARGE (matching setjmp_probe; model-independent output): case A cross-frame `longjmp(bufs[target])` into a runtime-indexed FILE-SCOPE array (`caught 2` then unwinds 1,0); case B a BLOCK-LOCAL `jmp_buf lb[3]` runtime-indexed in-frame; case D a FUNCTION-LOCAL STATIC `static jmp_buf sb[3]` runtime-indexed; case C a composing double-subscript `dd[i][j]` write/read over a `typedef int[8]` element.  Verified bug-loud: the UNFIXED minic (git stash + rebuild) prints `caught 5` and `dd0_0=30` (the alias + stride corruption); the fixed minic is byte-exact vs the golden under ALL THREE models in DOSBox, and the pre-existing `setjmp_probe` stays byte-identical on medium/compact/large.  **test-dos 306/306 → 309/309** (the three new entries `[ok]`, every prior entry unchanged).  Since this is a `minic.y` frontend change (NOT i8086/emit.c), the emit-bracket audit is NOT required; the required toolchain check is the MP byte-compare, and **MP compact rebuilt to a body of EXACTLY 731,088 bytes — byte-identical to the documented golden** (image 751,664, header 20,576 + body 731,088), confirming codegen is unchanged → no Victor run needed (stevie's medium-.EXE size gate inside test-dos also still `[ok]`); `make check` green.  The "`jmp_buf bufs[6]` cross-frame longjmp (§4v)" open track is now CLOSED.  **One bounded gap remains** (documented, not a regression — the pre-existing status quo for those forms): array-of-array-typedef in a brace-INITIALIZED (`jmp_buf x[2] = {…}`, nonsensical for jmp_buf) or MULTI-declarator (`jmp_buf a[2], b[2]`) declaration still ignores `g_td_arraydim` and would miscompile; no realistic consumer uses them, and they'd need the same `aoa_dim` treatment if one ever appears.  Next: pick another carried compiler track (huge `_qbe_huge_add` ≥0x8000 §4i — pure i8086 backend, hardest, needs emit audit; far static-DATA-ptr reloc §1g; Kw spill-slot sharing — size lever, no consumer pain; the bounded aoa init/multi-decl gap above; the pre-existing QBE `assoccon` ptrdiff abort surfaced this session) OR resume Phase-6 newlibc gating — `serial_loopback_test` remains the only tractable bm_testhost candidate but needs real new harness plumbing (channel-A polled RX in bm_console + an rs232a TXD→RXD MAME loopback colliding with the rs232a `null_modem` capture → move the gate's serial capture to channel B, plus RX-timing determinism on the 5 MHz 8088); `interrupt_test` stays SKIPPED per §6v.)

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

### Caveats for next time
- The QBE `assoccon` abort (`gvn.c:210`) my stride diagnostic hit is a
  PRE-EXISTING, UNRELATED QBE bug: it reproduces on `(char*)&a[i]-(char*)&a[0]`
  for a plain `int a[6]` too.  Not from this change; left untouched.
- Bounded aoa gap left (not a regression): brace-initialized
  (`jmp_buf x[2]={…}`) and multi-declarator (`jmp_buf a[2], b[2]`)
  array-of-array-typedef declarations still ignore `g_td_arraydim` and would
  miscompile.  No realistic consumer; same `aoa_dim` treatment if one appears.

### Closed track + carried tracks
- CLOSED: "`jmp_buf bufs[6]` cross-frame longjmp" (§4v).
- Carried compiler: huge `_qbe_huge_add` ≥0x8000 (§4i, backend, needs emit
  audit); far static-DATA-ptr reloc (§1g); Kw spill-slot sharing; the bounded
  aoa init/multi-decl gap above; the pre-existing QBE `assoccon` ptrdiff abort.
- Phase-6 newlibc: `serial_loopback_test` (only tractable bm_testhost
  candidate, needs harness work — channel-A polled RX + rs232a TXD→RXD
  loopback, move gate capture to channel B, RX-timing determinism);
  `interrupt_test` stays SKIPPED per §6v.

---

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

Older session headers (§7c and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
