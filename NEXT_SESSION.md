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

# Next session (carried compiler tracks — the Phase-6 libstub-retirement campaign is now COMPLETE end-to-end (every gate probe AND every shipped program: stevie §7r–§7v, then MicroPython §8c–§8d).  §8d [2026-06-16, this session] **FLIPPED MicroPython's default to libstub-free and REBASELINED the regression corpus — `tools/build-micropython.sh` (no flag) now builds the libstub-FREE MP (compact default-heap body 689,760, was 731,088 under libstub); `--libstub` is the opt-out equivalence anchor (verified still body 731,088); `make check` green, no compiler/qbe/emit/minic source touched (→ no emit audit).**  §8c had landed libstub-free MP as an OPT-IN `--no-libstub` path (byte-exact vs host `python3` in DOSBox AND on MAME victor9k); §8d is the small end-state flip the user requested.  **The change is one line + doc rebaseline:** `build-micropython.sh` `NO_LIBSTUB=0`→`1` (default), `--libstub` now the opt-out anchor.  The produced `--model=compact` binary is BYTE-IDENTICAL to §8c's already-Victor-verified `--no-libstub` build (same code path; the flag only chooses which path is default), so NO new Victor run was needed.  **VERIFIED:** default compact = image 710,352 / body 689,760 (reproducible, matches §8c); `--libstub` compact = image 751,664 / body 731,088 (anchor intact); the default small-heap build (571,600 B) runs the §8c smoke test (language + FLOAT + `math` sqrt/pi/sin + recursion + ZeroDivisionError) BYTE-EXACT vs host `python3` in DOSBox; `make check` green.  **Rebaseline scope:** the 731,088 baseline was DOCUMENTATION-ONLY — `git grep 731088` over `tools/`/`*.sh`/`*.py` is EMPTY (no hardcoded assertion), and `recompile-mp-tu.sh` is data-driven from `/tmp/mp_objs.txt` (written by `build-micropython.sh`), so it adapts automatically.  Updated the FORWARD-looking refs only: `CLAUDE.md` (regression-corpus line + the "After ANY toolchain change" house rule + a §8d status clause + Last-Updated), `ROADMAP.md` (the two "Body 731,088 B" lines), and `build-micropython.sh`'s banner/arg comments.  HISTORICAL "MP body 731,088 byte-identical" session records (§4t…§8a) are LEFT verbatim — accurate point-in-time records of the libstub corpus.  STRATEGY (ADD/FLIP, NEVER MUTATE the runtime): `dos_shim.c`/`libstub.asm`/`libstub_to_exe.py`/`near_to_far_rt.py`/`builtins_rt.asm`/every runtime asm + newlibc are UNTOUCHED; only the default flag + docs changed.  `libstub_to_exe.py` now survives ONLY as: the `--libstub` MP anchor, the `build-example.sh`/`build-stevie.sh` `--libstub` anchor, the `build-{sprintf,int86x,divmod32}-probe.sh` probe scripts, and the tiny/.COM fallbacks — it is no longer the DEFAULT runtime of ANY shipped artifact.  **⇒ Next session: the libstub-retirement campaign is DONE; pick a carried COMPILER track (each awaits a real consumer — synthetic-but-bug-loud gating):** (1) the huge pointer EQUALITY flat-compare (the §7u relational fix's latent sibling — `==`/`!=` of two differently-normalised huge pointers; `_sbrk` only does `== NULL` so no live consumer); (2) the aoa sub-gaps (file-scope/static multi-decl array-first = a grammar parse-error gap; plain `jmp_buf a, b;` multi-decl); (3) the minic file-scope-statics-need-a-trailing-`main` emission quirk surfaced in §8c (removing the last `main()` drops a TU's file-scope `static` data defs — confirmed via `.ssa` diff; no consumer, the `-Dmain` rename avoids it).  Bare-metal phase-3 bm_testhost tests EXHAUSTED.  NO QBE backend bug open; easy frame-size levers spent (§7k).)

## §8d session notes (2026-06-16)

### The ask
- User: "go ahead and make libstub-free the default" — the §8d end-state §8c
  had deferred (flip + rebaseline; the user's call since it rebaselines the
  project's regression baseline).

### The change (tiny — one flag + docs)
- build-micropython.sh: NO_LIBSTUB default 0 → 1; --libstub now the opt-out
  anchor; --no-libstub re-asserts the default.  Comment + banner rebaselined.
- The 731,088 baseline was DOCUMENTATION-ONLY: `git grep 731088` over tools/
  *.sh *.py is EMPTY (no script assertion).  recompile-mp-tu.sh reads
  /tmp/mp_objs.txt (written by build-micropython.sh) so it adapts automatically.
- Doc rebaseline (forward-looking only): CLAUDE.md (corpus line + "After ANY
  toolchain change" house rule + §8d status clause + Last-Updated→§8d),
  ROADMAP.md (two "Body 731,088 B" lines).  Historical "731,088 byte-identical"
  session records LEFT verbatim (accurate for their time).

### Verification (no compiler change → no emit audit; binary == §8c's Victor-verified build → no new Victor run)
- Default (no flag) compact: image 710,352 / body 689,760 (reproducible; ==§8c).
- --libstub compact: image 751,664 / body 731,088 (anchor INTACT).
- Default small-heap build (571,600 B): smoke test (build/mp-nl-smoke.py)
  BYTE-EXACT vs host python3 in DOSBox.
- make check green.

### ⇒ Next session
- libstub-retirement campaign COMPLETE (gate probes + stevie + MicroPython all
  libstub-free by default; libstub_to_exe.py survives only as the --libstub
  anchor + probe scripts + tiny/.COM fallbacks).
- Carried compiler tracks (await a consumer): huge pointer EQUALITY flat-compare
  (§7u sibling); aoa sub-gaps; the minic trailing-main file-scope-statics quirk.
---
Older session headers (§7w and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
