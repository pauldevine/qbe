# Next session (carried compiler tracks — only the aoa sub-gaps remain; the §8c-surfaced "minic trailing-main quirk" was DISPROVED as a NON-BUG this session.  §8f [2026-06-16, this session] **INVESTIGATED the carried "minic file-scope-statics-need-a-trailing-`main`" quirk (the §8c handoff's latent compiler track) and DISPROVED it — it is a NON-BUG; doc-only close, no compiler/qbe/emit/minic/toolchain source touched, gate UNCHANGED at 367/367, MP unaffected, `make check` not needed (no source change).**  §8c reported that compiling `dos_shim.c` without its trailing `main()` (the abandoned `-DNO_SHIM_MAIN` experiment) dropped ALL file-scope statics (`_impure_ptr`/`shim_reent`/`shim_files`/`shim_file_used`), ".ssa 0-vs-4 defs", and hypothesised "trailing `main()` gates static-data emission"; the `-Dmain=newlibc_test_main` rename sidestepped it and shipped, and it was carried as a latent track through §8d/§8e.  **THREE INDEPENDENT PROOFS it does NOT reproduce:** (1) MECHANISM — `minic.y:10531-10548` emits file-scope data via an UNCONDITIONAL loop over `gloname[1..nglo)` that runs AFTER `yyparse()`; `main` is never consulted, so the ONLY path to zero data output is `yyparse() != 0 → die("parse error")` → empty stdout ⇒ "0 defs" ⟺ parse error ⟺ MALFORMED input, never "main gated emission".  (2) EXACT §8c RECONSTRUCTION — wrapped `dos_shim.c`'s `main` in `#ifndef NO_SHIM_MAIN`, ran the real cpp (`clang -E -P -nostdinc -DDOS -D__ia16__ -DNO_LIBSTUB`) + minic (compact AND medium) both with and without `-DNO_SHIM_MAIN`: BOTH emit all 4 data defs, exit 0.  (3) TRUNCATION SWEEP — truncated the no-`main` TU at all 40 top-level `^}` boundaries: every one parses OK, data-def count rises monotonically 0→2→4 tracking declared statics; no parse error and no dropped statics anywhere.  **CONCLUSION:** the §8c 0-vs-4 was a MEASUREMENT ARTIFACT — the `-DNO_SHIM_MAIN` wrap fed minic malformed C (an eaten brace / dangling token), parse-error → empty `.ssa`, misdiagnosed as "main gates emission".  The `-Dmain=newlibc_test_main` rename remains the right choice — it solves the REAL `_main` symbol collision, not a phantom bug.  No regression gate added: a meaningful one needs multi-TU link plumbing (`build-example.sh` is single-source) — disproportionate for a confirmed non-bug (user chose the doc-only close).  Records corrected: `MEMORY.md`, `project_8c_mp_libstub_free.md`, `project_8e_huge_ptr_equality.md`, new `project_8f_trailing_main_nonbug.md`.  **⇒ Next session: ONE carried COMPILER track remains (awaits a real consumer — synthetic-but-bug-loud gating): the aoa sub-gaps** — the file-scope/static multi-decl array-first form (`static jmp_buf fa[2], fb[2];`, a grammar PARSE-ERROR gap, distinct from §7e/§7j aoa sizing) and the plain `jmp_buf a, b;` array-typedef-instance multi-decl.  The huge pointer-arith family is CLOSED (relational §7u + equality §8e).  Bare-metal phase-3 bm_testhost tests EXHAUSTED.  Libstub-retirement campaign COMPLETE.  NO QBE backend bug open; easy frame-size levers spent (§7k).)

## §8f session notes (2026-06-16)

### The pick
- §8e handoff offered two carried compiler tracks (aoa sub-gaps / minic
  trailing-main quirk).  User (AskUserQuestion) chose the trailing-main quirk.

### The investigation — it is a NON-BUG
- §8c claimed: dropping `dos_shim.c`'s trailing `main()` (via `-DNO_SHIM_MAIN`)
  drops ALL file-scope statics; ".ssa 0-vs-4 defs"; "trailing main() gates
  static-data emission".
