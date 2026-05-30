# Next session — MicroPython port: MicroPython LINKS but HANGS — DGROUP is too small; move static data to far segments (post §1r)

> **§1r — medium-model `setjmp`/`longjmp` landed; MicroPython now LINKS to a
> complete `mpython.exe`, but it HANGS at runtime.**  The setjmp/longjmp link
> blocker is CLOSED and runtime-verified by a real NLR round-trip probe.  The
> link advanced through it (and through the next wall) to produce — for the
> first time — a complete MicroPython .EXE.  The new frontier is a RUNTIME hang
> rooted in the medium model's single 64 KB DGROUP.
>
> **setjmp/longjmp (so you don't redo it):** new `SETJMP_EXE` in
> `tools/libstub_to_exe.py` (added to `build_epilogue`, unconditional), written
> directly in FAR form (4-byte CS:IP, `retf`; longjmp `mov sp,[bx+2]` + push
> CS:IP + `retf` synthesizes the far jump).  `jmp_buf` is `int[8]`; 7 words used:
> [0] caller BP, [2] resume SP (= setjmp's `bp+6`; the i8086 ABI passes args in
> caller-reserved slots and does NOT clean them, so resume SP == caller SP just
> before `call far`), [4] SI, [6] DI, [8] caller BX, [10] ret IP, [12] ret CS.
> New `minic/include/setjmp.h`.  **The bug that bit:** the first cut clobbered
> **BX** (used as the env pointer) without restoring it — BX is callee-saved
> here (qbe puts locals in BX/SI/DI), so a 2nd setjmp whose env arg lived in BX
> (`nlr_push(&middle)` right after `nlr_push(&outer)`) got a garbage pointer →
> wild longjmp → nondeterministic hang/crash.  Fix: `mov bx, dx` restore before
> `pop bp; retf`.  Probe `setjmp_probe.c` + golden (gate, **medium-only** — the
> far helper reads a 2-byte near env ptr): real nlr_buf_t chain, nlr_push=setjmp,
> nlr_jump=longjmp; covers direct=0, val, 0→1, deep 3-frame unwind, callee-saved
> guard survival, chained-buffer pop.  Gate **130→131**, `make check` green, no
> minic/qbe change (111 s/r 0 r/r unchanged).  See [[minic-setjmp-longjmp]].
>
> **THE new blocker — 64 KB DGROUP overflow (medium model).**  At the default
> port config (`MICROPY_HEAP_SIZE`=24576, `--stack-size 8192`) the link fails:
> `DGROUP + stack overflows 64KB (sp=87184)`.  MicroPython's static data (qstr
> pools, ROM const tables, mp_state BSS) is ~55 KB, and in the medium model
> _DATA + BSS + heap + stack ALL share one 64 KB DGROUP.  I confirmed shrinking
> to `MICROPY_HEAP_SIZE`=7168 + `--stack-size 3072` DOES link →
> `build/mp-link/mpython.exe` (452 KB; 108 modules; 370 KB far code across many
> segments; 61.5 KB data) — but it then **HANGS at runtime** (only ~4 KB DGROUP
> left for the stack ⇒ near-certain parser/compiler stack-starvation; could also
> be a codegen bug only this large multi-segment binary exercises).  I reverted
> both shrinks (they don't yield a working binary; the default config is the
> honest signal).
>
> **The real fix is NOT shrinking — it's getting MicroPython's static data OUT
> of DGROUP.**  Two paths:
> 1. **Build the MicroPython subset under the far-data model (compact or
>    large).**  Then _DATA pointers are 4-byte far and the linker can place
>    const/ROM tables in their own far segments, freeing DGROUP for heap+stack.
>    This is the architecturally-correct path and reuses the existing
>    `_far_X` libstub family + `far_stdlib[]` mangling.  Cost: every TU
>    recompiled `-m compact/large`; setjmp/longjmp needs a far-data variant
>    (4-byte env ptr + ES) — write a `FAR_SETJMP_EXE` gated by
>    `far_data_model(model)` (mirror how FAR_STDIO_EXE is gated).  qstr ROM
>    tables and `MP_ROM_*` const pools are the bulk to relocate.
> 2. **Aggressive data reduction** (smaller `MICROPY_CONFIG`: fewer builtins,
>    smaller qstr set, `MICROPY_ENABLE_COMPILER` trimmed) to get static data
>    well under ~50 KB so heap+stack fit in medium.  Cheaper to try first as a
>    smoke test, but a dead end for any real program.
>
> Milestone unchanged: `print(1+2)` → `3` in DOSBox (Phase 4).  `main.c` already
> does `do_str("print(1+2)", MP_PARSE_SINGLE_INPUT)`.  `gc_collect` is still a
> no-scan STUB (needs a real stack scan, now that setjmp works it can spill
> callee-saved regs) and `alloca` is routed to `m_malloc` via
> `MICROPY_NO_ALLOCA` — fine for `print(1+2)` but replace before non-trivial
> programs.  See `MICROPYTHON_PORT.md` and [[minic-setjmp-longjmp]].