- DISPROVED by three independent proofs:
  1. MECHANISM — minic.y:10531-10548 emits file-scope data via an
     UNCONDITIONAL post-`yyparse()` loop over `gloname[]`; `main` is never
     consulted.  Zero output ⟺ `yyparse()!=0 → die("parse error")` ⟺
     MALFORMED input.
  2. EXACT §8c RECONSTRUCTION — `#ifndef NO_SHIM_MAIN` wrap, real cpp
     (`-D__ia16__ -DNO_LIBSTUB`), minic compact+medium, both with/without
     `-DNO_SHIM_MAIN` → all 4 data defs, exit 0.
  3. TRUNCATION SWEEP — truncate the no-`main` TU at all 40 top-level `^}`:
     every one parses OK, defs rise 0→2→4 with declared statics.
- The §8c 0-vs-4 was a MEASUREMENT ARTIFACT (the `-DNO_SHIM_MAIN` wrap produced
  malformed C — an eaten brace/dangling token, the same class of mistake I hit
  once mid-investigation — → parse error → empty `.ssa`).

### Outcome (doc-only close, user's choice)
- No code change.  test-dos UNCHANGED 367/367 (not re-run — nothing touched).
- The `-Dmain=newlibc_test_main` rename is still correct (solves the real
  `_main` link collision, never masked a real defect).
- No regression gate: a meaningful one needs multi-TU link plumbing
  (build-example.sh is single-source) — disproportionate for a non-bug.
- Records corrected: MEMORY.md, project_8c, project_8e, new project_8f.

### ⇒ Next session
- ONE carried compiler track remains (awaits a consumer): the aoa sub-gaps —
  file-scope/static multi-decl array-first (`static jmp_buf fa[2], fb[2];`, a
  grammar parse-error gap) + plain `jmp_buf a, b;` multi-decl.
- Huge pointer-arith family CLOSED (§7u + §8e).  Libstub-retirement COMPLETE.
- NO QBE backend bug open.
---

# Next session (carried compiler tracks — the §7u huge-normalisation family is now COMPLETE (relational §7u + equality §8e).  §8e [2026-06-16, this session] **CLOSED the huge-pointer EQUALITY flat-compare — the latent sibling of the §7u relational fix; `==`/`!=` of two huge pointers that denote the SAME linear byte through DIFFERENT seg:off normalisations now compare correctly; `tools/test-dos.sh` 366 → 367, the fix is an MHuge-gated `minic.y` frontend change → no emit audit, MP compact body 689,760 BYTE-IDENTICAL → no Victor run, `make check` green.**  §7u routed huge RELATIONAL compares (`p < q`, `p <= q`) through `_qbe_huge_cmp` because the flat 32-bit `cultl`/`csltl` of two seg:off words is only monotonic-in-linear-address when BOTH operands are NORMALISED, and it explicitly LEFT the equality case unfixed (the sole live consumer `_sbrk` only does `== NULL` = 0:0 = linear 0, fine flat).  The equality gap is the SAME defect: the flat `ceql`/`cnel` compares the raw 32-bit (seg<<16)|off words bit-for-bit, so an UNNORMALISED operand (a bare symbol address whose offset can exceed 0xF) wrongly compares UNEQUAL to the NORMALISED pointer `_qbe_huge_add` returns for the same byte — a pure FALSE-NEGATIVE (a genuinely different address can never alias).  **FIX (minic.y `Binop` default arm, ~25 lines, mirrors the §7u relational block):** under `memmodel == MHuge` an equality compare (`o == 'e' || o == 'n'`) with BOTH operands non-function pointers routes through `_qbe_huge_cmp(p,q)` (= signed `linear(p)−linear(q)`, the SAME helper §7u uses) and tests its sign — `p == q ⟺ ceql cmp,0`, `p != q ⟺ cnel cmp,0`.  Linear equality is exactly C11 6.5.9 pointer equality on the flat 8086 huge model, and stays correct for NULL (`0:0` → linear 0).  MHuge-gated → compact/large/near (incl. the MP byte-compare) keep flat `ceql`/`cnel`, untouched.  Gated by the all-new `huge_ptreq_probe` (`minic/dos/examples/huge_ptreq_probe.c`, :huge) — bug-loud: it builds two pointers to the same linear byte through different normalisations (`(char *)constant` raw/unnormalised vs the normalised result of `+ 0` through `_qbe_huge_add`) and asserts `==`/`!=`; the UNFIXED compiler prints `eqconst=FAIL`/`neconst=FAIL`/`eqarith=FAIL`/`nearith=FAIL` (verified by reverting), while the address-distinct and NULL cases stay correct.  Every existing huge probe (huge_norm, hugeprobe, gc_bigheap, caddr family) still passes — their `p==q`/`p!=NULL` now goes through `huge_cmp` but yields the SAME answer.  STRATEGY: only the MHuge-gated minic.y arm + the new probe/golden + one gate entry changed; the COPY/ADD-NEVER-MUTATE libstub-free toolchain is untouched → MP/stevie/every gate provably can't regress.  **⇒ Next session: pick a remaining carried COMPILER track (each awaits a real consumer — synthetic-but-bug-loud gating):** (1) the aoa sub-gaps (file-scope/static multi-decl array-first = a grammar parse-error gap; plain `jmp_buf a, b;` multi-decl); (2) the minic file-scope-statics-need-a-trailing-`main` emission quirk surfaced in §8c (removing the last `main()` drops a TU's file-scope `static` data defs — confirmed via `.ssa` diff; no consumer, the `-Dmain` rename avoids it).  The huge pointer-arith family (relational §7u + equality §8e) is now CLOSED.  Bare-metal phase-3 bm_testhost tests EXHAUSTED.  Libstub-retirement campaign COMPLETE (gate probes + stevie + MicroPython all libstub-free by default; `libstub_to_exe.py` survives only as the `--libstub` anchor + probe scripts + tiny/.COM fallbacks).  NO QBE backend bug open; easy frame-size levers spent (§7k).)

## §8e session notes (2026-06-16)

### The pick
- §8d handoff offered three carried compiler tracks (huge ptr equality / aoa
  sub-gaps / minic trailing-main quirk).  User (AskUserQuestion) chose the huge
  pointer EQUALITY flat-compare — the most concrete, a direct follow-on to §7u.

### The bug + fix
- §7u fixed huge RELATIONAL compares (p<q, p<=q) via _qbe_huge_cmp but LEFT
  equality unfixed (only consumer _sbrk does ==NULL = linear 0, fine flat).
- Same defect for ==/!=: flat ceql/cnel compares raw seg:off words, so two
  pointers to the SAME linear byte with DIFFERENT normalisations (unnormalised
  symbol addr off>0xF vs the normalised _qbe_huge_add result) wrongly compare
  UNEQUAL (false-negative only; a different address never aliases).
- FIX (minic.y Binop default arm, ~25 lines, mirrors §7u relational block):
  MHuge + (o=='e'||o=='n') + both operands non-FUN PTR → route through
  _qbe_huge_cmp(p,q) and test ceql/cnel cmp,0.  Correct for NULL too (0:0→0).
  MHuge-gated → compact/large/near (incl. MP) keep flat ceql/cnel, untouched.

### Gate + verification
- NEW minic/dos/examples/huge_ptreq_probe.c (:huge), golden
  minic/dos/tests/huge_ptreq_probe.golden.txt.  Bug-loud: (char*)0x10000010UL
  (unnorm, linear 0x10010) vs (char*)0x10010000UL (norm, same linear) AND vs
  unnorm+0 (huge_add normalises) — UNFIXED prints eqconst/neconst/eqarith/
  nearith=FAIL (verified by git-stashing the fix); address-distinct + NULL OK.
- test-dos 366→367; make check green; 8 qbe_huge_cmp calls in the probe .ssa.
- All existing huge probes still pass (their p==q/p!=NULL now via huge_cmp,
  same answer).
- MP compact rebuilt: image 710,352 / body 689,760 BYTE-IDENTICAL to §8d
  baseline (MHuge-gated → compact provably untouched) → no Victor run.
- minic.y frontend change (not emit.c) → no emit audit.

### ⇒ Next session
- Remaining carried compiler tracks (await a consumer): aoa sub-gaps
  (file-scope/static multi-decl array-first parse-error gap; plain jmp_buf a,b
  multi-decl); the minic trailing-main file-scope-statics quirk (§8c).
- Huge pointer-arith family CLOSED (relational §7u + equality §8e).
- NO QBE backend bug open.
---

Older session headers (§7w and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