---

# (DONE §1r) Next session — MicroPython port: implement medium-model setjmp/longjmp (the LAST link blocker) (post §1q)

> **§1q (build bring-up step 3): FIRST REAL LINK of the curated core subset.**
> The whole MicroPython core (104 curated py/*.c + 2 port glue TUs) now
> compiles to OMF objects (106/106, 0 failures) and **links cleanly except for
> ONE remaining undefined symbol: `setjmp`/`longjmp`** (the NLR primitive).
> Everything else — duplicate-symbol collisions, `__builtin_clz`, `memmove`,
> `__builtin_expect`/`unreachable`, `gc_collect`, `alloca` — is resolved.
>
> New canonical harness `tools/build-micropython.sh` (committed): per-TU
> `clang -E` → minic -m medium → qbe -t i8086 → asm_to_omf → nasm, then
> crt0_exe + all .obj + libstub_exe → omf_link → `build/mp-link/mpython.exe`.
> Re-run: `bash tools/build-micropython.sh --keep-going`.
> New port glue (in the micropython tree): `ports/dos8086/main.c` (a
> `do_str("print(1+2)")` entry — the Phase-4 milestone path, avoids pulling in
> pyexec/readline so the subset stays py-core-only) and `ports/dos8086/mphalport.c`
> (INT 21h AH=40h console output).  Gate **128→130/130**, `make check` green,
> 111 s/r 0 r/r (no grammar change).
>
> **The fixes this session (so you don't redo them):**
> 1. **`static` functions were exported as public OMF symbols** (the link wall:
>    `duplicate public symbol '_utf8_get_char'`).  C `static` = internal
>    linkage; `static inline` helpers in shared headers (MicroPython's
>    `utf8_get_char` in py/misc.h, etc.) were defined-and-exported by every TU
>    that included them → duplicate publics.  TWO-part fix:
>    (a) **minic** (`minic.y`): emit QBE module-local `function` (not `export
>    function`) for a `static` function.  New `pending_static` flag, set/cleared
>    in the `yylex()` wrapper (lexer-level — set on a top-level `STATIC` token,
>    cleared at the function-body-closing `}` and at a top-level `;`), read at
>    all 8 function-header emit sites via the new `fn_export_kw()` helper.
>    Lexer-level (not grammar) keeps conflicts at 111 s/r 0 r/r.
>    (b) **`tools/asm_to_omf.py`**: stop auto-promoting CODE labels to publics.
>    It used to promote EVERY `_xxx:` label because minic didn't mark file-scope
>    *data* as exported.  Now it tracks `defined_text` (labels in a `.text`
>    section) and auto-promotes only NON-text (data/bss) labels; code labels are
>    public iff minic emitted `.globl` (i.e. `export function`).  Static data is
>    still auto-promoted (minic still doesn't `export data` — a separate, not-yet-
>    blocking gap; revisit if static-data duplicates ever surface).
>    Pinned by `static_linkage_probe.c` (medium + large): static fns reachable
>    via the far-call path (direct, nested static->static, recursion, and a
>    function pointer to a static fn), plus a non-static `exported_double` that
>    must stay exported.
> 2. **libstub helpers** (`minic/dos/libstub.asm`, near form — libstub_to_exe.py
>    shifts `[bp+N]→[bp+N+2]` and `ret→retf` for the .EXE): `___builtin_clz`
>    (16-bit CLZ, loop — 8086 has no BSR), `_memmove` (overlap-safe, near-data
>    offset compare picks direction), `___builtin_expect` (returns arg0),
>    `___builtin_unreachable` (bare ret).  All NEW additive symbols (no gate
>    test referenced them); placed before the prune skip region.
> 3. **`gc_collect`** — bring-up STUB in `main.c` (`gc_collect_start();
>    gc_collect_end();`, no root scan).  `print(1+2)` allocates far below the
>    24 KB heap so no collection triggers.  **Must be replaced with a real
>    stack scan** (needs working setjmp to spill callee-saved regs) before any
>    non-trivial program.
> 4. **`alloca` eliminated via config, not codegen** — `MICROPY_NO_ALLOCA=(1)`
>    in `ports/dos8086/mpconfigport.h` routes `alloca(x)`→`m_malloc(x)` (GC
>    heap).  True alloca needs a stack-frame-extending builtin minic doesn't
>    have, and a far-called libstub helper can't grow the *caller's* frame —
>    so config is the right call.
>
> **THE remaining blocker — `setjmp`/`longjmp` for the medium model.**  This is
> the NLR keystone: `nlr_push`/`nlr_pop` (py/nlrsetjmp.c) and the whole
> exception/unwind path depend on it, so NOTHING runs until it works.  It is
> **NOT mechanically convertible** from a near-form libstub stub: the
> medium-model far-call frame has a 4-byte return address (CS:IP), needs `retf`,
> and longjmp must do a FAR jump to restore CS:IP — the `[bp+N]+2` / `ret→retf`
> rewrite in libstub_to_exe.py cannot synthesize that.  So write it directly in
> the **far form** in `tools/libstub_to_exe.py`'s EPILOGUE (alongside
> FAR_STDIO_EXE etc.), or as a model-specific asm.  `jmp_buf` is `int[8]`
> (16 bytes); save BP, SP-at-resume (= lea bp+6 in the far frame), SI, DI, BX,
> and the return CS:IP; longjmp restores them and `jmp far` to CS:IP with the
> value in AX (longjmp(env,0) must yield 1).  **Add a real NLR runtime probe**
> (nlr_push/nlr_raise/nlr_pop round-trip — not just a setjmp smoke test) since
> the ABI is subtle; verify unwinding across a nested call.  Then
> `build-micropython.sh` should produce `mpython.exe` — try `print(1+2)` → `3`
> in DOSBox (Phase 4 milestone).  Expect to then hit codegen/stack/heap runtime
> bugs (the gc_collect stub, far-code segment-count limits, etc.).

# Next session — MicroPython port: py/*.c ASM->OBJ-clean (132/132 to OMF object) (post §1p)

> **§1p (build bring-up step 2): all 132 py/*.c now survive asm->obj** — each
> per-TU i8086 `.asm` (from §1o's `cg/<base>.asm`) goes through the real build's
> `asm_to_omf.py` wrap + `nasm -f obj` and produces an OMF object file.  New
> harness `build/mp-spike/run-asmobj.sh` (committed; the other spike scripts are
> not).  First run: 13 OK, 119 NASM_FAIL — but only **3 distinct root causes**,
> all fixed; second run **132/132 OK**.  `make check` green, 111 s/r 0 r/r, gate
> **125→128**.
> Re-run: `bash build/mp-spike/run-asmobj.sh $(cut -f1 build/mp-spike/codegen.tsv)`
> (needs §1o's `run-codegen.sh` to have produced `cg/*.asm` first).
>
> **The three §1p fixes (so you don't redo them):**
> 1. **`asm_to_omf.py` missed multi-underscore externs** (118 of 132 files).
>    `__builtin_clz` is mangled by minic to `___builtin_clz` and called via
>    `call far ___builtin_clz`.  `collect_referenced_syms`'s regex
>    `\b(_[A-Za-z]…)` can't match it — the word boundary sits before the FIRST
>    underscore, which is followed by `_` not a letter, so the symbol was never
>    added to the `extern` set and nasm failed "symbol not defined".  Fix:
>    `\b(_+[A-Za-z][\w]*)`.  (NB: `___builtin_clz` itself still has no runtime
>    impl — that's a libstub/link-layer gap for later; the per-TU object just
>    needs the extern declared.)
> 2. **C labels collided across functions** (py/runtime.c).  Two functions each
>    with a `too_short:` C label both emitted the flat `@user_too_short` block
>    → one asm symbol `user_too_short:` defined twice → nasm "inconsistently
>    redefined".  C labels are function-scoped.  Fix in `minic/minic.y`: a
>    per-function counter `cur_fn_labelid` (bumped at all 4 function-body emit
>    starts) suffixes every user label `@user_<name>_F<id>` at the Goto/Label
>    emit sites.  These labels aren't exported, so cross-module is already safe;
>    only the intra-module collision needed fixing.  Pinned by `dup_label_probe.c`.
> 3. **16-bit Ocopy of a relocatable address into a slot dropped the size**
>    (py/mpprint.c `_pad_common+17`, py/objstr.c `__str_uni_strip_whitespace`).
>    `=w add $sym, off` folds to a copy; when rega lands it in a slot the
>    generic `{Ocopy,Ki,"mov %=, %0"}` template emitted `mov [bp-N], _sym+off`
>    with no `word`, so nasm's OBJ writer rejected the relocation ("OBJ format
>    can only handle 16- or 32-bit relocations").  Fix in `i8086/emit.c`: an
>    early special-case for `Ocopy Kw && to=RSlot && arg[0]=RCon` emits
>    `mov word [bp-N], <imm/addr>` (no scratch reg, rega unaffected).  The Kl
>    Ocopy path already sized CAddr→slot correctly.  Pinned by `caddr_slot_probe.c`
>    (medium-only: far/Kl pointers route through the already-correct Kl path).
>
> Probes: `dup_label_probe.c` (medium+large), `caddr_slot_probe.c` (medium).

# Next session — MicroPython port: py/*.c CODEGEN-clean (132/132 to i8086 asm) (post §1o)

> **§1o (build bring-up step 1): all 132 py/*.c now survive the FULL codegen
> pipeline** (`minic | qbe -t i8086 -m medium` → i8086 asm), not just the
> parse+SSA step the old spike measured.  New harness
> `build/mp-spike/run-codegen.sh` runs each preprocessed TU through minic→qbe
> and tallies OK / MINIC_FAIL / QBE_FAIL / ASM_STUB.  First run: 124/132 OK, 8
> QBE_FAIL — all 8 were **minic SSA-emission bugs the parse-only spike could not
> see** (qbe validates the SSA; minic alone does not).  Three fixes flipped all
> 8 → **132/132 OK**.  `make check` green, 111 s/r 0 r/r, gate 123→125.
> Re-run: `bash build/mp-spike/run-codegen.sh $(ls -1 ~/projects/micropython/py/*.c | sed 's|.*/||;s|\.c$|.pp.c|;s|^|build/mp-spike/pp/|')`
> (needs the .pp.c files from run-spike.sh first).
>
> **The three §1o minic fixes (so you don't redo them):**
> 1. **Sub-word arithmetic result class** (`minic.y` irtyp→irtyp_ret at 3 emit
>    sites: general binop ~3155, inc/dec ~3070, float→int cast ~2745).
>    `uint16_t+uint16_t` / `uint8_t+uint8_t` where both operands share the
>    narrow type made `prom()` return that type, so the add result temp was
>    `=h`/`=b` — invalid QBE temp class (only w/l/s/d).  `irtyp_ret()` widens
>    char/short→`w` (also C-correct: integer promotion).  Flipped
>    emitbc/gc/objringio/ringbuf ("invalid class specifier").
> 2. **Seq fall-through termination with a trailing goto-label** — `stmt(Seq)`
>    returned `r1||r2`, so an earlier `return` masked a textually-last labeled
>    block that falls through; minic skipped the synthetic trailing `ret` →
>    qbe "last block misses jump".  New `contains_label()` helper; a Seq whose
>    tail contains a label now reports the tail's termination alone (mirrors the
>    existing `contains_case_label` logic in genswitchbody).  Flipped
>    compile/objstr/parsenum.
> 3. **goto Label dropped between switch cases** — `genswitchbody` short-circuited
>    past a Seq tail when the prior case body terminated (`break`) and the tail
>    held no *case* label, dropping a plain goto target sitting between cases →
>    qbe "block @user_X is used undefined".  Now goto labels are kept too (the
>    same `contains_label` check).  Flipped runtime (`power_overflow:` in
>    `mp_binary_op`).
>
> Probe `codegen_term_probe.c` (medium + large) pins all three.

# Next session — MicroPython port: py/*.c DONE (132/132); extmod/shared widened (post §1n)

> Master tracker: `MICROPYTHON_PORT.md`. Spike findings: `MICROPYTHON_SPIKE_REPORT.md`.
> **py/*.c spike now 132/132 OK** — the old `stream` fail was a harness gap
> (`SEEK_SET` undefined) and was closed by adding `SEEK_SET` to
> `build/mp-spike/stubinc/unistd.h`.  §1n then **widened the spike to
> extmod/*.c + shared/**\*.c (96 files)**: 90 OK, 4 MINIC_FAIL, 2 CPP_FAIL.
> Re-run with
> `bash build/mp-spike/run-spike.sh ~/projects/micropython/extmod/*.c $(find ~/projects/micropython/shared -name '*.c')`
> then `grep -E 'MINIC_FAIL|CPP_FAIL' build/mp-spike/summary.tsv`.
> Gate **121→123/123** (+extern_array_expr_probe medium+large). 111 s/r, 0 r/r.
> `make check` green.
>
> **The 4 remaining extmod/shared MINIC_FAILs are NOT minic grammar bugs** (all
> harness/arch artifacts — minic correctly rejects undefined symbols):
> - `sys_stdio_mphal` — `MP_QSTR_readlines` is not in the spike's generated
>   qstr enum (the qstrdefs only cover qstrs seen in py/*.c).  A real build's
>   QSTR generation would emit it.  Same class as the old `stream`.
> - `softtimer` — `MICROPY_PY_PENDSV_EXIT;` is an undefined port macro (left as
>   a bare-identifier statement → "undefined variable").
> - `import` — `mp_import_stat_t` is an undefined typedef (py/lexer.h not pulled
>   in by the spike's minimal include set for this TU).
> - `gchelper_generic` — `const register long x19 asm ("x19");` is the GCC
>   named-register-variable extension on an ARM code path the spike's cpp defines
>   wrongly selected; irrelevant to the i8086 port (which supplies its own
>   gchelper).  (CPP_FAILs `semihosting_rv32`/`semihosting_arm` are missing
>   `<stdnoreturn.h>` / unknown-arch — also not minic.)

## What changed §1n (so you don't redo it)

**One real grammar gap fixed — extern array with a constant-EXPRESSION
dimension.**  `extern char buf[(32) + 1];` parse-errored while
`extern char buf[2];` parsed.  The `EXTERN type IDENT '[' NUM ']' ';'` rule was
the lone array-decl holdout still pinned to `NUM`; changed it to
`'[' expr ']'` (line ~5313 in `minic/minic.y`).  An extern allocates no storage
here, so the folded size is discarded.  0 new conflicts (still 111 s/r 0 r/r).
Flipped extmod/network_ppp_lwip.c (its `mod_network_hostname_data[(…)+1]`).
Probe `extern_array_expr_probe.c` (medium + large).

**Pre-existing gap found, NOT fixed (didn't block any real consumer):**
file-scope sized char array initialised from a string literal —
`char g[5] = "abcd";` parse-errors even with a plain literal dim (brace init
`char g[5] = {'a',…};` and unsized `char g[] = "abcd";` both work).  The probe
sidesteps it with brace init.  Fix later only if a consumer needs it.

## What changed §1m (so you don't redo it)

Four grammar/codegen wins, all in `minic/minic.y` (+ gate wiring), no i8086/QBE
backend changes, **no new conflicts (still 111 s/r, 0 r/r)**, `make check` green.
**Flipped binary, objlist, modbuiltins, objtype, parse** (126→131).

1. **Anonymous struct/union as a type** (flips binary, objlist; half of
   modbuiltins) — `struct { … }` / `union { … }` can now be used directly as a
   `type` (in a cast `(struct{…}*)0`, a local decl `struct{…} v;`, a typedef
   `typedef struct{…} T;`, or a struct member `struct{…} name;`).  The §1k
   attempt (`type: typedefstructstart smembers '}'`) gave **76 r/r** because
   `STRUCT '{'` then had TWO empty marker reductions reachable inside a struct
   body: `typedefstructstart` (anon typedef) and `nested_s_begin` (nested anon
   member).  **Fix = UNIFY them.**  There is now exactly ONE marker for
   `STRUCT '{'` / `UNION '{'` — `nested_s_begin` / `nested_u_begin` (always
   pushes the enclosing `curstruct`, or -1 at top level, onto `structstk`).
   `type: nested_s_begin smembers '}'` pops it and returns `(idx<<3)+STRUCT_T`.
   The former dedicated *named*-nested member rules (`nested_s_begin smembers
   '}' IDENT ';'`) were **removed** — `struct{…} name;` now flows through the
   existing `smembers type IDENT ';'` (its `type` reduces the anon aggregate,
   popping structstk back to the parent first).  `typedef struct{…} T;` flows
   through `TYPEDEF type IDENT ';'`.  `typedefstructstart`/`typedefunionstart`
   are now **tagged-only** (`STRUCT IDENT '{'`) and still back the tagged
   `typedef struct Tag{…} T;` path.  Anon-hoist (`struct{…};` no name) keeps its
   `nestedagg: nested_s_begin smembers '}' ';'` rule.  Probe `anon_aggr_probe.c`.
2. **Function-local + inner-block anonymous enum** (other half of modbuiltins)
   — `enum { A, B, C };` as a statement.  Added `dcls: dcls enumstart enums '}'
   ';'` (function-body top) AND `stmt: enumstart enums '}' ';'` (inner block),
   both mirroring file-scope `edcl` (constants registered by the `enums` rule;
   no storage).  Covered by `anon_aggr_probe.c` cases b/c.
3. **Compound literal with NESTED brace, incl. through a deref** (flips
   objtype) — `*o = (T){{a}, b, c};` (py/objtype.c's `mp_obj_super_t`, whose
   first member is a sub-struct filled by `{…}`).  `inititem` now accepts
   `'{' initlist '}'` and `.field = '{' initlist '}'`.  The expr() and lval()
   compound-literal paths previously had DUPLICATE inline member-fill loops;
   both now call one shared recursive `emit_clit_aggr(clitnum, base_off, sidx,
   init)` that descends into a sub-struct/union member on a nested-brace item.
   The lval() path matters because a struct compound literal on the RHS of
   `*p = …` is re-materialised via lval() to get its address for the struct
   copy.  Probe `nested_clit_probe.c`.
4. **Cast to a function-pointer type** (flips parse) — `(RET (*)(PARAMS)) expr`
   (py/parse.c: `ctx.func = (void (*)(void *))(mp_lexer_free);`).  New
   `pref: '(' type '(' '*' ')' '(' fptpar0 ')' ')' pref` reusing the existing
   `fptpar0` param-type list; the cast type is `IDIR(FUNC($2))`, reinterpreting
   the operand.  Distinguished from the plain cast / compound literal by the
   token after `type` (`(` vs `)`).  Probe `fnptr_cast_probe.c`.

Three probes added (each medium + large): `anon_aggr_probe.c`,
`nested_clit_probe.c`, `fnptr_cast_probe.c`.  Gate **115→121**.

## What changed §1l (so you don't redo it)

**for-init inner-block scope** — closed compile.c's sibling for-loop double
definition. The three C99 for-init rules share a `forinit_var: type IDENT '='`
nonterminal; the state after `type IDENT =` is a single-action state miniyacc
**default-reduces without lexing lookahead**, so the rename binding is
established before the test/increment/body uses are lexed.  Probe
`for_init_scope_probe.c`.  The apostrophe-in-action-comment footgun was also
fixed (commit `a4a1fe7`): `cpycode` in `minic/yacc.c` is comment-aware, so
action comments can use `'`/`"`/braces freely.

## Scope for next session — build bring-up, the next layer down the pipeline

All 132 py/*.c now go C→preprocess→minic(SSA)→qbe(i8086 asm)→asm_to_omf+nasm
cleanly (§1o codegen, §1p asm→obj).  The next layers toward a runnable REPL,
in increasing cost:

1. **DONE (§1p): asm→obj per TU.**  `build/mp-spike/run-asmobj.sh` wraps each
   `cg/<base>.asm` with `asm_to_omf.py` + `nasm -f obj`; 132/132 produce OMF
   objects.  Three gaps fixed (multi-`_` externs, per-function label
   uniquification, 16-bit Ocopy-CAddr→slot size) — see §1p above.

2. **First real LINK of a curated core subset** (NOW the cheapest next signal).
   The dos8086 port does NOT
   need all 131 host objects — drop the other-arch `asm*`/`emitn*`/`nlr*`
   (keep `nlrsetjmp`).  Needs: (a) genhdr headers (already generated at
   `~/projects/micropython/ports/minimal/build/genhdr/` — point `-I` at it or
   regenerate for dos8086), (b) `ports/dos8086/main.c` + `mphalport.c`, (c) a
   `tools/build-micropython.sh` that compiles the subset + crt0 + libstub and
   `omf_link`s them.  Expect: multi-segment far-code link limits (~50+ code
   segments), and `setjmp`/`longjmp` (NLR) — `jmp_buf` is an array typedef;
   real medium-model setjmp/longjmp is still a Phase-2 libc gap.  Milestone:
   `print(1+2)` → `3` in DOSBox (Phase 4).

3. **Widen the codegen spike to extmod/shared** (optional de-risk) — the parse
   spike already cleared them (90/96, rest harness/arch); running them through
   qbe would surface any remaining backend gaps cheaply.

Master staging plan + phase table: `MICROPYTHON_PORT.md`.

## How to find the true site (lag-proof technique, unchanged)
minic's reported error line is the parser's *lookahead* line and lags the real
construct.  Read the real message by running minic directly on
`build/mp-spike/pp/<file>.pp.c` (not the lagged summary.tsv line).
Forward-bisect on column-0 `}` boundaries with brace auto-balancing (a small
python `head -n CUT` + append `}`×(open-count) reproduces far enough into a
function body); the FIRST cut whose prefix errors brackets the construct.  This
session that pinned the fnptr-cast at line 2718 of parse.pp.c in seconds.

## Guardrails (unchanged)
- Rebuild with `cd minic && make minic`; local `yacc` prints conflict counts
  (now **111 s/r, 0 r/r**). Justify any new shift/reduce; **no new
  reduce/reduce**. miniyacc is picky: no `/* … */` between a production head and
  its `:` (this bit twice this session — keep standalone comments OUT of the
  space between a `;` and the next rule head; put them inside the action body
  instead, where `cpycode` is now comment-aware).
- Run `tools/test-dos.sh` (must stay **128/128**) and `make check` (SSA, "All
  is fine!") at the **repo root** (not minic/). Add or extend a probe per
  runtime-bearing feature; the gate runs ~5 min in DOSBox — run it in the
  background and wait.
- Spike harness uses **`clang -E`** (the build-example.sh path uses `cpp`).
- DOSBox capture is occasionally flaky. If a `--model=large` probe diff fails
  once, re-run.

## Orthogonal pre-existing limits (don't chase unless a real consumer needs them)
- **Two divisions feeding one call** — i8086 div AX/DX clobber, `[[i8086-two-div-one-call-clobber]]`.
- **Far-data static pointer relocation** (`l $sym` → far seg:off) — `&global`
  data items are near-only, so probes that take a static address are medium-only.
- **Bare file-scope scalar pointer initializer** — `static int *p = &g;` parse-errors.
- **File-scope sized char array from a string literal** — `char g[5] = "abcd";`
  parse-errors (brace init `{'a',…}` and unsized `char g[] = "abcd";` work).
  Found §1n; not fixed (no consumer blocked).
- **Inline `100000L` literal** — lexer drops the `L`; build from small-literal arithmetic.
- **Deep block-scope shadow of an already-renamed name** — §1k's alpha-renaming
  handles sibling blocks, single-level shadow, and inner-then-function-scope
  collisions; a *declarator* lexed while an outer rename of the same name is
  active (double shadow) can mis-stamp.  See `[[minic-inner-block-scope]]`.
- **Compound literal is evaluated twice on `*p = (T){…}`** — the struct-copy
  assignment path runs expr() (materialise + load) then lval() (materialise +
  address) on the same 'L' node, emitting the literal into two `_clit` slots.
  Correct, just wasteful; not worth fixing unless it shows up hot.
