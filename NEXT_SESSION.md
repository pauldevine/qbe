# Next session (§2q' DONE — the §2q `def add(a,b)` HANG is FIXED. NOT a backend arg-passing bug: it was a minic FRONTEND gap — an INDIRECT (function-pointer) call did not coerce arguments to the callee's declared parameter types. MicroPython's body-variable LOAD_FAST goes through the bytecode emitter METHOD TABLE (emitcommon.c:131 `emit_method_table->local(emit, qst, id->local_num, kind)`), an indirect call; minic's coerce_arg only fired on DIRECT (name-keyed fnproto) calls, so `id->local_num` (uint16_t, Kw) stayed `w` where the param is `mp_uint_t` (Kl) — the 2-byte push shifted `kind`, which was then read from the wrong slot (arrived 0xb0). §2q's "static asm looked correct" puzzle was because it inspected the DIRECT call sites (compile.c); the real runtime path is the method-table indirect call. FIX in `minic/minic.y`: record each fn-ptr declarator's parameter types (in a new `fpproto[]` registry, with the index stashed in the declarator's varh/Member entry — type-bit stashing was impossible since the type integer is full and the typedef/member tables are 32-bit) and coerce args at BOTH indirect-call paths (member `obj->fn(...)` via a g_callee_fpid stash set in expr() case '.', and `fp(...)`/`(*fp)(...)` via the variable's recorded fpid). Probe `argmix_probe.c` (medium+compact+large, runtime-filled method table so it needs no --far-static-data) — bug-loud (`m7 FAIL 4300`/`fp9 FAIL 6500`, the trailing int dropped) on the unfixed compiler. `make check` green, 111 s/r 0 r/r (no grammar-structure change, only fptpar actions), DOS gate 174/174 (one flaky-DOSBox for_comma_inc_probe re-confirmed passing standalone). KNOWN GAP (noted, not §2q-blocking): a fn-ptr declared via a TYPEDEF (`typedef int (*F)(...); F fp;`) is still NOT coerced — the typedef name is lost at the variable declaration. NEXT = rebuild MicroPython compact far-data + run `def add(a,b): return a+b; print(add(3,4))` on the real Victor (run-victor-sasi.sh, FOREGROUND) — expect the LOAD_FAST `d4 NN`→`b0/b1` fix to let add() execute; restore heap 20480 + revert the MP-tree debug markers (CLEANUP list in §2q below). See [[project-repl-loadfast-argmix]].)

> **§2q' (DONE 2026-06-02) — the §2q user-function-with-args HANG is FIXED in
> the minic frontend.  It was NOT an i8086 backend arg-passing bug (the §2q
> hypothesis) — it was a missing argument coercion on INDIRECT (function-pointer)
> calls.  `minic/minic.y` only; `make check` green (111 s/r, 0 r/r — only the
> fptpar semantic actions changed, no grammar-structure change); amd64/arm64/rv64
> unaffected; DOS gate 174/174.**
>
> ROOT CAUSE (the §2q "static-vs-runtime contradiction", resolved): §2q traced
> the DIRECT call sites in py/compile.c (`EMIT_LOAD_FAST` macro), where the asm
> looked correct — but MicroPython's bytecode-only port (`MICROPY_EMIT_NATIVE`=0)
> routes a function body's variable read through `compile_load_id` →
> `mp_emit_common_id_op` → **emitcommon.c:131** `emit_method_table->local(emit,
> qst, id->local_num, MP_EMIT_IDOP_LOCAL_FAST)` — an INDIRECT call through the
> static method table `mp_emit_bc_method_table_load_id_ops`.  The member type is
> `void (*)(emit_t*, qstr, mp_uint_t local_num, int kind)` = a (l, w, l, w) call;
> `id->local_num` is a `uint16_t` (Kw), but the param is `mp_uint_t` (Kl, 4 bytes
> under far-data).  minic's §2i/§2o `coerce_arg` fires only on DIRECT calls
> (`fnproto`, keyed by the callee NAME); the indirect-call paths (`call()` case
> 'C' fn-ptr branch and `expr()` case 'I') evaluated args with `eval_arg` and
> NEVER coerced.  So `local_num` was pushed as 2 bytes where the callee reads 4,
> shifting `kind` — read from the wrong [bp+off] as 0xb0 — and the LOAD_FAST
> emitter took the 2-byte `MP_BC_LOAD_FAST_N + kind` branch, emitting `d4 NN`
> bytecode the VM landed outside add's body → the hang.
>
> WHY NOT TYPE-BIT STASHING: the fn-ptr type integer encodes only the RETURN
> type (no param list), and is full (recursive `<<3` shifts); the typedef/member
> tables store types as 32-bit `unsigned`, so high proto bits would truncate and
> shifts would corrupt them.  Instead: a name-free, collision-free registry.
>
> FIX (`minic/minic.y`): a new `fpproto[]` table holds each fn-ptr declarator's
> fixed parameter types; the index (`fpid`) is stamped into the declarator's
> `varh`/`Member` entry (new fields, init -1).  The `fptpar0/fptpar1` grammar
> (previously `{ $$ = 0; }`, discarding the params) now builds a `mkptype` type
> chain — same tokens/reductions, so ZERO new conflicts.  Recorded at the struct
> fn-ptr member rule and the local `T (*fp)(...)` decl rules.  Recovered at the
> call: case '.' stashes the member's fpid in `g_callee_fpid` (read by case 'I'
> right after `expr(n->l)`), and case 'C'/'I' fall back to the variable's fpid
> via `varfpid`.  Both indirect-call arg loops now run `coerce_arg` per fixed
> param — the indirect-call analogue of the direct-call fnproto coercion.
>
> PROBE `minic/dos/examples/argmix_probe.c` (+golden, gated medium+compact+large):
> the exact (l, w, l, w) shape with a narrow uint16_t 3rd arg (widened w→l) and a
> trailing `int k`, via a method-table member call (MP shape) AND a directly-
> declared `int (*fp)(...)` variable.  Bug-loud on the unfixed compiler
> (`m7 FAIL 4300`, `fp9 FAIL 6500` — the trailing int read from the wrong slot).
> The method table is filled at RUNTIME (not `static const`) so the probe needs
> no `--far-static-data`: a code symbol in a STATIC data initializer is only
> relocated to a far seg:off under that opt-in (which the MicroPython port
> enables — that is why MP's static method table works on-target); a plain
> `dd _fn` left segment 0 and hung.  Orthogonal to the coercion fix.
>
> KNOWN GAP (noted, NOT §2q-blocking): a fn-ptr declared through a TYPEDEF
> (`typedef int (*F)(...); F fp;`) is still not coerced — minic resolves the
> typedef to its type at the variable declaration, losing the typedef name, so
> the recorded prototype doesn't transfer to the variable.  MicroPython's method
> tables and plain `T (*fp)(...)` declarators are covered; the typedef-variable
> form is left as a documented gap.
>
> NEXT: rebuild MicroPython (`bash tools/build-micropython.sh --model=compact
> --keep-going`) and run `def add(a, b):\n    return a + b\nprint(add(3, 4))` on
> the real Victor (`tools/run-victor-sasi.sh build/mp-link/mpython.exe 60`,
> FOREGROUND).  Expect the LOAD_FAST bytecode to now be `b0 b1 f2 63` (not
> `d4 00 d4 01 ...`) and add() to execute.  BEFORE the run, do the §2q CLEANUP
> (below): RESTORE `ports/dos8086/mpconfigport.h` heap 16384→**20480**, and
> revert the MP-tree debug markers in vm.c/objfun.c/bc.c/emitbc.c (KEEP the
> py/parse.c §2n alignment fix).  See [[project-repl-loadfast-argmix]].
>
> ---
>
# (prior) Next session (§2q IN PROGRESS — BROADENING THE REPL. Confirmed on the real Victor: arithmetic, variables, multi-statement, while-loop, for/range ALL WORK. NEW BUG (unfixed, root-cause LOCALIZED): a user function with args — `def add(a,b): return a+b; print(add(3,4))` — HANGS during execution because add's body bytecode is MIS-EMITTED: each `LOAD_FAST` (should be 1-byte `0xb0+n`) is emitted as 2-byte `0xd4 NN`. Cause: in `mp_emit_bc_load_local` (py/emitbc.c) the 4th param `kind` (an `int`, passed `w 0`) ARRIVES AS 0xb0 instead of 0 — confirmed on-target (`LFb0 00`/`LFb0 01`). The SSA is CORRECT on BOTH sides (caller `call $f(l,w,l,w 0,...)`, callee `(l %t0,w %t1,l %t2,w %t3)`), so it's an i8086 BACKEND arg-passing or param-read bug for the `(l,w,l,w)` mix where the last `w` arg is misread. STATIC analysis of the caller asm looked correct (kind=0 written, frame OK) — the static-vs-runtime contradiction is UNRESOLVED. NEXT = reproduce as a pure minic/i8086 ABI probe (no MicroPython, DOSBox gate) + dump raw callee param bytes to disambiguate arg-passing vs param-read. See §2q below.)

> **§2q (IN PROGRESS 2026-06-02) — broadening the REPL past `print(1+2)`.
> Multiple language features VERIFIED on the real Victor; ONE new bug found,
> root-cause localized but NOT yet fixed.  No qbe/minic/MP code committed this
> session (all changes are debug instrumentation in the MP tree — see CLEANUP).**
>
> WHAT WORKS NOW (all verified end-to-end on the real Victor 9000 via
> run-victor-sasi.sh, each printing correct results):
>  - `print(1+2)` → 3 (baseline, re-confirmed)
>  - arithmetic: `x=10;y=5;print(x*y);print(x-y);print(x+y)` → 50, 5, 15
>    (exercises 32-bit Kl multiply with >16-bit result, assignment, name lookup,
>    multi-statement MP_PARSE_FILE_INPUT)
>  - while loop + comparison: `i=0;while i<3:print(i);i=i+1` → 0,1,2
>  - for/range + iterator protocol: `for i in range(3):print(i)` → 0,1,2
>    (range/iterator code was ALREADY linked via the builtins table — no image
>    growth)
>  Changing the do_str() test STRING does not change linked code footprint (the
>  whole parser/compiler/VM is pulled in regardless); only features needing new
>  runtime fns grow it.
>
> **THE NEW BUG — user function with positional args hangs.**
> `def add(a, b):\n    return a + b\nprint(add(3, 4))` reaches D3 (compile done)
> then HANGS in execution with NO output (no D4, no exception).  The 0-arg module
> function runs fine; the new thing is calling a user bytecode function WITH args
> and a body that uses LOAD_FAST (function locals).  Every earlier test used only
> module-level globals (LOAD_NAME/STORE_NAME), never LOAD_FAST.
>
> ROOT CAUSE (localized by on-target bisection — the decisive method again):
>  1. Instrumented vm.c MAKE_FUNCTION(M1)/CALL_FUNCTION(C0/C1): trace reached
>     `M1 C0` then hung — i.e. the call INTO add never returns.
>  2. Instrumented objfun.c fun_bc_call (F0/Fe/Ff) + bc.c mp_setup_code_state
>     (S0/S1/S2/S9): `F0 S0 S1 S2 S9 Fe` then hang — setup + ALL arg-binding
>     complete; hang is inside `mp_execute_bytecode` of add's body.
>  3. Per-opcode tracer at the vm.c dispatch switch: add's body FIRST opcode
>     read as **0x14** (one heap size) / **0xd4** (another) — should be **0xb0**
>     (LOAD_FAST 0).  The landing opcode CHANGED with heap layout → ip pointed
>     outside add's real bytecode.
>  4. Dumped add's stored bytecode in bc.c (skhex of self->bytecode[0..15] + the
>     decoded n_state/n_pos_args/n_info/n_cell + landing offset).  add's bytecode:
>     `1a 0c | 81 56 81 57 81 58 | d4 00 d4 01 f2 63 ...`
>       - SIG 0x1a → n_state=4, n_pos_args=2 (CORRECT for add(a,b))
>       - SIZE 0x0c → n_info=6, n_cell=0 (n_info=6 is CORRECT: the on-target build
>         encodes the 3 code_info qstrs — simple_name, arg a, arg b — as 2-byte
>         var-uints each = 6 bytes; the .mpy reference uses a qstr-table window so
>         its n_info is smaller.  Do NOT chase the n_info difference; it is a
>         config artifact, not the bug.)
>       - body at offset 8: `d4 00 d4 01 f2 63` — WRONG.  Reference (mpy-cross
>         disasm) body is `b0 b1 f2 63` = LOAD_FAST 0; LOAD_FAST 1; BINARY_OP
>         __add__; RETURN_VALUE.  The `f2 63` tail is right; the two LOAD_FASTs
>         are each emitted as a 2-byte `d4 NN` instead of the 1-byte `0xb0+n`.
>  5. emitbc.c `mp_emit_bc_load_local` (line ~514):
>        if (kind == MP_EMIT_IDOP_LOCAL_FAST && local_num <= 15)
>            emit 1-byte (MP_BC_LOAD_FAST_MULTI + local_num);   // 0xb0+n
>        else
>            emit 2-byte (MP_BC_LOAD_FAST_N + kind, local_num); // 0x24+kind, then n
>     On-target opcode 0xd4 = MP_BC_LOAD_FAST_N(0x24) + 0xb0, i.e. the else branch
>     ran with **kind == 0xb0** (not 0).  Added an LF marker printing kind+local_num
>     at the top of the function: on-target prints **`LFb0 00`** and **`LFb0 01`**
>     (×3, one per compile pass) — so `kind` ARRIVES as 0xb0, local_num arrives
>     CORRECTLY as 0/1.  (NB 0xb0 == MP_BC_LOAD_FAST_MULTI — possibly a
>     coincidence, possibly a clue.)
>
> THE PUZZLE (where it stands — UNRESOLVED): the SSA is CORRECT on both sides.
>   - call (compile.ssa): `call $mp_emit_bc_load_local(l %t131, w %t136, l %t142, w 0, ...)`
>     — coerce_arg correctly WIDENED local_num (a uint16_t field, Kw) to `l` to
>     match the `mp_uint_t` param.  kind passed `w 0`.  (`...` is on EVERY minic
>     call — normal, not variadic-anomalous.)
>   - callee header (emitbc.ssa): `export function $mp_emit_bc_load_local(l %t0, w %t1, l %t2, w %t3)`.
>   Types: `qstr`=`size_t`=2B(Kw); `mp_uint_t`=`uintptr_t`=4B(Kl); `int`=2B(Kw);
>   MP_EMIT_IDOP_LOCAL_FAST=0; id->local_num is uint16_t (2B) → coerced to l.
>   So the IR is a clean (l,w,l,w) call matching a (l,w,l,w) callee, last arg `w 0`.
>   The i8086 BACKEND mis-passes/mis-reads the 4th arg (kind) as 0xb0.
>
>   STATIC asm trace did NOT reveal the fault (this is the unresolved part):
>   - Caller site asm 10627 (in _close_over_variables_etc, frame `sub sp, 156`):
>     writes kind=0 at [bp-152], local_num at [bp-156/-154], qst at [bp-158],
>     emit at [bp-162/-160].  slot() = `-6 - 2*(fn->slot - s)`; prologue pushes
>     bx/si/di (6B) AFTER `mov bp,sp` then `sub sp, 2*fn->slot`, so SP = bp-6-156
>     = bp-162 = arg slot 0 (emit).  Frame is CORRECT (my first "frame too small"
>     read was an arithmetic error — the -6 reg block).  Callee reads (far code →
>     4-byte ret addr → first arg at bp+6): emit@[bp+6], qst@[bp+10],
>     local_num@[bp+12], kind@[bp+16].  Map back: callee kind@[bp+16] == caller
>     [bp-152] == 0.  So STATICALLY kind should be 0.  Runtime says 0xb0.
>     CONTRADICTION not yet explained.
>   - There are TWO call sites: asm 10627 and 41310.  Both look statically correct
>     (41310: kind=0 @[bp-338], qst=`w 10`, local_num @[bp-342/-340], emit
>     @[bp-348/-346]).  Did NOT confirm which one actually runs for add's LOAD_FAST
>     (compile.c:643 EMIT_LOAD_FAST in compile_load_id).
>
> **NEXT — two concrete moves (do the probe FIRST, it's the fast path):**
>  1. **Reproduce as a pure minic/i8086 ABI probe — NO MicroPython, NO Victor.**
>     The whole point: this is a backend arg-passing bug for a `(l,w,l,w)` call
>     whose 4th arg is a `w` constant, under compact far-data (far-call → 4-byte
>     ret addr).  Write `minic/dos/examples/argmix_probe.c`: a function
>     `int f(void *p, unsigned a, unsigned long n, int k)` that returns/prints
>     `k`, called as `f(&x, 1, 0, 0)` (also try `f(&x, 1, 0, 7)`), gated
>     compact+large in tools/test-dos.sh (DOSBox — fast iteration, no SASI loop).
>     If k arrives wrong → bug reproduced in the gate; iterate there.  Mirror the
>     exact widths: p=far ptr(l), a=unsigned(w), n=unsigned long(l, forces the Kl
>     in 3rd position), k=int(w) last.  This is the [[minic-wide-arg-narrow-param]]
>     family — but here the SSA is already correct, so look at i8086/abi.c
>     (selpar param-offset assignment + the max_arg_words/arg_slot_top reservation
>     at i8086_abi line ~358-387) and i8086/emit.c selcall (arg-slot stores) for
>     the `(l,w,l,w)` offset computation.  Suspect: an off-by-one/size error when
>     a Kl arg (n) sits between Kw args and the final Kw arg's slot offset is
>     mis-derived, OR the callee selpar reads the 4th param from a wrong [bp+off].
>  2. **If the probe does NOT reproduce it**, the fault needs the exact MP context
>     — then dump the RAW callee param bytes on-target: in mp_emit_bc_load_local
>     print `*(unsigned char*)((char*)&emit + 16)`-style or, cleaner, read the
>     param stack directly — to learn whether [bp+16] CONTAINS 0xb0 (arg-passing
>     bug) or holds 0 but `kind` (%t3) is mis-bound/clobbered (param-read bug).
>     Also confirm WHICH call site runs (add a distinct marker per site).
>
> **CLEANUP REQUIRED before any milestone run / commit (all in the MICROPYTHON
> tree ~/projects/micropython, uncommitted; the qbe repo has NO changes this
> session except the pre-existing uncommitted asm_to_omf.py TEXT_SEG_BUDGET
> env-override scaffolding, which is harmless):**
>  - `ports/dos8086/mpconfigport.h`: MICROPY_HEAP_SIZE was reduced 20480 → **16384**
>    for instrumentation headroom — **RESTORE to 20480** before any real run.
>  - `ports/dos8086/main.c`: do_str() is set to the add() test — restore to
>    `do_str("print(1+2)", MP_PARSE_SINGLE_INPUT)` (or keep the add test until the
>    bug is fixed — your call).
>  - `py/vm.c`: VMK macro + M1/C0/C1 markers + an unused `vmhex` function — remove.
>  - `py/objfun.c`: FK macro + F0/Fe/Ff markers — remove.
>  - `py/bc.c`: SK macro + `skhex` fn + S0/S1/S2/S9/" Z"/" Y" markers + the
>    self->bytecode[0..15] dump loop — remove.
>  - `py/emitbc.c`: `ebhex` fn + the `LF`+kind+local_num print in
>    mp_emit_bc_load_local — remove.
>  - `py/parse.c`: the §2n 4-byte-alignment fix — **KEEP** (uncommitted, load-bearing).
>  - `mpy-cross/build/mpy-cross` exists; reference disasm recipe:
>    `printf 'def add(a,b):\n return a+b\n' > /tmp/t.py && mpy-cross/build/mpy-cross /tmp/t.py -o /tmp/t.mpy && python3 tools/mpy-tool.py -d /tmp/t.mpy`
>
> **HARNESS NOTES (learned the hard way this session — see
> [[feedback-victor-harness-deterministic]], updated):**
>  - Run via the Bash tool's NATIVE `run_in_background:true`, redirect script
>    stdout to a file (`tools/run-victor-sasi.sh build/mp-link/mpython.exe 60 > /tmp/run.log 2>&1`).
>    Do NOT wrap in `timeout` and do NOT rely on a long foreground call — the
>    harness auto-backgrounds those and the teardown fires the script's
>    `trap cleanup EXIT INT TERM` → `kill -9` MAME before any serial (symptom:
>    0-byte output file + no mame in `ps`).
>  - ALWAYS verify with `ps -eo pid,etime,comm | grep -i mame` — do not claim
>    "it's running" without checking.
>  - For HANG debugging use a SHORT `-seconds_to_run` (60), not 250: a hanging
>    guest runs the FULL N guest-seconds under -nothrottle = HOURS of wall time
>    (a 250s hang ran ~4h).  A successful run halts early.  Read the LIVE serial
>    capture (`$WORK/serial.txt`, path from `ps -ww` of the mame cmdline) to see
>    the trace up to the hang before MAME exits; then `kill -9` mame + sweep
>    orphan `sleep 1120` watchdogs.
>  - Fast inner loop: `tools/recompile-mp-tu.sh <base> <src>` rebuilds ONE TU and
>    relinks (reuses every other .obj + /tmp/mp_objs.txt + --pack-code).
>    Full build: `bash tools/build-micropython.sh --model=compact --keep-going`.
>
> ---
>
# (prior) Next session (§2p DONE — size shrink lever: `omf_link.py --pack-code` coalesces the gc-surviving per-function CODE segments back into a few <=64KB buckets, reclaiming the per-function paragraph padding. Compact far-data mpython image body 542528→537360 B (−5168), so the ~800 B of Victor headroom becomes ~6 KB. Flag-off is BYTE-IDENTICAL to the prior link (default path unchanged); flag wired into build-micropython.sh + recompile-mp-tu.sh. Packed mpython VERIFIED on the real Victor: full trace `C1 C2 C3 C4 D0 D1 D2 D3 3 D4 C5` — still prints `3`. No minic/qbe-backend change.)

> **§2p (DONE 2026-06-01) — SIZE SHRINK LEVER: `omf_link.py --pack-code`.**
> The user picked "size headroom" (the recurring wall — §2o shipped with only
> ~800 B under the ~824 KB Victor load ceiling).  This is a capability-free win
> (no heap/feature cut) entirely in the linker.
>
> ROOT OF THE WASTE: `--gc-sections` strips dead code at PER-FUNCTION
> granularity, so `asm_to_omf.py` emits one CODE segment per function and the
> linker placed each at its own paragraph base.  788 surviving CODE segments ×
> up to 15 B of paragraph padding = **5,855 B of inter-segment padding** in the
> core-subset image (measured from the map).
>
> FIX (`tools/omf_link.py`, opt-in `--pack-code`): after gc-sections decides
> liveness, greedily coalesce the live CODE segments (in module/SEGDEF order)
> into <=64 KB buckets (`CODEPACK<n>`), appending each function WORD-aligned
> instead of paragraph-aligned.  Functions are reached by offset within the
> bucket, not by their own segment selector, so paragraph alignment is
> unnecessary; the bucket itself is paragraph-aligned by `_layout_segments`.
> SOUND because every code reference is an offset-aware OMF fixup —
> `_resolve_target` returns `(out_idx, base+disp)` from `seg_map`, the 16-bit
> offset patch computes `tgt_abs_byte - frame_byte` (= the function's offset
> within its bucket), the SEG selector writes the bucket's `para_base`, and the
> far-ptr patch uses `tgt_byte_in_out`.  Self-relative (near) jumps stay
> intra-function, hence intra-bucket, so their displacement is preserved.  The
> DATA path already coalesces exactly this way (`_place_coalesced`).  Bucket cap
> `CODE_BUCKET_MAX = 65500` keeps every offset < 65536.
>
> RESULT: 788 CODE segs → **7 buckets**; inter-seg padding 5,855 → 341 B;
> compact far-data mpython image body **542,528 → 537,360 B (−5,168)**.  Headroom
> under the Victor ceiling goes from ~800 B to ~6 KB.  Relinking the SAME objects
> with the flag OFF is **byte-identical** to the committed `mpython.exe` (proves
> the default path is untouched — stronger than re-running the gate, which only
> ever links flag-off; the DOS gate is therefore green by construction).
>
> VERIFIED ON THE REAL VICTOR (MAME/SASI, packed image): full trace
> `C1 C2 C3 C4 D0 D1 D2 D3 3 D4 C5` — parse, compile, `print` emits `3`, module
> call returns clean.  Identical behaviour to §2o, 5 KB lighter.  (NB on a
> harness gotcha — NOT "MAME flakiness": run-victor-sasi.sh is DETERMINISTIC
> (fixed disk copy + `-seconds_to_run` + `-nothrottle`), so a deterministic
> emulator can't be intermittently flaky and a fixed boot can't make the guest
> nondeterministically hang.  My first run produced empty serial because it was
> auto-BACKGROUNDED by the shell tool and then torn down — the script's
> `trap cleanup EXIT INT TERM` (line 80) `kill -9`s MAME on any signal, and
> there was NO `mame` process at all when checked (a hung guest would show a
> live MAME pinning a core).  Run it in the FOREGROUND with an explicit wait and
> it completes every time.  If serial is ever empty, debug it for real — inspect
> the raw `$CAP`, check for the `__V9BEGIN__` sentinel and whether MAME reached
> `-seconds_to_run` — do not just re-roll.)  Flag wired into
> `tools/build-micropython.sh` and
> `tools/recompile-mp-tu.sh`; `tools/test_omf_link.sh` test1 (MZ correctness)
> passes.
>
> FURTHER LEVERS NOT YET TAKEN: the 45 per-module `FAR_DATA` segments are still
> paragraph-distinct (they carry far-addressed static data, each reached by its
> own `seg _sym` selector, so they CANNOT be blindly coalesced like CODE — a
> coalesced one would need every contained symbol re-based, doable but a bigger
> change).  Other directions from §2o remain open: broaden the REPL
> (multi-statement / arithmetic / variables / for — next latent Kl/far bug), and
> Finding 3 (raised-exception-object far ptr loses its SEGMENT on the raise
> path).  Build: `bash tools/build-micropython.sh --model=compact --keep-going`;
> run FOREGROUND: `tools/run-victor-sasi.sh build/mp-link/mpython.exe 250`.
>
> ---
>
# (prior) Next session (§2o DONE — 🎉 `print(1+2)` PRINTS `3` ON THE REAL VICTOR. The §2n "hangs inside `mp_arg_parse_all`" blocker was a minic call-ABI bug: a NARROW integer literal (`NULL`/`0`, always `w`) handed to a WIDE far-pointer parameter (`l`) was NOT widened, so the 2-byte push shifted every later stack arg. FIXED in `minic.y::coerce_arg`. DOS gate 169→172 green, `make check` green, 111 s/r 0 r/r. THE FIRST PYTHON STATEMENT RUNS END-TO-END ON 1982 HARDWARE.)

> **§2o (DONE 2026-06-01) — 🎉 MILESTONE: `print(1+2)` → `3` on the real Victor
> 9000 (MAME/SASI), reproducible.  The §2n "hangs inside `mp_arg_parse_all`"
> blocker was a minic call-argument-width bug — the REVERSE of the §2h/§2i
> wide→narrow shift.  DOS gate 169→**172**, `make check` green, 111 s/r 0 r/r
> (C-action only).  Fix + probe committed.**
>
> ROOT CAUSE (found by reading the generated SSA/asm — the static method, no
> MAME needed to localize it): `mp_builtin_print` calls
> `mp_arg_parse_all(0, NULL, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args,
> u.args)`.  The 2nd parameter `pos` is `const mp_obj_t *` = a far pointer (`l`,
> 4 bytes under far-data), but minic typed the `NULL` literal as integer `0`
> (`w`, 2 bytes) and emitted `call $mp_arg_parse_all(w 0, w 0, l %t7, w %t8, ...)`
> — a 2-byte push where the callee reads 4.  Every later stack arg shifted 2
> bytes: `n_allowed`, `allowed`, and `out_vals` were all read from the wrong
> slots, so the arg-parse loop ran wild over the 8-byte `u.args` stack union and
> corrupted the return frame → hang/reboot, before `3` could print.  (The
> `n_allowed = MP_ARRAY_SIZE = sizeof/sizeof` was a RED HERRING — it emits
> `%t8 =w div 16, 8` and the asm `idiv` correctly yields 2.)
>
> WHY §2i DIDN'T COVER IT: §2i's `coerce_arg` only fired when BOTH the arg and
> the param were INTEGER scalars (`arg_int && par_int`), to narrow/widen
> int<->int.  A pointer param has `KIND==PTR`, so `par_int` was false and the
> `NULL` arg was left as `w`.  But pointers are integers at the IR level (`w`
> near / `l` far), so a width mismatch against a pointer param shifts the stack
> exactly the same way.
>
> FIX (`minic.y::coerce_arg`): broaden the scalar test from "is an integer kind"
> to "is NOT a float and NOT a by-value aggregate" for both arg and param —
> i.e. treat pointers/functions as width-relevant scalars.  The existing
> widen (`w`→`l`: Con→retype to LNG, else extuw/sext) and narrow (`l`→`w`)
> machinery then matches the arg's IR class to the param's.  `NULL`/`0` to a far
> pointer param now emits `l 0`.  Floats and aggregates still bypass (aggregates
> cross by pointer via eval_arg/emit_arg; float<->int would be a real conversion,
> not a width fix).  Verified: the regenerated SSA is now
> `call $mp_arg_parse_all(w 0, l 0, l %t7, w %t8, ...)`.
>
> PROBE: `minic/dos/examples/nullarg_probe.c` (+golden), gated medium + compact +
> large.  Mirrors the bug shape — `report(0, NULL, 5, 7L)` (NULL as a non-last
> far-pointer arg, scalars after it) and `fill_one(NULL, 42, &dst)` (out-pointer
> last must still land).  Medium is a no-op (near pointers are `w`); compact/large
> are bug-loud without the fix.  This is the reverse companion to
> `argwiden_probe.c`.  See [[minic-wide-arg-narrow-param]].
>
> ON-TARGET RESULT (clean compact far-data build, body 823392 B, under the
> ~824.2KB Victor ceiling, 106/106 + --gc-sections): the full trace is
> **`C1 C2 C3 C4 D0 D1 D2 D3 3 D4 C5`** — parse (D2), compile (D3), then the
> builtin `print` emits **`3`**, then the module call returns clean (D4).
> Reproducible across runs.  **The first Python statement executes end-to-end on
> 1982 hardware.**
>
> NEXT (no blocker — pick a direction):
>  - **Broaden the REPL** to multi-statement / arithmetic / variables / `for`
>    and fix whatever the next latent Kl/far codegen bug is (the fragility notes
>    below still apply — layout shifts can flip a latent bug).
>  - **Size headroom** is the recurring wall (~800 B under the ceiling).  A real
>    shrink lever (dead-code, smaller heap-with-real-gc, code-seg packing) buys
>    room for a bigger feature set / instrumentation.  See §2b/§1z.
>  - Finding 3 (raised-exception-object far ptr loses its SEGMENT on the raise
>    path) is STILL open — needed before any uncaught exception can print
>    correctly; instrument `mp_raise`→`nlr_raise`→`nlr_jump`.
>  - MP tree state: `py/parse.c` = the §2n 4-byte-alignment fix (UNCOMMITTED in
>    the micropython repo, KEEP).  `ports/dos8086/` untracked (main.c C/D phase
>    markers + heap 20480 + real gc_collect).  All other py/*.c CLEAN.
>  - Build: `bash tools/build-micropython.sh --model=compact --keep-going`;
>    run FOREGROUND: `tools/run-victor-sasi.sh build/mp-link/mpython.exe 250`
>    (re-run on empty serial — MAME flakiness).
>
> ---
>
# Next session (§2n DONE — the §2m "module bytecode never calls print" blocker was TWO 4-byte-ALIGNMENT root causes. (1) parse-node chunk misalignment → compile emitted garbage; (2) const-object data misalignment (asm_to_omf dropped `.balign`) → call dispatch crashed. Both FIXED. `print(1+2)` now compiles CORRECTLY, the VM dispatches the calls, and `mp_builtin_print` is ENTERED. NEW blocker = hangs INSIDE `mp_arg_parse_all`. DOS gate 169/169 green; asm_to_omf fix committed `3f9f8c1`.)

> **§2n (DONE 2026-06-01) — the §2m "module bytecode never calls print" blocker
> was TWO independent 4-byte-ALIGNMENT bugs.  MicroPython's tagged pointers
> (`mp_obj_t` AND parse-node `mp_parse_node_t`) store kind/tag bits in a
> pointer's low 2 bits and REQUIRE every object & parse-node struct to be
> >=4-byte aligned (`(p & 3) == 0`).  On 16-bit (size_t=2, far ptr=4) two places
> violated that; on 32/64-bit the headers/objects are naturally >=4-aligned so
> upstream never hits it.  DOS gate 169/169 green.**
>
> FIX 1 — **parse-chunk header misalignment** (`~/projects/micropython/py/parse.c`,
> KEPT uncommitted in the MP tree).  `mp_parse_chunk_t` header = `size_t alloc`(2)
> + `union{size_t used; chunk* next}`(4) = **6 bytes** on i8086, so `chunk->data`
> — and hence EVERY parse-node struct — landed at offset ≡2 (mod 4).  Every
> struct node was then misclassified as a LEAF (`MP_PARSE_NODE_IS_STRUCT` =
> `(pn&3)==0` failed), so `compile_node(scope->pn)` saw the whole `print(1+2)`
> tree as a single `MP_PARSE_NODE_STRING` leaf → emitted ONE bogus
> `LOAD_CONST_STRING` (+ the module epilogue), never a CALL.  FIX in
> `parser_alloc()`: round each request up to a 4-multiple and align the FIRST
> allocation within each fresh chunk to a 4-byte boundary (subsequent allocs stay
> aligned since all node requests are 4-multiples; the chunk base from m_new is
> already GC-block(16)-aligned).
>
> FIX 2 — **const-object data misalignment** (qbe `tools/asm_to_omf.py`,
> COMMITTED `3f9f8c1`).  asm_to_omf DROPPED `.balign`/`.p2align`, so minic's
> careful 4-/16-byte alignment of file-scope aggregates collapsed to the
> segment's word(2) alignment.  `_mp_builtin_print_obj` landed at off=0x00DE
> (`0xDE & 3 == 2`) → `mp_obj_is_obj(&print_obj)` returned false →
> `mp_obj_get_type` fell to `types[o & 0xf]` and returned the WRONG type →
> `type->slots[slot_index_call-1]` dispatch crashed before reaching the builtin.
> FIX: translate `.balign N`/`.p2align k` to NASM `align` in the data/bss/`_HUGE_`
> buckets (drop only in `.text` — perf-only), and declare the data/bss segments
> `align=16` so the linker paragraph-aligns them and NASM accepts the in-segment
> align directives.  After the fix `_mp_builtin_print_obj` is at off=0x00E8
> (4-aligned).  Gate byte-output unchanged → **169/169 ok**.
>
> HOW FOUND (decisive method = read artifacts + minimal markers): instrumented
> the VM dispatch loop (`py/vm.c`, switch on `*ip`) → executed opcodes were
> `10 51 63` (LOAD_CONST_STRING + LOAD_CONST_NONE + RETURN), NOT the expected
> `11..11..83 34 34 59 51 63`.  Dumped the raw module bytecode (`08 02 07 10 81
> a0 3f 51 63`) and a reference via mpy-cross.  Traced `compile_node` →
> `compile_node(root)` took the `MP_PARSE_NODE_STRING` LEAF branch (root low
> nibble 6) → parse-chunk header math (6 bytes) pinned FIX 1.  After FIX 1:
> `CsCrKK` ×4 (compile correct, both call_function emits) + `F1` (VM dispatches a
> real CALL_FUNCTION) but a CRASH in the call dispatch → linked map showed
> `_mp_builtin_print_obj` at off=0xDE (2 mod 4) → asm_to_omf `.balign` drop → FIX 2.
> After BOTH: trace `C1..D3 FF P0` — compile completes, both calls dispatch, and
> **`mp_builtin_print` is ENTERED** (P0 marker).
>
> **THE NEW blocker — `print(1+2)` hangs INSIDE `mp_arg_parse_all`.**  Trace
> reaches `P0` (top of `mp_builtin_print` in py/modbuiltins.c) but the very next
> call, `mp_arg_parse_all(0, NULL, kw_args, n_allowed, allowed_args, u.args)`,
> never returns (a "Pa" marker right after it never printed; print(3) emits no
> `3`, and after the 2nd call the machine hangs/reboots).  `mp_map_lookup` on the
> empty fixed kw map is safe (linear scan, no `%alloc` div-by-zero — it's
> `is_ordered`).  PRIME SUSPECTS: (a) reading the function-local `static const
> mp_arg_t allowed_args[]` from far data (alignment / far-load of the struct
> array), or (b) a miscomputed `n_allowed = MP_ARRAY_SIZE(allowed_args)` =
> `sizeof(arr)/sizeof(arr[0])` on a local static-const struct array → loop runs
> wild.  NEXT: add a 16-bit marker just inside `mp_arg_parse_all` and one in the
> loop (put any `extern void mp_hal_stdout_tx_strn_cooked(const char*, size_t);`
> at FILE scope — an in-FUNCTION-body prototype made minic parse-error), OR
> verify `sizeof(allowed_args)` / each `allowed[i].flags` read on-target; print
> `n_allowed`.  Suspect a remaining far-load/sizeof miscompile.
>
> **CAVEATS for next session (the build is fragile):**
>  - **Heap**: heap=16384 is TOO SMALL — `mp_compile` exhausts it and the trace
>    STOPS AT D2 (it's OOM, not a codegen bug).  heap=20480 (current
>    mpconfigport.h) compiles fine.  heap=22528 also works but the alignment-fix
>    bloat pushes the body to 824816 (> ~824192 ceiling) → won't load.  Keep heap
>    <= 20480 with the alignment fix.
>  - **Layout fragility**: tiny instrumentation changes flip a LATENT compile-path
>    Kl/far codegen bug — symptoms range from D2-stop to EMPTY serial output.
>    Add markers in MINIMAL increments and re-confirm D3 each time.  (The 3-marker
>    P0/Pa/P1 build compiled; adding 2 more flipped it to D2.)  This still hints
>    more latent Kl/far bugs remain (the D3 success is not robust to layout).
>  - **MAME flakiness**: the SASI harness intermittently produces EMPTY serial
>    output (exit 0, no C1).  Just RE-RUN.  Run FOREGROUND (background runs seem
>    to lose serial more often).
>  - Build: `bash tools/build-micropython.sh --model=compact --keep-going`
>    (full, ~1min, applies alignment everywhere — REQUIRED after touching any TU
>    if you want consistent alignment), or `tools/recompile-mp-tu.sh <base> <src>`
>    (one TU + relink).  Run: `tools/run-victor-sasi.sh build/mp-link/mpython.exe 250`.
>  - MP tree state: `py/parse.c` = the alignment FIX (uncommitted, KEEP).
>    `ports/dos8086/` untracked (main.c has C1..D4/DE phase markers, gc_collect is
>    the real collector, heap 20480).  All other py/*.c reverted CLEAN.
>
> ---
>
# Next session (§2m DONE — the §2l "print emits no visible output" blocker was TWO minic static-data/typing bugs in the int-print SLOT DISPATCH, now FIXED and committed `2fd2f4b`. `make check` green, both proven on the real Victor. NEW blocker = `print(1+2)`'s MODULE BYTECODE never calls `print` at all — a compile/VM bug, NOT an output bug.)

> **§2m (DONE 2026-06-01, committed `2fd2f4b`) — the §2l "print emits no
> visible output" blocker is RESOLVED.  It was TWO minic bugs in how
> `mp_obj_print_helper` reaches the int formatter via the type-slot far
> fn-ptr dispatch.  `make check` green (111 s/r, 0 r/r), amd64/arm64/rv64
> byte-identical.  Both found by on-target bisection on the real Victor.**
>
> HOW FOUND (the decisive method again): added a DISCRIMINATOR in
> `ports/dos8086/main.c` that called `mp_obj_print_helper(&mp_plat_print,
> MP_OBJ_NEW_SMALL_INT(3), PRINT_STR)` DIRECTLY (bypassing the VM/bytecode) —
> it HUNG.  Then instrumented `mp_obj_print_helper` (`Y0`/`Y1`/`Y2`/`Y3`) and
> `mp_obj_int_print` (`Z0`): trace `Y0 Y1 Y2` then hang, NO `Z0` — so
> `mp_obj_get_type` returned (Y1), `HAS_SLOT(print)` was true (Y2), but the
> far slot-dispatch call `MP_OBJ_TYPE_GET_SLOT(type,print)(...)` never reached
> `mp_obj_int_print`.  A DIRECT call to `mp_obj_int_print` (bypass the slot)
> printed `3` fine → the formatter is correct; the BUG is the slot load+call.
> A seg:off dump at the call site showed the returned `type` segment was
> STACK GARBAGE (`c5ea`) not `&mp_type_int` (`c330`), and the loaded slot fn
> ptr had segment `0000`.  Reading the generated asm pinned both root causes:
>
> BUG 1 — **flexible-array-member initializer emitted ONLY its first element.**
> `mp_obj_type_t` ends in `const void *slots[]`.  minic's `agg_emit_struct`
> routed the braced `.slots = {make_new, print, ...}` through
> `agg_emit_value`→`agg_emit_scalar` (a flex member has `count==0`), emitting
> just `slots[0]`.  So `slots[1]` (the `print` fn) read past-the-array garbage
> and the far call jumped wild.  Verified in the emitted data: `_mp_type_int`
> stopped after `dd _mp_obj_int_make_new` (`; end data`).  FIX (`minic.y`):
> detect `m->isflex`, count brace elements (new `agg_brace_count`, honours
> `[k]=v`), emit the whole array via `agg_emit_array`.
>
> BUG 2 — **array-of-pointers subscript used the wrong stride.**
> `mp_obj_get_type` returns `types[(uintptr_t)o & 0xf]` from a static
> `const mp_obj_type_t *const types[]`.  `array_vartyp` registered the array
> with the ELEMENT (pointer) type itself, so `types[i]` scaled by
> `sizeof(*T)` (= 20, `sizeof(mp_obj_type_t)`) instead of `sizeof(T*)` (= 4).
> The asm showed `mov bx,20; imul bx` — `types[7]` indexed at base+140, way
> out of the 16×4-byte array → a wrong-segment type ptr.  FIX (`minic.y`):
> `array_vartyp` always returns `IDIR(elemtyp)` (a C array of T decays to T*;
> `T *arr[]` decays to `T **`).  Byte-identical for scalar/struct-element
> arrays; only pointer-element arrays were mistyped.  Asm after the fix:
> `mov bx,4; imul bx`.
>
> VERIFIED on the real Victor: with both fixes the slot dispatch reaches
> `mp_obj_int_print` and prints `3` (`Y2 c330 c330 5a80 Z0 3 Y3` — returned
> type seg == &mp_type_int seg, slot fn ptr in a real code segment).  Gate
> probe `slotarray_probe.c` (compact + far-static, MicroPython's config)
> exercises BOTH bugs (a flexible fn-ptr-member dispatch + a runtime-indexed
> `int *` array); passes on the Victor via the MAME/SASI harness.
> fnptrprobe re-verified unchanged.  **Full DOS gate GREEN: 168→169/169 ok**
> (the new slotarray_probe row; no regressions from the two minic fixes).
> NOTE: `tools/run-dos-exe.sh` had a macOS launch bug — it ran `open -gjWn -a
> dosbox.app --args …`, but `open -a <bundle> --args` does NOT forward flags
> to DOSBox (every probe reported "no OUT.TXT").  Fixed (committed `78f1cf1`)
> to `open -a <binary> -g -j -W -n --args …` (point at
> .../Contents/MacOS/DOSBox).  The MAME Victor harness (run-victor-sasi.sh /
> run-victor-mame.sh) is the other validation path and works headlessly.
>
> SIZE NOTE: the BUG 1 fix correctly emits all the previously-dropped type
> slots, so the compact far-data mpython body grew 823360 → **824928 B**,
> OVER the ~824.2KB Victor load ceiling.  To get a loadable image for the
> milestone run, `ports/dos8086/mpconfigport.h` `MICROPY_HEAP_SIZE` was
> reduced 24576 → **22528** (still ample for `print(1+2)` with working
> gc_collect; print isn't even called yet so heap is moot for now) → body
> 822880, loads.  This is an UNCOMMITTED micropython-tree change; revisit
> with a real shrink lever (the ceiling is the recurring wall — see §2b/§1z).
>
> **THE NEW blocker — `print(1+2)`'s MODULE BYTECODE never calls `print`.**
> With both fixes + the smaller heap, the full pipeline runs clean:
> `C1..C4 D0 D1 D2 D3 D4 C5` (parse, compile, execute all complete, NO
> exception) — but STILL no `3`.  Since the print OUTPUT path is now PROVEN
> working (the slot dispatch printed `3` from main.c's direct call), the only
> remaining explanation is that the compiled module function doesn't call
> `print`.  CONFIRMED on the Victor: a marker at `mp_call_function_n_kw`
> (py/runtime.c — the universal call dispatcher the VM's MP_BC_CALL_FUNCTION
> at py/vm.c:984 routes through) printing `F<n_args>` fired EXACTLY ONCE as
> `F0` (the outer `mp_call_function_0(module_fun)` from main.c) — there was
> NO second call (no `F1`).  So the module function executes and returns
> cleanly but never dispatches a `CALL_FUNCTION` for `print`.  This is a
> COMPILE or VM bug (the expression statement `print(1+2)` either compiled to
> bytecode with no working CALL, or the VM exits/mis-dispatches before
> reaching MP_BC_CALL_FUNCTION).  NEXT: instrument the VM opcode dispatch loop
> in `py/vm.c` (a minimal per-opcode marker, or specifically the
> MP_BC_CALL_FUNCTION / MP_BC_LOAD_NAME / MP_BC_LOAD_GLOBAL / MP_BC_RETURN
> entries) and/or dump the module function's raw bytecode to see whether the
> CALL opcode is present (compile bug) or present-but-not-executed (VM
> dispatch bug — likely another Kl/far-data miscompile in the
> computed-goto/opcode-fetch).  Use 16-bit-only debug printers (no Kl shift).
> Build: `bash tools/build-micropython.sh --model=compact --keep-going`; run:
> `tools/run-victor-sasi.sh build/mp-link/mpython.exe 200`.  The
> micropython tree is CLEAN except the untracked `ports/dos8086/` port (C/D
> markers in do_str + the 22528 heap).  Finding 3 (raised-exc-obj far ptr
> loses its segment) is STILL open but does NOT block (no exception raised).

> **§2l (DONE 2026-06-01, committed `ed5f35b`) — the §2k "uncaught exception
> during mp_compile" is FIXED.  It was a general QBE i8086 BACKEND codegen bug in
> the Kl (32-bit) bitwise `Oand`/`Oor`/`Oxor` handlers.  DOS gate 166→**168**,
> `make check` green, amd64/arm64/rv64 byte-identical.  All in `i8086/emit.c` +
> a new gate probe.**
>
> ROOT CAUSE: the Kl `Oand`/`Oor`/`Oxor` handlers used `load32_dxax` + the op,
> operating in AX/DX, but NEVER preserved the caller's AX/DX — unlike the Kl
> `Oadd`/`Osub`/`Omul` handlers, which bracket with `kl_save_axdx`/`kl_restore_axdx`
> ([[i8086-kl-add-sub-mul-r1-alias]]).  rega doesn't model the implicit AX/DX
> clobber of these ops, so a live SSA value rega parked in AX/DX across a Kl
> OR/AND/XOR was silently corrupted.  (CLAUDE.md's open [[i8086-kl-shift-clobbers-ax]]
> note was STALE — the Kl SHIFT handlers already had the save/restore bracket; it
> was the logical ops that were unfixed.)
>
> CANONICAL VICTIM (found by on-target bisection on the real Victor, the decisive
> method again): MicroPython's `MP_BC_PRELUDE_SIG_ENCODE` (py/bc.h).  `mp_uint_t`
> is `uintptr_t` = 4 bytes under far-data, so the module prelude's
> `while (S|E|F|A|K|D)` condition lowers to a chain of Kl ORs.  rega kept the
> loop-carried byte `z` (the encoded prelude byte, used by both the loop body's
> `0x80|z` and the post-loop `out_byte(z)`) live in AX across the OR-chain; the
> OR-chain clobbered AX, so the encoder emitted a bogus multi-byte prelude
> (`0x82 0x81 0x00` instead of `0x08`).  Decoded back in `mp_setup_code_state`,
> that read as a module taking 2 positional args + 1 default, so `fun_bc_call`
> raised TypeError on `mp_call_function_0` of `print(1+2)` — the long-standing
> "exception during compile/exec" blocker.
>
> HOW FOUND (one decisive marker run at a time): instrumented `py/compile.c`
> (compile COMPLETED — the exception had MOVED to execution), then `py/objfun.c`
> `fun_bc_call` (raise was between `mp_cstack_check` and `mp_execute_bytecode`,
> in `INIT_CODESTATE`→`mp_setup_code_state`), then `py/bc.c`
> `mp_setup_code_state_helper` (decoded `n_pos_args=2, n_def=1` — wrong; raw
> prelude bytes `0x82 0x81 0x00`), then `py/emitbc.c` `mp_emit_bc_start_pass`
> (encode INPUTS correct: `num_pos_args=0`, `n_state=2`; but `code_base[0]`
> written = `0x82`, should be `0x08`).  Then read the generated SSA/asm:
> `mp_uint_t` ops were typed `l`, the `while`-condition OR-chain (asm lines
> "mov ax,[slot]; or ax,[slot]; mov [slot],ax", NO push/pop) clobbered the `z`
> value rega had loaded into AX before the loop.
>
> FIX (`i8086/emit.c`): bracket each of Kl `Oand`/`Oor`/`Oxor` with
> `kl_save_axdx`/`kl_restore_axdx` and handle the dst-in-DX RTmp case, mirroring
> `Osub` Kl exactly.  Slot operands are bp-relative so the AX/DX push/pop (which
> move SP, not BP) leaves their offsets valid.  Probe `sigencode_probe.c`
> (medium + compact) replicates the encode with `unsigned long` (Kl) inputs;
> bug-loud (`b0=0`) without the fix, `b0=8` with.  Wired into `tools/test-dos.sh`
> RUNTIME_TESTS.  (Note: the isolated probe needs the Kl-OR-chain `while`
> condition + a loop-carried value to trigger — a too-minimal probe folds clean.)
>
> ON-TARGET RESULT (clean full rebuild, body 823360 B, under the ~824.2KB
> ceiling): the trace advanced from `C1..C4 D0 D1 D2 DE` to
> **`C1 C2 C3 C4 D0 D1 D2 D3 D4 C5`** — `mp_compile` returns (D3) AND
> `mp_call_function_0` returns with NO exception (D4).  Confirmed reproducible.
>
> **THE NEW blocker — `print` emits NO VISIBLE OUTPUT.**  `print(1+2)` executes
> cleanly (D3→D4) but the `3` never appears (raw `od -c` of the serial capture
> shows ONLY the C/D markers — no `3`, no `\n`).  `print` routes through
> `mp_plat_print` → `MP_PLAT_PRINT_STRN` → `mp_hal_stdout_tx_strn_cooked` — the
> SAME path the C/D markers use (and those DO appear) — so EITHER (a) the module
> bytecode never actually calls `print` (a remaining VM/bytecode miscompile that
> raises no exception), OR (b) `print` is called but `mp_obj_print_helper(int 3)`
> / `mp_print_int` produces zero output (a far-data int→string bug).
>
> **CAUTION for the next session — razor-thin SIZE margin + instrumentation
> heisenbugs.**  The clean image body is 823360 B; the Victor ceiling is
> ~824192 B (loads) / 824512 B (does not) — only ~830 B of headroom.  Worse, a
> small marker added to `py/modbuiltins.c` (mp_builtin_print entry) regressed the
> trace to `D2` EVEN THOUGH the image still loaded (823824 B < ceiling) — a
> LAYOUT-induced compile-path codegen bug, not a load failure.  So: instrument
> with MINIMAL net size growth, prefer TUs NOT on the compile path, and/or
> temporarily stub something (e.g. `gc_collect` empty) for headroom; re-confirm
> the baseline `D3 D4` after any change.  This fragility also hints MORE latent
> Kl/far-data codegen bugs remain — the D4 success is correct but not robust to
> layout shifts.
>
> NEXT: (1) determine if `print` is CALLED — instrument the VM call dispatch
> (`fun_builtin_var_call` in py/objfun.c, or the `MP_BC_CALL_FUNCTION` opcode in
> py/vm.c) with a MINIMAL marker (watch size).  If NOT called → a bytecode/VM
> miscompile (likely another Kl/far op); bisect the emitted opcodes for the
> module.  (2) If called → trace `mp_obj_print_helper`→`mp_print_int`/the int
> formatter for the lost output.  Use 16-bit-only debug printers (no Kl shift).
> Build: `bash tools/build-micropython.sh --model=compact --keep-going`; run:
> `tools/run-victor-sasi.sh build/mp-link/mpython.exe 200`.  MicroPython tree
> (~/projects/micropython, separate repo) is CLEAN except the untracked
> `ports/dos8086/` port; the C/D phase markers in `main.c::do_str` remain.
> Finding 3 (raised exc obj far ptr loses its segment) is STILL open but does NOT
> block (no exception is raised now).
>
> ---

> **§2k (DONE 2026-06-01) — the §2j' "mp_compile EMIT-pass hang" is FIXED, and the
> §2j' emitbc.c jump-offset/size_t-width hypothesis was WRONG.**  The real bug was a
> general QBE i8086 BACKEND codegen bug in the Kw shift handler.  DOS gate 164→**166**,
> `make check` green.  All in `i8086/emit.c` + a new gate probe.
>
> ROOT CAUSE: the Kw shift handler (`Oshl`/`Oshr`/`Osar`, `cls != Kl`) only
> materialized the VALUE operand `arg[0]` into the destination register when it was an
> `RTmp` the allocator had already placed there.  When `arg[0]` was an `RCon`
> (constant) or `RSlot`, it was NEVER loaded — the shift ran on whatever the dest reg
> held, which for `CONST << count` was the freshly-computed COUNT.  So `1 << count`
> emitted `count << count`, and `1 << 0` → `0 << 0 == 0`.  (minic SSA was correct:
> `%t =w shl 1, %count`.)
>
> CANONICAL VICTIM: `py/gc.c` `gc_alloc`'s ATB head-mark
> `gc_alloc_table_start[b/4] |= (AT_HEAD << (2*(b&3)))` = `1 << (2*(b&3))`.  For a
> block whose index is divisible by 4 the shift is 0, so the mark became `|= 0` — a
> NO-OP.  The block was never recorded used, so the NEXT `gc_alloc` handed out the
> SAME live block and `m_new0` zero-filled it.  In the port this zeroed a freshly-
> allocated `scope`'s `pn` (parse-tree root) — set correctly by `scope_new`, then
> wiped by the immediately-following `raw_code = m_new0(...)` which returned the SAME
> address as `scope`.  `compile_node(scope->pn)` then saw NULL → the CODE_SIZE pass
> emitted nothing → `code_base` was sized at ~2 bytes → the EMIT pass overflowed it
> (~24 bytes) → GC-heap corruption → the observed "EMIT pass size won't settle / 3rd
> pass hangs."
>
> HOW FOUND (on-target bisection, the decisive method again): instrumented
> `emitbc.c`/`compile.c`/`scope.c` with 16-bit-only hex printers over
> `run-victor-sasi.sh`.  Trace showed CODE_SIZE emitted 2 bytecode ops vs EMIT's 12
> (opcodes differed: CODE_SIZE = just the module epilogue), then `scope->pn` read 0 at
> compile but was set correctly in `scope_new`, then `raw_code`'s `m_new0` returned the
> SAME address as `scope` → read the `gc_alloc` marking asm → `shl ax, cl` with `ax`
> holding the COUNT, never loaded with 1.
>
> FIX (`i8086/emit.c`): new `emit_shift_val(reg, r0, fn, f)` materializes the value
> operand (RTmp / RCon incl. CAddr / RSlot) into the shift register, emitted AFTER the
> count is secured into CL so a count register that aliases the destination is read
> before being overwritten.  The `dst==CX` via-BX path orders the value-save vs count-
> load per operand kind.  Probe `shlconst_probe.c` (+golden, medium+compact): the
> `gc_alloc` ATB pattern (`atb[b/4] |= 1<<(2*(b&3))` for 8 blocks → 0x55/0x55) plus
> direct `CONST<<var`; bug-loud (atb garbage) without the fix.  Wired into
> `tools/test-dos.sh` RUNTIME_TESTS.
>
> RESULT: clean compact far-data `mpython.exe` body 820480→**820624 B** (the fix adds a
> `mov reg,const` before some shifts; still well under the ~824.3KB ceiling).  On the
> real Victor (SASI) the EMIT-pass hang is GONE — the trace advanced from `D2`(hang) to
> **`D0 D1 D2 DE C5`**: `mp_parse` returns (D2), `mp_compile` now runs to completion-or-
> raise without hanging, and raises an exception (DE) before D3.
>
> **THE NEW blocker — an uncaught exception DURING `mp_compile`** (trace `D0 D1 D2 DE`,
> DE = `do_str`'s nlr-else branch in `ports/dos8086/main.c`, between parse-done D2 and
> compile-done D3).  This is the SAME shape as the §2i-era compile exception; with
> `gc_alloc` now correct, `mp_compile` is operating on un-corrupted data but still
> raises.  NEXT: instrument `py/compile.c` (and the emit path) to find WHERE in compile
> it raises and WHAT exception (likely a `comp->compile_error` setter, or a real raise
> from a remaining far-data miscompile).  Use 16-bit-only debug printers (no Kl shift).
> Note Finding 3 is STILL open: a raised exception object's far pointer loses its
> SEGMENT on the raise path (offset kept, segment garbage) — so even a legitimate error
> will print wrong / crash `mp_obj_print_exception`; pin that on the raise path
> (`mp_raise`→`nlr_raise`→`nlr_jump`) too.  Build: `bash tools/build-micropython.sh
> --model=compact --keep-going`; run: `tools/run-victor-sasi.sh build/mp-link/mpython.exe
> 200`.  The C/D phase markers in main.c::do_str remain; the MicroPython tree is
> otherwise CLEAN (all §2k instrumentation reverted).  fnptr_argwiden_probe.c (ungated)
> documents the still-latent fn-ptr wide→narrow arg-coercion gap (a prime suspect for
> the compile exception if it's an arg-shift through an emit_t-style fn-ptr call).
>
> ---

# (prior) Next session (§2j' DONE — the ~850 B shrink lever landed: emit_clit_aggr now fills local-init dest DIRECTLY, dropping the compound-literal temp + struct copy and fixing far member-init truncation; committed `df3c76a`, DOS gate 164/164, `make check` green, 111 s/r 0 r/r) — the compact far-data mpython image now LOADS on the real Victor (body 825152→820480 B, UNDER the ~824.3KB ceiling) and the §2j compile_error fix is HARDWARE-CONFIRMED (the §2i/§2j bogus `DE` exception is GONE). The `print(1+2)`→`3` milestone is STILL not reached: a NEW blocker is now exposed — `mp_compile` HANGS in the bytecode EMIT pass. NEXT = debug the EMIT-pass hang (precisely localized below; almost certainly an emitbc.c/compile_scope far-data codegen bug, NOT the struct-init change).

> **§2j' (DONE 2026-06-01, committed `df3c76a`) — the ~850 B shrink lever (the
> §2j NEXT) landed and is hardware-verified.**  DOS gate 164/164, `make check`
> green, 111 s/r 0 r/r (C-action only).
>
> WHAT LANDED (all `minic/minic.y`): `emit_clit_aggr` now takes a destination
> ADDRESS operand (any aggregate lvalue — a `%_clit` slot, a local `%var`, a
> `*p` deref temp) instead of only a `%_clit` number, and is FAR-CORRECT (member
> stores are `storef%c` at `=l add` offsets under far-data; the old `%_clit`-only
> path used near `=w add`+`store%c`, which TRUNCATED the segment of a pointer
> member written at offset>0 — a latent miscompile).  New `symb_operand` helper
> formats a Symb's address operand to a string.  The `=` handler special-cases
> `dst = (T){...}` (the desugaring the local-aggregate-init rules emit, and any
> user struct-literal assign): it fills `dst` IN PLACE via `emit_zero_aggr` +
> `emit_clit_aggr`, skipping the compound-literal temp AND the whole-struct copy.
> That drops far more than 850 B (every `S s = {...}` loses its temp alloc +
> ~size/2-word copy) AND fixes far-data member-init truncation for locals (the
> old non-zero path went through `emit_struct_copy`, which for a local aggregate
> computed `dst_far=false` under far-data → near stores into a far stack slot).
> Probe `local_zeroinit_probe.c` gains `init`/`mid2`/`desig` NON-zero cases
> (positional `small_t` + designated `big_t`) that DEREF a pointer member written
> past the first word, so a far-store segment truncation is bug-loud; medium is
> byte-identical (far=0 path unchanged).
>
> RESULT ON HARDWARE: compact far-data `mpython.exe` body 825152→**820480 B**
> (−4672), UNDER the ~824.3KB Victor load ceiling (824192 loads, 824512 doesn't).
> On the real Victor (SASI) the image LOADS and runs through `mp_init` +
> `mp_parse`: trace `C1 C2 C3 C4 D0 D1 D2`.  The §2i/§2j bogus compile-time `DE`
> exception is GONE (the §2j `compile_error` fix is hardware-confirmed; before, it
> was unverifiable because the image was over the ceiling).
>
> **THE NEW blocker — `mp_compile` HANGS in the bytecode EMIT pass.**  Bisected
> on-target with E-markers in `py/compile.c::mp_compile_to_raw_code` (markers
> since REVERTED — tree is clean; re-add via the recipe below).  Trace:
> `D2 E0 E1 E2 E3 Es Ec Ee Ei Ei` then HANG.  Decode:
>  - `E0`→`E3`: scope creation + `MP_PASS_SCOPE` loop + `scope_compute_things` all
>    COMPLETE.
>  - `Es`: `compile_scope(MP_PASS_STACK_SIZE)` ran and RETURNED.
>  - `Ec`: `compile_scope(MP_PASS_CODE_SIZE)` ran and RETURNED.
>  - `Ee`: entered the `while (!compile_scope(comp, s, MP_PASS_EMIT)) {}` loop.
>  - `Ei Ei`: the EMIT `compile_scope` returned FALSE (= `mp_emit_bc_end_pass`
>    reported the emitted size did NOT settle: `bytecode_offset != bytecode_size`
>    or `code_info_offset != code_info_size`, emitbc.c:384) on TWO successive
>    passes, requesting another pass each time.
>  - HANG: the **3rd** `compile_scope(MP_PASS_EMIT)` call never returns (no 3rd
>    `Ei`, no `Ed`/`E4`).  Even at 300s emulation it stays at the hang (true hang,
>    not slow; no reboot — no 2nd `__V9BEGIN__`).
> Bytecode emit normally converges in ONE pass; two non-settling passes is itself
> abnormal, so the bytecode/code-info SIZE is oscillating across EMIT passes —
> classic symptom of a miscompiled jump-offset / size accumulator under far-data
> (`bytecode_offset`/`code_info_offset` are `size_t` = `int` = 2 bytes in minic's
> stddef.h; the jump-encoding at emitbc.c:242-279 computes signed
> `label_offsets[label] - bytecode_offset - 2` and picks 1/2/3-byte encodings —
> a width/sign bug there would shift sizes every pass and never settle, and the
> 3rd pass walking a corrupted offset could loop).
>
> WHY THIS IS ALMOST CERTAINLY NOT THE §2j' STRUCT-INIT CHANGE: STACK_SIZE,
> CODE_SIZE, and TWO full EMIT passes all ran `compile_scope` to completion over
> the SAME struct initializers; a miscompiled local-aggregate init would corrupt
> every pass uniformly, not specifically hang the 3rd EMIT pass on a size-
> convergence path.  (If you want to be 100% sure, the only `S s = {...}`-shaped
> locals on the compile path are the suspect — but the gate's `local_zeroinit_probe`
> init/mid2/desig cases already pass on compact+large.)
>
> **NEXT — pin the EMIT-pass hang.**  (1) Instrument `py/emitbc.c`:
> `mp_emit_bc_end_pass` (print `bytecode_offset`/`bytecode_size`/`code_info_offset`/
> `code_info_size` each EMIT pass — see if they oscillate vs. grow unboundedly)
> and the jump-offset encoder `emit_write_bytecode_byte_signed_label`-style code
> at emitbc.c:242-279 (print `label`, `label_offsets[label]`, `bytecode_offset`,
> the chosen encoding size).  Use 16-bit-only debug printers (NO Kl shift —
> [[i8086-kl-shift-clobbers-ax]]).  (2) Recompile that ONE TU via
> `tools/recompile-mp-tu.sh emitbc ~/projects/micropython/py/emitbc.c` (compact
> far-data, --gc-sections; reuses every other .obj + /tmp/mp_objs.txt), run
> `tools/run-victor-sasi.sh build/mp-link/mpython.exe 200`, and watch the offsets.
> (3) Likely fix is in i8086/qbe codegen for the offset arithmetic / size_t
> compares (a width or sign-extension bug), or possibly in how `label_offsets[]`
> (a far array under far-data) is indexed/stored.  RE-ADD the compile.c E-markers
> recipe (if you need to re-confirm the phase): `#include "py/mphal.h"` +
> `#define EMARK(s) mp_hal_stdout_tx_strn_cooked((s),3)` after
> `#if MICROPY_ENABLE_COMPILER`, then `EMARK("E0\n")` at the top of
> `mp_compile_to_raw_code`, `E1` after `emit_bc_new`, `E2`/`E3` around the
> `scope_compute_things` loop, `Es`/`Ec`/`Ee` before the STACK_SIZE/CODE_SIZE/EMIT
> `compile_scope` calls, `Ei` inside the EMIT while-body, `Ed`/`E4` after the loop.
> CAUTION: markers cost image bytes — the clean image is 820480 B with ~3.7KB of
> headroom under the ceiling; a handful of EMARKs fit, but strip them before any
> milestone confirmation run.  Build: `bash tools/build-micropython.sh
> --model=compact --keep-going`.
>
> MICROPYTHON TREE STATE (separate repo, ~/projects/micropython, uncommitted):
> `py/compile.c` reverted CLEAN (E-markers removed).  `build/mp-link/mpython.exe`
> rebuilt clean (compile.obj marker-free, body 820480 B).  The C/D phase markers
> in `ports/dos8086/main.c::do_str` remain (D0=lexer, D1=before mp_parse, D2=parse
> done, D3=compile done, D4=call done, DE=exception).  `gc_collect` is the real
> stack-scanning collector.
>
> ALSO STILL OPEN (unchanged): Finding 3 — a raised exception object's far pointer
> loses its SEGMENT on the raise path; needed for any real exception to print, but
> `print(1+2)` raises none, so it does not block the milestone.

> # (prior) §2j — the `mp_compile` exception blocker (FIXED in f8040ee; this is the predecessor of §2j' above)

> **§2j (DONE 2026-06-01, committed `f8040ee`) — the §2i NEW blocker (an
> uncaught exception raised DURING `mp_compile` of the correct parse tree of
> `print(1+2)`) is FIXED.  It was NOT the suspected fn-ptr arg-coercion shift —
> that suspicion was WRONG for this config (MicroPython MINIMUM-ROM has
> `MICROPY_EMIT_NATIVE=0`, so `EMIT_ARG` is a DIRECT `mp_emit_bc_*` call, NOT
> emit_t method-table dispatch — already coerced by §2i).  DOS gate 161→164,
> `make check` green, 111 s/r 0 r/r (C-action only).**
>
> HOW IT WAS FOUND (on-target bisection, the decisive method): injected K0..K3
> phase markers + a Z0/Z1 init probe into `py/compile.c::mp_compile_to_raw_code`
> on the real Victor (SASI).  Trace showed compile ran ALL passes (`K3`) then hit
> `comp->compile_error != NULL` though `SYN` (the only compile_error setter once
> native is off) never printed — and `Z1` fired: `compiler_t comp_state = {0};`
> left the `compile_error` POINTER field (past the first word) as non-NULL stack
> GARBAGE.  So `mp_compile` raised a bogus exception on a perfectly good parse
> tree.  (Lesson reinforced: get on-target DATA before building a big speculative
> fix — the fn-ptr mechanism would have been wasted effort.)
>
> ROOT CAUSE: every minic local-aggregate zero-init looped `j += 4` while
> emitting a 2-byte `storew` (T.wordsz == 2 on i8086), zeroing only HALF the
> bytes — alternating 2-byte gaps of stack garbage; under far-data the near
> `=w add`/store also truncated the segment.  FIVE sites had this (the two
> compound-literal 'L' paths + three bare struct-local decl paths).
>
> FIX (all `minic/minic.y`): new `emit_zero_aggr(addr, s)` — correct full-
> coverage zero-fill (a `memset` call for s>8, mangled to `_far_memset` under
> far-data; tiny aggregates inline storel/storew/storeb with a 4-byte stride);
> all five sites route through it.  Implicit bare `struct S s;` zero-init is now
> gated on the struct actually having a bitfield (`struct_has_bitfield`) — C
> leaves an uninitialized automatic indeterminate, the old half-fill couldn't be
> relied on, and most structs have no bitfield (net size win).  `S s = {0};`
> locals (dcls + stmt rules) now zero the target DIRECTLY via `memset`, skipping
> the compound-literal temp AND the struct copy.  Probe `local_zeroinit_probe.c`
> (+golden), gated medium+compact+large; bug-loud (all members garbage) without
> the fix.  Also added `fnptr_argwiden_probe.c` (UNGATED) documenting the
> separate, still-latent fn-ptr wide→narrow arg gap (fn-ptr TYPE carries no
> param list; not hit by this config).
>
> **THE NEW state — image is ~850 B OVER the Victor load ceiling.**  Correct
> (full) zeroing legitimately costs more code than the buggy half-fill, so the
> compact far-data mpython.exe body grew to **825152 B**, and DOS reports
> "Program too big to fit in memory" on the Victor (observed ceiling: a 824192 B
> body LOADS, 824512 B does NOT — so ~824.3 KB).  Three shrink levers already
> applied this session got it from 828896→825152 (memset-for-big, direct-`{0}`
> on-target, bitfield-conditional bare-decl), but ~850 B remain.  The on-target
> `print(1+2)` → `3` MILESTONE is therefore NOT yet verified — it is blocked ONLY
> by this size wall, not by correctness (the fix is proven by the probe across 3
> models + the Z1 on-target root-cause).
>
> **NEXT — shrink ~850 B, then verify the milestone on the Victor.**  RECOMMENDED
> LEVER (clean, also fixes a latent bug): generalize `emit_clit_aggr` to take a
> destination ADDRESS (like `emit_zero_aggr` now does) and be far-correct
> (it currently uses near `=w add %_clit, off` + `store%c` for explicit members
> at offset>0 — which TRUNCATES the segment under far-data, a latent bug), then
> have the local-aggregate-init rules (dcls `type IDENT '=' '{' … '}'` ~line
> 6755, and the stmt-level one ~line 7360) fill the freshly-alloc'd `%v`
> DIRECTLY — zero it via `emit_zero_aggr("%v", s)` then `emit_clit_aggr("%v", …)`
> — eliminating the compound-literal temp AND the per-init struct copy for ALL
> aggregate initializers (not just `{0}`).  That removes far more than 850 B
> (every `S s = {…};` loses its ~S/2-word copy) AND fixes the far-data member-
> init truncation.  Keep the `%_clit` path for genuine compound-literal VALUES
> (`(T){…}` used as an rvalue).  Extend `local_zeroinit_probe.c` with a non-zero
> `S s = {a, b, …};` case (medium+compact+large) to guard the far-correctness.
> Then: `make check`, `tools/test-dos.sh` (expect 164+), full rebuild
> `bash tools/build-micropython.sh --model=compact --keep-going`, and
> `tools/run-victor-sasi.sh build/mp-link/mpython.exe 220` — expect the trace to
> reach `D3 D4` and print `3`.
>
> MICROPYTHON TREE STATE (separate repo, ~/projects/micropython, uncommitted):
> `py/compile.c` was reverted CLEAN (the K0..K3/Z markers are gone).
> `ports/dos8086/main.c` gc_collect was restored to the real stack-scanning
> collector (it had been stubbed empty for size headroom during the bisection).
> The C/D phase markers in main.c's do_str remain.  Reproduce the bisection if
> needed by re-adding markers; the recipe is `tools/recompile-mp-tu.sh compile
> ~/projects/micropython/py/compile.c` then `tools/run-victor-sasi.sh`.  CAUTION:
> markers cost image bytes against the razor-thin ceiling — temporarily stub
> gc_collect (empty) to make room, as this session did.
>
> ALSO STILL OPEN (unchanged from §2h): Finding 3 — a raised exception object's
> far pointer loses its SEGMENT on the raise path; needed for any real exception
> to print, but `print(1+2)` raises none, so it does not block the milestone.

> **§2i (DONE 2026-06-01, committed `96553e4`) — the wide-arg→narrow-param
> blocker is FIXED; `mp_parse` now runs to completion on the real Victor.**
> DOS gate 158→**161**, `make check` green, 111 s/r 0 r/r (C-action only).
>
> FIX (the §2h Finding 2 "GENERAL/correct" option a, all in `minic/minic.y`):
> minic recorded only a function's RETURN type, so `emit_arg` sized every
> call argument by the argument's OWN type — a wide `l` (4-byte) value handed
> to a narrow `w` (2-byte) parameter was pushed 4 bytes where the callee reads
> 2, shifting every later stack arg.  Now:
>  - New `fnproto[]` table (open addressing, keyed by name) records each
>    function's fixed parameter types + count.
>  - `fnproto_record()` wired into every prototype/definition site:
>    `ansi_proto_register`, `ansi_func_proto`, `emit_knr_func{,_typed}`, the
>    `EXTERN type IDENT(par1);` proto, and the `dcls`-level local proto.
>  - `coerce_arg()` narrows/widens an integer-scalar arg to the declared
>    param width (`=w copy` / `extuw` / Con-aware `sext`); pointer/float/
>    aggregate args untouched.  Fires in the DIRECT-call emit loop, skipping
>    true-vararg args (index ≥ nparam).  Also fixes the symmetric long→int
>    shift latent on medium.
> Probe `argwiden_probe.c` (+golden), gated medium+compact+large; bug-loud
> `r FAIL 99` / `base FAIL 0` (the exact mp_parse_num_base symptom) without
> the fix, all pass with it.  ALSO: `tools/run-dos-exe.sh` now runs DOSBox
> headless (`SDL_VIDEODRIVER=dummy` + `SDL_AUDIODRIVER=dummy`) so gate runs
> open no windows / steal no focus.
>
> VERIFIED ON THE REAL VICTOR (clean compact far-data build, body 824064 B —
> just under the ~824.9KB wall, links 106/106 + --gc-sections): the trace
> advanced from `D0 D1 DE` (ValueError mid-parse) to **`D0 D1 D2 DE`** —
> `mp_parse` now returns a valid parse tree (D2), and the failure has MOVED
> into `mp_compile` (DE appears before D3).
>
> **THE NEW blocker — an uncaught exception during `mp_compile`.**  Trace:
> `C1 C2 C3 C4 D0 D1 D2 DE C5` (main.c driver in ~/projects/micropython;
> D2=parse done, D3 would be compile done, DE=do_str's nlr else branch).  So
> something raises between D2 and D3.  PRIME SUSPECT: the §2i fix coerces only
> DIRECT named calls — minic's function-pointer TYPES encode only the return
> type (`FUNC(rettype)`), no param list, so a wide→narrow arg shift through a
> FN-PTR call is STILL latent, and the compiler is fn-ptr-dispatch-heavy
> (emit_t method tables: `emit->method(...)`).  NEXT: (1) instrument
> `py/compile.c` (checkpoints around the compile passes / scope walk) +
> recompile via `tools/recompile-mp-tu.sh compile ~/projects/micropython/py/
> compile.c`, run `tools/run-victor-sasi.sh build/mp-link/mpython.exe 220`, and
> bisect WHERE in compile it raises and WHAT (use 16-bit-only debug printers —
> [[i8086-kl-shift-clobbers-ax]]).  (2) If it's a fn-ptr-call wide→narrow
> shift, extending coercion needs param types carried in the function-POINTER
> type (a bigger minic change — fn-ptr types currently only hold the return
> type).  (3) Finding 3 (exception object far pointer loses its SEGMENT on the
> raise path: offset kept, segment garbage) is STILL open and needed for any
> real exception to print — instrument the raise path
> (`mp_raise`→`nlr_raise`→`nlr_jump`) to pin the 16-bit truncation.  Build:
> `bash tools/build-micropython.sh --model=compact --keep-going`.
>
> ---
>
> # (prior) §2h diagnosis — wide-arg→narrow-param was diagnosed here (NOW FIXED in §2i above)
>
> The §2g "token-2 INVALID" was an INSTRUMENTATION ARTIFACT (the lexer is provably correct); the REAL blocker is a minic far-data ABI bug: a wide (`l`, 4-byte) argument passed to a narrow (`size_t`/`w`, 2-byte) prototype parameter is NOT narrowed, shifting all later stack args. This makes `mp_parse_num_integer` raise `ValueError("invalid syntax for integer")` on the INTEGER token `1`, aborting `mp_parse`. FIX minic to narrow wide args to the prototype's param width (or type pointer-difference as `ptrdiff_t`/int). A SECOND independent bug: the raised exception object's far pointer loses its SEGMENT on the raise path (offset kept, segment garbage) → `mp_obj_print_exception` crashes.

> **§2h (DIAGNOSIS ONLY — 2026-05-31, NO qbe-repo code change, nothing committed).
> All work was on-target bisection on the real Victor (SASI) + static SSA/asm
> reading. The MicroPython tree (~/projects/micropython, separate repo) has an
> uncommitted lexer-only→full-pipeline debug driver in ports/dos8086/main.c; the
> §2-era instrumentation in py/{lexer,gc,parse,qstr,runtime}.c is STASHED
> (`git stash list`: stash@{0}=lexer, stash@{1}=gc/parse/qstr/runtime) — pop if
> needed, but the CLEAN tree is what proved the lexer correct.**
>
> **FINDING 1 — the §2g "token-2 reads MP_TOKEN_INVALID" was an INSTRUMENTATION
> ARTIFACT, not a real bug.** With clean (un-instrumented) py/parse.c + py/qstr.c,
> the lexer tokenises `print(1+2)` PERFECTLY. Verified two ways on the Victor:
> (a) a lexer-only driver (do_str just calls mp_lexer_to_next in a loop) emits the
> full correct stream `T07 T51 T08 T3c T08 T52 T04 T00` (NAME `(` 1 `+` 2 `)` NEWLINE END);
> (b) under real mp_parse the first three tokens are `T07 T51 T08` (correct). The
> §2g debug prints in parse.c/qstr.c perturbed register allocation and TRIGGERED a
> latent clobber that mis-set `tok_kind`. LESSON: heavy instrumentation can CREATE
> codegen bugs here — keep probes minimal and re-test clean. (Also exhaustively
> verified statically that `tok_enc`, its far-pointer relocation, `is_char`'s
> `ceql`, and `chr0` are all correct — the lexer codegen is sound.)
>
> **FINDING 2 (THE REAL BLOCKER) — minic passes a WIDE arg to a NARROW prototype
> param without narrowing → stack-arg shift → ValueError in integer parsing.**
> `mp_parse` consumes `print` `(` `1`, then `push_result_token` for the INTEGER
> calls `mp_parse_num_integer("1",1,0,lex)`, which calls
> `mp_parse_num_base((const char*)str, top - str, &base)`. ROOT CAUSE (airtight,
> from the generated asm):
>   - Caller SSA: `call $mp_parse_num_base(l %t72, l %t80, l %base)` — `len` (=
>     `top - str`, a FAR-pointer difference) is typed `l` (4 bytes) and pushed as 4 bytes.
>   - Callee asm reads `str` at `[bp+6/8]` (4B), **`len` at `[bp+10]` (2B, it's
>     `size_t`)**, `base` at `[bp+12/14]` (4B). minic's `size_t`/`ptrdiff_t` are
>     `int`=2 bytes (minic/include/stddef.h).
>   - So the 4-byte `len` push shifts EVERY later arg by 2 bytes: the callee reads
>     `&base` from `[bp+12]/[bp+14]` = caller's `len`-high-word(0) + `base`-low-word
>     → a GARBAGE `base` pointer. `mp_parse_num_base`'s (correct) `*base=10`
>     `storefw` writes to that wild address; the caller's real `base` (SS:[bp-226])
>     stays **0**. Then `mp_parse_num_integer`'s digit loop `if (dig >= base) break`
>     is `1 >= 0` → breaks on the first digit → 0 chars parsed → `value_error` →
>     `ValueError("invalid syntax for integer")`.
>   - VERIFIED on-target (probes in mp_parse_num_integer): `str`=valid heap far ptr,
>     `len`=1, `*str`='1' (all inputs CORRECT), but `base`=**0** after
>     mp_parse_num_base (`R0000`), 0 chars parsed (`S0000`). And the created
>     exception type resolved to `mp_type_ValueError`.
> THE FIX (minic): coerce each CALL argument to the callee's declared parameter
> width. minic currently records ONLY the return type (`FUNC(rettype)`); it has NO
> per-function parameter-type table, and `emit_arg`/`eval_arg` (minic.y ~1997-2021)
> size each arg by the ARGUMENT'S own type via `irtyp_ret`. Two fix options:
>   (a) GENERAL/correct: record per-function param-type lists at every
>       prototype/definition (varadd sites ~5358/5397/6264) and, at the 3 call-emit
>       sites (minic.y 2079-2101 fnptr, 2129-2148 named, 2589-2638 inline expr —
>       all share `emit_arg`), narrow/widen each arg to the param class (reuse the
>       `=w copy` truncation idiom at ~3124 and `sext`/`extuw` at ~1490). Also fixes
>       the symmetric `long`→`int`-param shift that is latent on MEDIUM too.
>   (b) TARGETED: type a pointer-pointer DIFFERENCE as `ptrdiff_t` (=`int`/`w`, per
>       minic's stddef.h) instead of `l` in `expr()`'s `-` handling — then `top-str`
>       is a 2-byte `w` matching the `size_t` param. Smaller but only fixes the
>       ptr-diff case (the general wide→narrow shift stays latent).
> EITHER fix MUST be verified: rebuild minic, `make check` (expect 111 s/r 0 r/r),
> `tools/test-dos.sh` (gate ~158), then full MicroPython rebuild + `run-victor-sasi`.
> Fixing this may let `print(1+2)` parse (then compile/exec are the next frontiers).
>
> **FINDING 3 (SECONDARY, independent bug) — a raised exception OBJECT's far
> pointer loses its SEGMENT on the raise path.** `exc` (= nlr.ret_val) reads as
> `<garbage-seg>:0x0180` — the OFFSET is constant/correct but the SEGMENT is stale
> stack garbage (varied c5f9/c5fc/c5fd across runs). `nlr_jump`'s `top->ret_val =
> val` store is a correct `storefl` (4 bytes) and `do_str`'s read is a correct
> `loadfl`, so the segment is dropped UPSTREAM in the raise path
> (`mp_raise…`→`nlr_raise(exc)`→`nlr_jump(MP_OBJ_TO_PTR(exc))`): a far
> `void*`/`mp_obj_t` truncated to 16 bits somewhere. This is WHY
> `mp_obj_print_exception` crashes (the reboot-after-DE) and why the exception type
> read as garbage. Independent of Finding 2; needed for ANY real exception to
> propagate. Next session: instrument the raise path (print exc seg:off at each
> hop from creation to nlr_jump) to pin the truncation, then fix in minic/qbe.
>
> **ALSO learned:**
>  - minic gap: `T a = x, b = y;` (multi-declarator WITH initializers) parse-errors
>    (CLAUDE.md §1j noted it). Hit it in debug code; split into one decl per line.
>    MicroPython core compiles 106/106 without it, so it's not a port blocker.
>  - IMAGE SIZE WALL (precise): a body of ~823.4KB LOADS on the Victor; ~824.9KB+ →
>    "Program too big to fit in memory". Razor-thin (~824KB). Each debug print
>    costs; keep instrumentation tiny or strip working-phase probes (stashed).
>  - The lexer-only `do_str` does NOT shrink the image (the runtime stays reachable
>    via inline-helper / mp_state_ctx fixups; --gc-sections can't prune the
>    connected component). And dropping `mp_init()` crashes the lexer preload.
>    main.c is left with the FULL-pipeline driver + a minimal "DE\n" handler (the
>    real mp_obj_print_exception crashes under far-data — Finding 3).
>  - `gc_collect` is NOT involved (never runs — heap not full; verified with a marker).
>
> **REPRODUCE:** `bash tools/build-micropython.sh --model=compact --keep-going`
> then `tools/run-victor-sasi.sh build/mp-link/mpython.exe 190`. Fast inner loop:
> `tools/recompile-mp-tu.sh <base> <src.c>` (recompiles one TU + relinks; needs
> /tmp/mp_objs.txt). The C/D phase markers in ports/dos8086/main.c are in place.
>
> ---
>
> # (prior) Next session — THREE far-data fixes (§2f/§2g) cleared the file_input loop + qstr-intern hang; parser now consumes "print" and reaches token 2.  NEW blocker = the lexer's SECOND token reads MP_TOKEN_INVALID (1) instead of `(` (0x51) — a latent far-data lexer gap on token 2.  ALSO: the fully-instrumented image is now ~846KB → "too big to fit in memory" on the Victor; strip debug markers (or shrink) to get a loadable image.

> **§2f/§2g (DONE 2026-05-31, committed `014ee64` + `bb61947`) — THREE
> far-data fixes, found by on-target bisection on the real Victor.  DOS gate
> 154→158, `make check` green, 111 s/r 0 r/r.  MicroPython's `print(1+2)`
> parse advanced from an INFINITE LOOP (single_input→file_input→file_input_2→
> file_input_3 forever) all the way through "print" qstr interning and the
> first token, to the SECOND token.**
>
> Each fix has a probe (where a synthetic probe was authorable) and is
> bug-loud-verified.  Decode helpers for the on-target traces: token enum
> (MINIMUM rom level, FSTRINGS+ASYNC OFF): END=0, NEWLINE=4, INDENT=5, DEDENT=6,
> NAME=7, INTEGER=8, DEL_PAREN_OPEN=0x51, OP_PLUS=0x3c; rule-id decode from the
> compiled enum in `build/mp-link/parse.pp.c` (single_input=56=0x38).
>
> FIX 1 — **unsigned char widened to int SIGN-extended** (`minic.y`,
> model-INDEPENDENT).  Every char→int widening emitted `extsb`; a `uint8_t`
> with bit 7 set (0x8D) sign-extended to 0xFF8D (the byte already sits
> zero-extended in a `w` temp — loadub/loadfb clear the high byte — so
> sign-extending the low byte corrupts it).  Fix: emit `extub` when
> ISUNSIGNED(src) at the int=char assignment site + the three prom()
> char-promotion sites.  Canonical victim: py/parse.c `get_rule_arg()` reads a
> `const uint8_t` offset table; a 0x8D offset → 0xFF8D indexed the rule-arg
> table wild → `rule(stmt)`/`rule(simple_stmt)` resolved to rule 0 → the
> file_input loop.  Probe `uchar_widen_probe.c` (medium+compact); bug-loud
> "idx FAIL -115 / sum FAIL -227".
>
> FIX 2 — **pointer MEMBER of a far struct stored NEAR** (`minic.y`).  The
> assignment far-store fired only for ISFAR && KIND!=PTR/FUN, or a DIRECT
> global (FARSTORAGE).  A pointer member of a far struct reaches the store as a
> computed Tmp address (FARSTORAGE false) with a PTR value type (the ISFAR
> clause excludes PTR), so minic emitted a NEAR store (to the DGROUP shadow)
> while the member READ correctly used loadfar of the real far segment — value
> written never reached where the reader looked.  Fix: a storage-far
> side-channel `lval_storage_far`, set by `lval()` (member-of-far-struct /
> far-pointer-deref / far-global-variable) and OR'd into the store's far
> trigger — distinguishing "lives in far storage" from the value-FAR bit
> (which on a PTR means the VALUE is a far pointer in NEAR storage, e.g. a
> local far-pointer variable, which must stay a near store).  Additive: only
> adds far stores where storage is genuinely far.  Canonical victim:
> qstr_init's `MP_STATE_VM(last_pool) = &CONST_POOL` (mp_state_ctx is a far BSS
> struct, last_pool a pointer member) wrote near, qstr_find_strn read far → the
> qstr pool loop saw NULL.  Probe `farstruct_ptr_probe.c` (compact+large,
> far-static); bug-loud "p/next/q/wr FAIL".
>
> FIX 3 — **far-pointer DATA relocation lost its segment** (`tools/asm_to_omf.py`).
> A relocatable pointer initializer in static data was emitted by qbe as
> `.long _sym` → nasm `dd _sym` → OMF loc-9 32-bit OFFSET fixup, segment word
> left 0.  Under far-data a data pointer is a 4-byte seg:off far pointer, so the
> missing segment = wrong-segment deref.  Fix: under far-data, split a
> relocatable `.long _sym[+N]` into `dw _sym+N` (loc-1 offset) + `dw seg _sym`
> (loc-2 segment + runtime reloc); `omf_link` already resolves both.
> Canonical victim: the static qstr pools — each pool's `prev` (and
> `qstrs[]`/`lengths[]`) is a compile-time `&far_static`, so the pool->prev
> chain walked into GARBAGE pools (observed bogus lengths 0x652, 0x9a58, never
> terminating); after the fix the chain reads the real pools (lengths 31 then
> 183) and terminates at NULL.  NO synthetic gate probe — authoring one is
> blocked by ORTHOGONAL minic limits on far arrays-of-pointers (static-array
> element deref + far-pointer equality, both separate gaps); validated by the
> on-target qstr chain + the `.omf.asm` emitting `dw _sym / dw seg _sym`.
> CAVEAT: fix 3 ADDS a relocation per far-pointer data item (5158 relocs now),
> growing the MZ header ~3KB → re-tightens the Victor size wall.
>
> **THE NEW blocker — the lexer's SECOND token is MP_TOKEN_INVALID (1).**  On
> the v9 build (qstr patched) the on-target token trace is exactly `T07`
> (preload NAME "print") then, after the parser matches the NAME atom and calls
> mp_lexer_to_next, `T01` (= MP_TOKEN_INVALID) — it should be `T51`
> (DEL_PAREN_OPEN) for the `(`.  So the SECOND mp_lexer_to_next mis-tokenises
> `(` as INVALID.  The first token has been correct since §2e, so this is a
> latent far-data lexer gap on token N>1, only now reachable (the parser never
> got past token 1 before).  NEXT: instrument `py/lexer.c::mp_lexer_to_next`
> (next_char chr0/chr1/chr2 cache, the token-classification `tok_enc` table walk
> — those tables are far static data; also is_char/is_char_or etc.) to see what
> the lexer reads for `(` and where it decides INVALID.  Use 16-bit-only debug
> printers (no Kl shift — [[i8086-kl-shift-clobbers-ax]]).
>
> **FIRST, get a loadable image.**  The full rebuild + the debug instrumentation
> now in the micropython tree (qstr.c/lexer.c/parse.c/runtime.c/gc.c K*/F*/T*/
> rXX markers — UNCOMMITTED, separate repo) totals ~846KB and DOS reports
> "Program too big to fit in memory" on the Victor.  Strip the noisy markers
> (keep a minimal targeted set for the token-2 probe), and/or revisit the
> shrink levers (dead-strip is on; MICROPY_CONFIG trim, smaller link subset —
> see the §2b/§1z blocks).  Build: `bash tools/build-micropython.sh
> --model=compact --keep-going`; run: `tools/run-victor-sasi.sh
> build/mp-link/mpython.exe 240`.  The C/D/E/Q phase markers in main.c +
> lexer.c + runtime.c + gc.c are still in place.
>
> ---
>
> # (DONE §2e) mp_parse HANG CLEARED (two far-data codegen fixes); blocker was parser ends with non-END token (NOW understood: the file_input loop, fixed in §2f)

> **§2e (DONE 2026-05-31, committed `29e225a`) — the mp_parse HANG is FIXED.
> Two far-data codegen bugs, both with probes, DOS gate 150→154, `make check`
> green, 111 s/r 0 r/r (C-action edits only), amd64/arm64/rv64 byte-identical.**
>
> Found by on-target bisection on the real Victor via a NEW harness
> `tools/run-victor-sasi.sh` (the SASI hard-disk sibling of run-victor-mame.sh —
> boots `victor_python.img` partition 0, NO floppy, `-scsi:0 harddisk -hard1`;
> needed because mpython.exe is >612KB and a Victor floppy can't hold it).
>
> BUG 1 — **bitfield WRITE through a far address** (`minic.y` case `'='` on a
> `.` member).  The bitfield read-modify-write computed a far storage-unit
> address (`ptyp=IDIR_FAR`, `base_far` true under any far-data model) but then
> emitted a NEAR `load`/`store%c` — a near store of a far Kl address uses only
> the offset against DS, so the write hit the wrong segment and the bitfield
> value never reached its real (far) home.  The bitfield READ path already used
> `loadfar`; only the WRITE was wrong.  Now both use `loadfar`/`storefar` when
> `base_far`.  Canonical victim: py/parse.c's `rule_stack_t { size_t rule_id:8;
> … }` in the GC heap — `push_rule` wrote `rule_id` to the wrong segment,
> `pop_rule` always read 0, and `mp_parse` spun forever (rule stack never
> emptied → the observed hang).  Probe `bitfield_far_probe.c` (compact+large);
> bug-loud specifically on the HEAP-pointer case (a stack local's far segment
> happens to be DS-reachable so it masks the bug — the GC-heap case is the true
> exposure, matching the on-target hang).
>
> BUG 2 — **return-value type coercion missing** (`minic.y` `Ret`).  `return
> expr;` emitted `ret <x>` WITHOUT coercing `x` to the function's declared
> return type.  A narrow (INT/CHR) value returned from an `l` function reached
> `ret %tN` as a `w` temp; selret never widened it to DX:AX, so the function
> returned stale AX:DX.  py/lexer.c `next_char` does `mp_uint_t chr2 =
> lex->reader.readbyte(lex->reader.data)` where `mp_uint_t`==`uintptr_t` (32-bit
> under far-data) and `mp_reader_mem_readbyte` does `return *cur;` (byte→Kl):
> the byte was read CORRECTLY (cur/end/0x70 verified on-target inside readbyte)
> but the caller got ~`0x00000001`, which the lexer mapped to
> `MP_LEXER_INVALID_BYTE` (`'\1'`) → every source byte mis-tokenised → first
> token became 0x2f instead of NAME.  `Ret` now runs the assignment converter
> (LNG widen via `sext` / LNG narrow / float) against `curfntyp`.  Probe
> `fnptr_klret_probe.c` (compact+large); bug-loud (`c0 FAIL 3a80001`).  NOTE:
> the earlier passing `ret_byte` masked this because its EXPLICIT `(unsigned
> long)` cast already produced the widening; only the IMPLICIT return coercion
> was missing.  (While debugging, also re-confirmed the latent
> [[i8086-kl-shift-clobbers-ax]] — a 32-bit `>>=4` loop in a debug helper hung;
> avoid Kl shifts in on-target debug printers, use 16-bit word aliasing.)
>
> VERIFIED on the real Victor (clean build, instrumentation removed): the lexer
> now produces correct tokens and **`mp_parse` completes its rule loop in 11
> iterations with NO hang** (was: spun forever).
>
> **THE NEW blocker — parser ends with a NON-END token → `syntax_error`.**  On
> the clean build the trace reaches `D1` (enter mp_parse), the loop runs 11
> iters and exits normally, but at the end-of-parse check `lex->tok_kind` is
> `0x07` (NOT `MP_TOKEN_END`=0), so `lex->tok_kind != MP_TOKEN_END` is true and
> the `syntax_error:` path is taken (never reaches `D2`).  For `print(1+2)`
> SINGLE_INPUT, 11 rule iterations looks LOW — the parser likely stopped early
> (matched a shorter production / consumed too few tokens), leaving a token
> unconsumed.  So this is almost certainly ANOTHER far-data codegen gap, in
> either (a) the incremental `mp_lexer_to_next` calls DURING parse (a later
> token mis-read — the FIRST token is now correct, but token N may not be), or
> (b) the parser's token-match / push_result / result-stack logic.  NEXT:
> re-add the per-rule token trace (loop-top `pdbg_hex(rule_id)` + `t<tok_kind>`)
> AND a `mp_lexer_to_next`-site trace, recompile `parse` (+`lexer`) via
> `tools/recompile-mp-tu.sh`, run via `tools/run-victor-sasi.sh
> build/mp-link/mpython.exe 240`, and see WHICH token first goes wrong / where
> the parse stops short.  Use 16-bit-only debug printers (no Kl shift).  First
> determine the exact token enum values for THIS config (FSTRINGS/ASYNC may be
> on — check MICROPY_CONFIG_ROM_LEVEL) so 0x07/0x2f decode correctly.  Build:
> `bash tools/build-micropython.sh --model=compact --keep-going`.  The
> C/D/E/Q/A phase markers in main.c + lexer.c + runtime.c + gc.c (the
> ~/projects/micropython tree, NOT the qbe repo) are still in place.
>
> ---

# (DONE §2d) Next session — struct pass-BY-VALUE ABI landed; MicroPython lexer crash CLEARED on the real Victor; NEW blocker = a HANG in mp_parse (after the lexer, before parse completes)

> **§2d (DONE 2026-05-31) — minic now passes STRUCTS BY VALUE as arguments; the
> deterministic lexer reboot on the real Victor is FIXED.  MicroPython now runs
> through mp_init AND the whole lexer, into `mp_parse`.**  DOS gate **148→150**,
> `make check` green, 111 s/r 0 r/r (no grammar change — C-action edits only),
> amd64/arm64/rv64 byte-identical.
>
> ROOT CAUSE (found by serial-checkpoint bisection on-target, then reading the
> generated SSA): minic had struct *return*-by-value (§1d) but NOT struct
> *pass*-by-value.  A by-value aggregate ARGUMENT was emitted as a SINGLE scalar
> word (`%t20 =w loadw %reader; call ...(w %t20)`), and the callee declared a
> one-word param + stored only that word into its N-byte local — so every member
> past the first was uninitialised.  The canonical victim: the lexer's
> `mp_lexer_new(qstr, mp_reader_t)` takes a 12-byte `mp_reader_t` (far fn-ptr
> `readbyte` at offset 4 + far `data` + `close`) by value; only the first word
> arrived, the callee read a garbage far fn-ptr, and `next_char`'s first indirect
> `call far` jumped to nowhere → a hard reboot ~4 allocs into compiling
> `print(1+2)`.  (The "24KB heap exhaust" framing in the §2c note below was WRONG
> — there was an earlier deterministic crash; `gc_collect` never even ran because
> `gc_init` sets the alloc threshold to `(size_t)-1`, so collection only fires on
> heap-full, which `print(1+2)` never reached.)
>
> FIX — by-value aggregate crosses the call boundary as a POINTER to its storage
> (mirrors the §1d sret design; type-driven on both ends so it agrees across
> separate compilation).  All in `minic/minic.y`:
> - new helpers `is_aggr`, `eval_arg` (caller yields the aggregate's ADDRESS, not
>   a truncated load — re-derives via `lval` for V/@/. lvalues; C/I/L expr()
>   already yield a slot address), `emit_arg` (emits a struct arg as a
>   `DATAPTR_T()` pointer), `bind_param` (callee: alloc the local + `emit_struct_copy`
>   the pointed-to struct in; far loadfw/storefw under far-data).
> - wired into all 3 call-emission paths (direct, fn-ptr-var, indirect `(*fp)()`),
>   the ANSI function-def param signature + binding, and both K&R emit paths.
> Probe `structarg_probe.c` (+golden): struct-with-far-fn-ptr-member passed by
> value, callee calls through the member; C copy semantics (callee mutation
> doesn't touch caller); struct arg between scalars; pass-through; indirect call
> with a struct arg.  Gated **compact + large** (the far-data MicroPython target).
> VERIFIED on the real Victor: serial trace now shows `C1 C2 C3 Q0..Q3 C4 D0
> E0..E5 D1` (mp_init, then `mp_lexer_new` fully through `mp_lexer_to_next`,
> returning into `do_str`) with NO reboot — the lexer is sound.
>
> **MEDIUM is intentionally OMITTED from the probe** — it trips a SEPARATE,
> pre-existing backend bug: the callee's by-value copy is near `storew`s, which
> QBE load.c forwards + reconstructs into the member `loadl` (4-byte far code
> ptr) via Kl `shl/and/or`; on i8086 that reconstruction CLOBBERS a live value
> rega parked in AX (the call's `data` arg), returning garbage.  That is the
> [[i8086-kl-shift-clobbers-ax]] / [[qbe-loadc-wordsize-i8086]] family (Kl ops
> not declaring their AX/DX clobber to rega), independent of this ABI and not hit
> under far-data (which uses opaque `loadfw`/`storefw`, no slice reconstruction).
> Fixing it (mirror §2c's `Target.divclob`: add a Kl-arith clobber to rega's
> avoid mask for the reconstruction ops) would let the probe cover medium too.
>
> ALSO this session (in the ~/projects/micropython tree, a SEPARATE repo — NOT
> committed with the qbe change): `ports/dos8086/main.c` `gc_collect` is now a
> real conservative C-stack scan (`[sp, stack_top)`, scanned at BOTH even
> alignments since 8086 slots are 2-byte but a far ptr is 4 bytes; no register
> spill needed because the Kl-slot-resident invariant keeps every live 4-byte
> mp_obj_t in a stack slot; mp_state roots are already traced by
> `gc_collect_start`).  NOT yet exercised (the run hasn't reached heap-full).
> The heap STAYS 24KB: bumping it to 56KB made the image 863KB and DOS reported
> "Program too big to fit in memory" — the Victor leaves essentially no headroom
> over the ~831KB that loads, so a working gc_collect (not a bigger heap) is the
> reclaim path.  The gc.c per-alloc/`S:` serial checkpoints are now SILENCED
> (`GCK` no-op); the C*/D*/E*/Q* phase markers remain.
>
> **THE NEW blocker — a HANG in `mp_parse`.**  On-target the trace stops at `D1`
> (lexer done, entering `mp_parse`) and never reaches `D2` (parse done) in ~150s
> of 1.5×-speed emulation, with NO reboot (so not a crash/OOM — a hang or
> infinite loop).  `print(1+2)` parses trivially, so this is almost certainly
> another far-data codegen gap (likely another struct-by-value or far-ptr shape
> the parser hits — `mp_parse` threads `parser_t`/`mp_parse_node_t` structs).
> NEXT: add checkpoints inside `py/parse.c::mp_parse` (and `push_result_*` /
> `pop_result`), recompile via `tools/recompile-mp-tu.sh parse
> ~/projects/micropython/py/parse.c`, and bisect on the Victor (SASI recipe
> below).  Reproduce: full build `bash tools/build-micropython.sh --model=compact
> --keep-going`, inject onto `victor_python.img:0:\PROG.EXE`, run via MAME SASI
> (see the §2b block).  The C/D/E/Q checkpoints in main.c + runtime.c + lexer.c
> are still in place.

# (prior) Next session — MicroPython runs through mp_init() + into compiling print(1+2) on the real Victor; NEW blocker = 24KB heap exhausts during compile (gc_collect is a no-op stub)

> **§2c (DONE 2026-05-31, committed `d70c280`) — the gc_alloc hang is FIXED.  It
> was a 16-bit div/mul AX:DX register-clobber codegen bug, NOT a far-data pointer
> bug.  Verified ON THE REAL VICTOR: mpython now runs through `mp_init()` (C4) and
> deep into compiling `print(1+2)`, with gc_alloc succeeding repeatedly.**
>
> ROOT CAUSE: 8086 idiv/imul/div are fixed-register (dividend in AX, DX:AX the
> implicit pair, DX clobbered).  The i8086 backend emits them in-place instead of
> precoloring TMP(RAX)/TMP(RDX) in isel (amd64 does), so rega kept a value live
> ACROSS such an op in AX/DX and the next div/mul destroyed it.
> `gc_setup_area` computes `gc_alloc_table_byte_len = (24576-1)/(1+8/2*(4*4)) =
> 24575/65 = 378`, but the outer dividend (in AX) was zeroed by the inner
> idiv(8/2)/imul → came out **0** → 0 heap blocks → first gc_alloc finds nothing,
> returns NULL, caller retries forever → hang at `mp_init`'s first
> `mp_obj_dict_init`.  Localized with serial checkpoints (S: layout dump in
> gc_setup_area showed `total=0x6000 table=0x0000 pool=0x0000`; after the fix
> `table=0x17a=378 pool=0x5e8=1512`).  See [[feedback_i8086_div_divisor_axdx_clobber]].
>
> FIXES (DOS gate **147→148**, `make check` green, amd64/arm64/rv64 byte-identical):
> - **spill.c** + new `Target.divclob` field (i8086 = `BIT(RAX)|BIT(RDX)`, 0
>   elsewhere): for div/mul/rem, OR divclob into the live-across avoid mask,
>   mirroring caller-save-across-call, so rega keeps cross-living temps out of AX/DX.
> - **i8086/emit.c**: Kw idiv/div backstop — stage a divisor that still lands in
>   AX/DX into BX before the DX:AX setup (xchg-ax-bx subcase when dividend in BX).
> - **libstub.asm**: `__builtin_clzl` (32-bit CLZ), now referenced once uintptr_t
>   widened to 32-bit.
> - **build-micropython.sh / recompile-mp-tu.sh**: pass `-DFAR_DATA=1` to the C
>   preprocessor for far-data models.  `stdint.h` gates intptr_t/uintptr_t width on
>   `FAR_DATA`; without it `mp_uint_t`/`mp_obj_t` tagging was 16-bit and truncated
>   far pointers' segments (a real bug, but NOT the hang — the hang persisted after
>   this until the div fix).
> - **divreg_probe.c** (+golden, medium gate): nested div/mul in div operands;
>   bug-loud (`t1=0`) without the fix, `t1=378 t2=4607 t3=3 t4=-500 t5=0` with it.
>
> **THE NEW blocker — the 24KB heap fills during compilation of `print(1+2)` and
> `gc_collect` is still a NO-OP STUB (main.c), so nothing is reclaimed → a gc_alloc
> eventually returns NULL → the program crashes/reboots (a second `__V9BEGIN__`
> appears in the serial capture).**  The on-target capture shows: `C4` (mp_init
> done), then dozens of successful `A0 A1 A2 Af Am Ar` gc_alloc cycles as the
> lexer/parser/compiler allocate, then an `A0 A1 A2 A2` (gc_collect retry → still
> NULL), then a reboot — never reaching `C5`/`3`/`__V9END__`.  **NEXT MOVES (pick):**
> (1) **bump `MICROPY_HEAP_SIZE`** in ports/dos8086/mpconfigport.h (it's 24KB; the
> far-data model leaves lots of conventional RAM — try 64–128KB) as the cheap first
> test; (2) **implement a real `gc_collect`** (stack scan; now that setjmp/longjmp
> work it can spill callee-saved regs and scan [SP, stack_top) — see
> [[minic-setjmp-longjmp]]); (3) confirm whether the reboot is pure OOM or a
> separate crash (add an OOM checkpoint in m_malloc_fail / gc_alloc's NULL return).
> The serial checkpoints (C*/Q*/S:/A* in main.c, runtime.c, gc.c — in the
> ~/projects/micropython tree, NOT the qbe repo) are STILL IN PLACE; strip the
> noisy gc.c A*/S: ones once the heap path is solid (they print per-alloc).
> SASI harness recipe unchanged (below).  Reproduce: `bash
> tools/build-micropython.sh --model=compact --keep-going` then inject + run via
> the SASI disk (see the §2b block).
>
> ---
>
> # (DONE §2b) — `--gc-sections` dead-strip + first on-target execution of MicroPython
>
> **What landed (committed `70e9c8a`): `omf_link.py --gc-sections`** — segment-
> granular dead-code elimination via FIXUPP reachability from `_start` (standard
> linker --gc-sections model).  BFS the fixup graph (each fixup's TARGET + FRAME —
> segment / group-members / external-symbol→defining-segment — marks its segment
> live) to a fixpoint; drop segments nothing reachable points at.  Sound here
> because every cross-segment dependency (call, data-table fn-ptr, `seg sym`
> selector) is an OMF fixup; the only hand-asm (crt0/libstub) uses nasm fixups too.
> Opt-in flag, default OFF → gate byte-identical (DOS pipeline **147/147**, `make
> check` green, a no-flag MicroPython relink is byte-for-byte identical to the
> prior mpython.exe).  `build-micropython.sh` passes it.  **Result: 208 segments
> stripped, CODE 861KB→675KB, image 971KB→781KB (footprint 928.7KB→745.9KB) — now
> UNDER the ~896KB Victor ceiling.**  Correctness is structurally proven: the link
> succeeds, and `_apply_fixups`/`_resolve_target` hard-index `seg_map`, so any
> kept→stripped reference would KeyError-crash the link.
>
> **THE SASI HARNESS WORKFLOW (reusable — this is how to run a >612KB .EXE on the
> Victor; the floppy can't hold it and `vtg_image_util` caps single-file floppy
> writes ~440KB).**  Use the dedicated bootable SASI hard disk
> `~/projects/qbe/victor_python.img` (Victor 9000 Hard Disk, FAT12, partition 0 =
> boot drive A:/C:, ~9.5MB free; the user fixed a vtg bug so partition 0 is now
> WRITABLE).  Boot params: `-scsi:0 harddisk -hard1 <img>` and **NO `-flop1`** (with
> a floppy present the Victor always prefers floppy boot).  Inject onto partition 0
> with `vtg_image_util copy <exe> <img>:0:\\PROG.EXE` + an `AUTOEXEC.BAT` (`echo off`
> / `portset a 9600 none 1 8` / `ctty seriala` / `echo __V9BEGIN__` / `prog` / `echo
> __V9END__`).  Capture serial via `-rs232a null_modem -bitbanger <cap>`, ~200-240s
> (boot ~50s).  **Verified end-to-end: boot → AUTOEXEC → serial → mpython LOADS and
> runs on real hardware.**  (TODO: fold a SASI path into `tools/run-victor-mame.sh`
> + a `VICTOR_TESTS` entry once `print(1+2)` actually works.)
>
> **THE next blocker — `gc_alloc` hangs (mp_init's FIRST heap allocation).**  With
> serial checkpoints added to `main.c` (C1..C5) and `py/runtime.c::mp_init` (Q0..Q3),
> the on-target capture is exactly: `__V9BEGIN__ C1 C2 C3 Q0 Q1 Q2` — then NOTHING.
> So: `main()` entered (C1), stack set (C2), `gc_init()` returned (C3), `qstr_init()`
> returned (Q1), reached just before the first `mp_obj_dict_init` (Q2) — and
> **`mp_obj_dict_init(&mp_loaded_modules_dict, …)` never returns (no Q3)**.  That
> call is `mp_obj_dict_init` → `m_malloc` → **`gc_alloc` (py/gc.c)**.  Hypothesis:
> a far-data pointer-arith/comparison bug in gc.c's block/ATB scan (heap is a far
> `static char heap[24576]` in its own FAR_DATA segment; `gc_init(heap, heap+sizeof)`
> does far-ptr arithmetic; gc_alloc scans the heap+alloc-table with far pointers) —
> likely an infinite loop in the free-block finder, OR `gc_init` set the pool/ATB
> pointers wrong so the scan never terminates.  **NEXT: add checkpoints inside
> `gc_alloc`/`gc_init` (py/gc.c), recompile that one TU via
> `tools/recompile-mp-tu.sh gc ~/projects/micropython/py/gc.c` (the in-place
> per-TU recompile+relink helper committed this session — compact far-data,
> --gc-sections; needs `/tmp/mp_objs.txt` = the link object list, regenerated by a
> full build-micropython run or the glob in this note), and bisect.**  The
> checkpoints in main.c + runtime.c are STILL IN PLACE for this.  This is a real
> QBE-toolchain far-data gap to FIND AND FIX (the project principle), not a
> dead-strip regression (closure proof above).
>
> Build/run recipe used this session (reproduce):
> `bash tools/build-micropython.sh --model=compact --keep-going` (full), or
> `tools/recompile-mp-tu.sh <base> <src.c>` (one TU, fast), then the SASI harness
> above.  NOTE: the checkpoint helper reads `/tmp/mp_objs.txt` (the ordered link
> object list: crt0_exe.obj + every built TU .obj + libstub_exe.obj); regenerate
> it with the same source glob `build-micropython.sh` uses if it's been cleared.

---

# (DONE §2b) Original §2a-followup — SHRINK mpython.exe under the ~896KB Victor ceiling, then run print(1+2) on-target via the new MAME harness

> **§2a (DONE 2026-05-30) — the MAME Victor 9000 headless harness is BUILT,
> VALIDATED, and GATED.**  DOSBox emulates a 640KB IBM PC (wrong machine); the
> on-target / >640KB path now runs under MAME machine `victor9k` (~896KB RAM).
> New, all committed:
> - **`tools/run-victor-mame.sh`** — the `victor9k` analog of `run-dos-exe.sh`.
>   Takes a built `.EXE` + optional `seconds_to_run` (default 90); `cp`s the
>   base `python.img` to a scratch `run.img`; injects the EXE as `PROG.EXE` +
>   a generated `AUTOEXEC.BAT` via `vtg_image_util copy`; runs MAME headless
>   (`SDL_VIDEODRIVER=dummy mame victor9k -ramsize 896K -flop1 run.img -video
>   none -sound none -nothrottle -skip_gameinfo -seconds_to_run N -rs232a
>   null_modem -bitbanger <cap>`, in a `-homepath` sandbox so it never touches
>   the user's `~/.mame`); streams the serial capture (CR/0x1A-stripped,
>   trimmed between `__V9BEGIN__`/`__V9END__` sentinels) to stdout.  **Exit 77
>   (skip-not-fail)** if MAME / its roms / `$VICTOR_DISK` / `vtg_image_util` is
>   missing.  Env overrides: `$VICTOR_DISK` (default `~/Desktop/randos/
>   python.img`), `$MAME`, `$MAME_ROMS`, `$VTG_IMAGE_UTIL`, `$VICTOR_RUN_SECS`.
> - **`tools/test-victor.sh`** — a SEPARATE gate (each MAME run boots MS-DOS
>   3.1 from floppy, ~45-60s wall-clock, so it's kept off the fast DOSBox
>   gate).  `VICTOR_TESTS` array (`<src>:<golden>:<model>`), same run/skip/diff
>   shape as `test-dos.sh`.  Currently 1 entry: `cprobe` (the harness smoke
>   test).  **Passes 2/2 (build + cprobe).**
> - **`minic/dos/tests/cprobe.golden.txt`** — golden for the smoke test.
>
> **The TWO gotchas found and fixed (so you don't re-hit them):**
> 1. **`flop1` not `hard1`** — the base is a bootable FLOPPY; `-flop1 run.img`
>    (myfreedos's `-hard1` was for a hard-disk image).
> 2. **`echo off`, NOT `@echo off`** — MS-DOS 3.1 predates the `@` line prefix,
>    so `@echo off` is parsed as an unknown `@echo` command and leaves echo ON,
>    which leaks the `A:\>PROG.EXE` prompt+command into the serial capture.
>    Bare `echo off` gives clean program-only output.
>
> Confirmed working: `tools/run-victor-mame.sh build/examples/cprobe/cprobe.exe`
> → exactly `x=42 (want 42)\nx=99 (want 99)`, stable across repeated runs.  The
> serial-capture path (`ctty seriala` → MAME `-bitbanger`) is proven end-to-end
> independent of mpython's size.  No minic/qbe/backend change this session —
> just three new files — so the DOSBox gate + `make check` are unaffected.
>
> **THE NEXT MOVE — shrink `mpython.exe` (footprint 928.7KB, ~33KB over the
> ~896KB Victor ceiling) so it loads, then run `print(1+2)` → `3` on the Victor
> via this harness** (add it as a `VICTOR_TESTS` entry).  Shrink levers, rough
> payoff order: (1) **dead-strip unreferenced functions/segments in
> `omf_link`** (reachability from `_start`/`_main` through PUBDEF/EXTDEF/FIXUPP;
> now that big TUs split per-function-group the granularity is finer — biggest
> lever, `print(1+2)` touches maybe 10-20% of the linked core); (2) **trim
> `MICROPY_CONFIG`** (fewer builtins/modules, smaller qstr set); (3) **curate a
> smaller link subset**.  See the §1y/§1z block below + [[project-victor9000-target]],
> [[project-minic-far-setjmp-and-size-wall]].

---

# (DONE §2a) Original spec — BUILD A MAME VICTOR 9000 HEADLESS TEST HARNESS (kept for reference)

> **WHY THIS WAS THE NEXT SESSION (user direction 2026-05-30):** the real target
> is the **Victor 9000 / Sirius 1** (~896KB RAM, non-IBM memory map), NOT the
> 640KB IBM PC that `tools/run-dos-exe.sh` drives under DOSBox.  DOSBox was only
> ever a convenient stand-in; it emulates the WRONG machine, so it can neither
> load a >640KB image nor exercise Victor-specific hardware.  Before we chase
> the mpython.exe shrink (or anything else "does it run on target"), we need a
> **MAME-based `victor9k` harness** that runs a built program headlessly and
> captures its output for golden-diff assertions — the same way
> `~/projects/myfreedos` and `~/projects/newlibc` already test on this platform.
> Build the harness FIRST, validate it with a TINY program, then point the gate
> (or a new victor-gate) at it.  See [[project-victor9000-target]].
>
> **THE PROVEN PATTERN (reverse-engineered from myfreedos + newlibc — reuse it,
> don't reinvent):**
> - **MAME**: binary at `~/projects/mame/mame`, machine `victor9k`, `-ramsize 896K`.
> - **Headless flags**: `-video none -sound none -nothrottle -skip_gameinfo
>   -seconds_to_run <N>` with `SDL_VIDEODRIVER=dummy` in the env.  (`-rompath
>   ~/projects/mame/roms` if roms aren't on the default path.)
> - **Output capture — TWO mechanisms, pick the serial one for line-oriented
>   stdout:**
>   1. **Serial → host file** (simplest; this is what `myfreedos/boot/victor/
>      test_mame.sh` uses): `-rs232a null_modem -bitbanger /tmp/cap.txt`.  The
>      `AUTOEXEC.BAT` does (EXACT sequence, user-confirmed) `portset a 9600 none
>      1 8` then `ctty seriala`, redirecting DOS CON (handle 1 = stdout) to
>      serial port A.  **9600 is the CEILING — MAME's serial timing breaks above
>      it** (the image's CONFIG.SYS defaults porta to 1200; `portset` bumps it to
>      9600 at runtime).  Our qbe programs already write stdout via INT 21h
>      AH=40h to handle 1, so `ctty seriala` routes that to `/tmp/cap.txt` with
>      no program change.  `portset` syntax:
>      `PORTSET <A|B> <baud> <parity> <stopbits> <bits>`.
>   2. **Screen-RAM dump** (alternative; `newlibc/phase3_newlib/run_test.sh`
>      `--auto`): a MAME `-autoboot_script` Lua dumps screen RAM at `0xF0000`
>      (4000 B = 80×25×2), decoded by `phase3_newlib/tools/decode_victor_screen.py`
>      (char glyph = low byte − 0x60).  Heavier; only needed if we test the
>      Victor text screen directly rather than stdout.
> - **Boot/program disk (USE THE STABLE MS-DOS 3.1 FLOPPY, not myfreedos):** the
>   base is `~/Desktop/randos/python.img` — a bootable **Victor MS-DOS 3.1
>   single-sided floppy** (MSDOS.SYS + COMMAND.COM + the PORTA/PORTB/PORTSET
>   serial utils; CONFIG.SYS already `device=porta.exe`/`portb.exe`).  Chosen
>   over the myfreedos FreeDOS image because myfreedos is itself under test — MS
>   DOS 3.1 is the stable reference OS.  Mount it in MAME as a **floppy**:
>   `-flop1 <img>` (NOT `-hard1`).
> - **File injection — `vtg_image_util` (on PATH; CONFIRMED working):** it
>   reads/writes Victor FAT12 floppies.  `vtg_image_util copy <localfile>
>   <img>:\\NAME.EXT` writes (OVERWRITES an existing name; there is no `-f` for
>   copy — that flag is `create`-only).  `copy <img>:\\NAME .` reads back;
>   `list`/`info`/`delete` as expected.  **ALWAYS operate on a COPY of
>   python.img — never mutate the user's master.**  So the harness: `cp
>   python.img run.img`; `vtg_image_util copy <prog>.exe run.img:\\PROG.EXE`;
>   write an `AUTOEXEC.BAT` (`@echo off` / `portset a 9600 none 1 8` /
>   `ctty seriala` / `PROG.EXE` / a sentinel echo) and `vtg_image_util copy
>   AUTOEXEC.BAT run.img:\\AUTOEXEC.BAT`.
> - **Exit detection**: fixed `-seconds_to_run` (myfreedos uses 150; tune down),
>   or a `PASS:`/`FAIL:` sentinel regex over the captured output.
> - **Interactive debugging (NOT for the gate, but invaluable when a run
>   misbehaves)**: the **`mame-victor-test` skill** + MCP server at
>   `~/projects/Victor9000-Development-Private/mame/mame-mcp-server/` exposes
>   `mame_read_screen_text`, `mame_read_memory`, `mame_get_registers`,
>   breakpoints, single-step, etc.  myfreedos's CLAUDE.md marks it MANDATORY for
>   ad-hoc MAME work ("DO NOT run MAME directly" — for interactive sessions).
>
> **CONCRETE DELIVERABLES for the session:**
> 1. **`tools/run-victor-mame.sh`** — the `victor9k` analog of `run-dos-exe.sh`:
>    takes a built `.EXE`, `cp`s the base `python.img` to a scratch `run.img`,
>    injects the EXE + a generated `AUTOEXEC.BAT` (`@echo off` /
>    `portset a 9600 none 1 8` / `ctty seriala` / `PROG.EXE` / a sentinel echo
>    so we know it finished) via `vtg_image_util copy`, runs MAME headless
>    (`SDL_VIDEODRIVER=dummy mame victor9k -ramsize 896K -flop1 run.img
>    -video none -sound none -nothrottle -skip_gameinfo -seconds_to_run <N>
>    -rs232a null_modem -bitbanger <cap>`), then streams the serial capture
>    (CRLF/0x1A-stripped, à la `run-dos-exe.sh`) to stdout, trimmed to between
>    the sentinel markers.  Exit 77 (skip) if `mame`, its roms, or
>    `$VICTOR_DISK` are missing — so the gate degrades gracefully on machines
>    without the Victor MAME setup.  Base image path overridable by env
>    `$VICTOR_DISK` (default `~/Desktop/randos/python.img`), mirroring `$DOSBOX`.
> 2. **File injection uses `vtg_image_util` directly** — no custom FAT writer
>    needed (it already does Victor FAT12 read/write).  Just `cp` the master to
>    scratch and `vtg_image_util copy` the EXE + AUTOEXEC.BAT in.  The one open
>    question to settle empirically on the first MAME run: confirm the serial
>    capture actually fills (porta = MAME `-rs232a` port A; baud held at 9600).
> 3. **VALIDATE THE HARNESS WITH A TINY PROGRAM FIRST** — e.g. the existing
>    far-data "halprobe" that prints `3`, or a 1-line hello `.EXE`.  This proves
>    the serial-capture path end-to-end INDEPENDENT of mpython's size, and
>    becomes the harness's own smoke test / golden.  (mpython.exe is 928.7KB —
>    ~33KB over 896KB — so it still needs the shrink before IT runs; do that
>    AFTER the harness exists, in a later session.)
> 4. **Gate wiring** — add a `victor` runtime path to `tools/test-dos.sh` (a new
>    RUNTIME-style array gated on `$VICTOR_DISK`/`mame` being present), or a
>    sibling `tools/test-victor.sh`.  Keep the DOSBox path for the small
>    near/far probes it already validates (it's faster and needs no Victor
>    image); use MAME for the on-target / >640KB / Victor-hardware cases.
>
> **RESOURCES (paths):** MAME `~/projects/mame/mame` (machine `victor9k`); **base
> boot floppy `~/Desktop/randos/python.img` (Victor MS-DOS 3.1)**; **file-inject
> tool `vtg_image_util` (on PATH)** — `copy`/`list`/`info`/`delete` Victor FAT12;
> harness exemplars `~/projects/myfreedos/boot/victor/test_mame.sh` (the serial
> `-bitbanger` pattern) + `~/projects/newlibc/phase3_newlib/{run_test.sh,
> tools/decode_victor_screen.py}` + `~/projects/newlibc/MAME_DEBUG_GUIDE.md`;
> MCP/skill
> `~/projects/Victor9000-Development-Private/mame/mame-mcp-server/` (`mame-victor-test`);
> Victor HW docs `~/Documents/Victor9k Stuff/Manuals/{subsystem-docs,GPTFiles}`;
> full Victor FreeDOS port `~/projects/myfreedos`; OEM **MS-DOS 3.1 sources**
> `~/projects/myfreedos/Victor Vintage Software/MS-DOS 3.1 Sources`.  There is a
> `victor9000-engineer` agent for Victor hardware/MS-DOS-internals questions.
>
> **STILL DEFERRED (user):** `~/projects/newlibc` (the real Victor-targeted libc)
> integrates at a LATER stage — keep the libstub path for now.  The mpython
> shrink (omf_link dead-strip / MICROPY_CONFIG trim) is the move AFTER the
> harness exists.  See [[project-victor9000-target]],
> [[project-minic-far-setjmp-and-size-wall]].

# (DONE §1y/§1z) MicroPython LINKS under compact far-data; image size is the wall (now measured vs the ~896KB Victor ceiling, not 640KB)

> **§1y (commit `76c69eb`) — FAR_SETJMP_EXE: far-data setjmp/longjmp.**  New
> `_far_setjmp`/`_far_longjmp` in `tools/libstub_to_exe.py` (4-byte far env
> ptr via ES:BX; longjmp's `val` at `[bp+10]`).  Resume-SP arithmetic is
> IDENTICAL to the medium SETJMP_EXE (`lea [bp+6]`) — args sit above the
> 4-byte CS:IP return address regardless of width.  Appended only under
> far-data models.  `minic.y`: `setjmp`/`longjmp` added to `far_stdlib[]` →
> mangled to `_far_setjmp`/`_far_longjmp` under compact/large/huge.
> `setjmp_probe` now gated medium + compact + large (full NLR round-trip,
> byte-identical golden across all three).  Gate 145→**147**, `make check`
> green, 111 s/r 0 r/r (no grammar change).
>
> **§1z — MicroPython LINKS under `--model=compact` far-data, and the
> remaining blocker is IMAGE SIZE, not the toolchain.**  `tools/build-
> micropython.sh --model=compact` now: compiles 106/106 TUs (0 fail) with
> `--far-static-data` + `-DFAR_DATA`/`-DDOS_FAR_DATA`, then LINKS to
> `build/mp-link/mpython.exe` (108 modules; 861KB far code; 43KB far data
> OUTSIDE DGROUP; only 37KB in DGROUP — the §1r DGROUP-overflow hang is GONE).
> Two real gaps fixed to get there:
> 1. **>64KB CODE segment** (`compile.obj` was 78KB — far-data codegen ~2x's
>    code size, pushing MicroPython's biggest TU past the 64KB real-mode
>    segment cap).  nasm emitted a `SEGDEF2` (32-bit, 4-byte length) +
>    `LEDATA32`; `omf_link._handle_segdef` always read a 2-byte length →
>    misparse → "bad LNAMES index 0".  FIX (two parts): (a) `asm_to_omf.py`
>    now SPLITS a TU's `.text` across multiple `<BASE>_TEXT`/`_TEXT1`/`_TEXT2`
>    CODE segments at FUNCTION boundaries (qbe emits a `.text` directive
>    before every function) when the estimated size exceeds `TEXT_SEG_BUDGET`
>    (56KB; `est_line_bytes`≈4×, ~2x margin over the measured ~2.1 B/line).
>    Far calls resolve cross-segment via symbol fixups (already how
>    cross-module calls work) and each function stays wholly in one segment so
>    intra-function near jumps remain segment-local.  `omf_link` places every
>    CODE-class segment distinctly (`_place_distinct`), so N per module just
>    works.  (b) `omf_link._handle_segdef` now reads the 4-byte length for
>    `SEGDEF2` and HARD-REJECTS any USE16 segment >64KB with a clear message
>    (defensive: a real-mode segment can't exceed 64KB).  Single-segment TUs
>    are byte-identical; no probe is big enough to split.
> 2. **`mphalport.c` console HAL was medium-only** (near-data ABI: `str` at
>    `[bp+6]` near, `len` at `[bp+8]`, INT 21h via DS:DX).  Under far-data
>    `str` is a 4-byte far ptr (`[bp+6]`/`[bp+8]`), `len` at `[bp+10]`, and the
>    buffer is OUTSIDE DGROUP so DS must be set to `str.seg`.  Made it
>    `#if DOS_FAR_DATA` (build-micropython passes `-DDOS_FAR_DATA=1` under far
>    models).  **VERIFIED CORRECT** by a standalone far-data probe that prints
>    "3" — so the console path works; the HAL is NOT the blocker.
>
> **THE remaining blocker — IMAGE SIZE (but only modestly over).**  TARGET IS
> THE **VICTOR 9000 / Sirius 1**, which has up to **~896KB** of contiguous
> conventional RAM (NOT the IBM-PC 640KB — its non-IBM memory map allows more).
> `mpython.exe`'s loaded footprint is **928.7KB** (body 951024 B, minalloc 0)
> → only **~33KB over the 896KB raw ceiling** (more once DOS + PSP + heap/stack
> headroom is counted, but the same order of magnitude — NOT the wild overage a
> 640KB ceiling would imply).  This is NOT a codegen/link defect — we link the
> WHOLE curated core (omf_link has no dead-code elimination) while `print(1+2)`
> touches a small fraction.  **Next-move options (rough payoff order):**
> 1. **Dead-strip unreferenced functions/segments in `omf_link`** — mark from
>    `_start`/`_main` through PUBDEF/EXTDEF/FIXUPP reachability, drop unreached
>    CODE segments.  Now that big TUs split per-~function-group the granularity
>    is finer; biggest lever (print(1+2) needs maybe 10-20% of the core, so this
>    likely shaves FAR more than the ~33-100KB needed).
> 2. **Shrink `MICROPY_CONFIG`** — fewer builtins/modules, smaller qstr set,
>    trim the compiler.  Even a small trim likely closes a 33KB gap.
> 3. **Curate a smaller link subset** — only modules transitively needed for
>    lexer→parse→compile→`mp_call_function_0`+print.
>
> **TEST-ENVIRONMENT CAVEAT:** the `tools/run-dos-exe.sh` / DOSBox path emulates
> a 640KB IBM PC, so a >640KB image won't load THERE regardless of the Victor
> ceiling.  Validating `print(1+2)` on the real target needs a Victor 9000
> emulator/hardware path (there is a `victor9000-engineer` agent + a Victor
> codebase in `~/projects/newlibc`).  Small far-data probes (setjmp_probe,
> halprobe printing "3") DO run under DOSBox and prove the toolchain end-to-end.
>
> **PLANNED LIBC (deferred, user direction):** `~/projects/newlibc` is a real
> Victor-9000-targeted C library; the plan is to integrate it at a LATER stage
> (it replaces the current libstub.asm / minic/include stopgaps).  Do NOT wire
> it in yet — the current libstub path is the bring-up vehicle.
> See [[project-minic-far-setjmp-and-size-wall]], [[minic-far-data-segment]].

# (DONE §1y/§1z) Prior next-session note — toolchain gaps: long const/struct + huge ptrdiff FIXED (§1w/§1x); MicroPython far-data compiles clean (§1v); next = FAR_SETJMP_EXE then link far

> **PRINCIPLE (reaffirmed): the goal is to FIND AND FIX QBE-toolchain gaps;
> running MicroPython is the vehicle, not the prize.  When a probe trips a real
> codegen defect, FIX THE GAP — don't scope the probe around it.**
>
> **§1w (commit `0eec5f4`) — three model-INDEPENDENT `long` truncation gaps**
> (i8086 `int` is 16-bit; bit under medium too):
> 1. `minic.y sext()` sign-extended a COMPILE-TIME CONSTANT via `=l extsw`,
>    which on i8086 keeps only the low 16 bits → `long x = 555666L` became
>    31250 (and bit-15-set 40000 went negative).  Fix: retype the Con LNG
>    directly (its full value is already known), no extsw.
> 2. Integer literals were always typed INT and the L/l suffix discarded, so a
>    `long` literal > 16 bits passed to an `l` parameter or a `%ld` vararg went
>    out as a 16-bit word.  Fix: new `Node.nlong` (lexer sets it on L/l suffix
>    or value > 0xFFFF), `'N'` case types it LNG.
> 3. `load.c def()` reconstructing a 4-byte slice (a `loadl` from two 2-byte
>    `storew`s — struct-copy of a returned `long` member) used class Kw because
>    the width test hardcoded `sl.sz > 4`; `high << 16` then shifted a 16-bit
>    temp to 0 and lost the high word.  Fix: `sl.sz > T.wordsz`
>    (**target-general**; no-op on amd64/arm64/rv64 where wordsz==4).
> Probe `longconst_probe.c`.  Closes [[qbe-loadc-wordsize-i8086]] residual,
> the struct-return-long limit, and [[minic-long-literal-as-int-vararg]].
>
> **§1x (commit `ef870bd`) — huge ptr-MINUS-ptr.**  Two normalised far pointers
> into one object can sit in different segments, so a flat 32-bit `sub` of their
> seg:off words gave (Δseg<<16)+Δoff not the true Δseg*16+Δoff
> (`&a[20]-&a[3]` ≠ 17).  Routed MHuge ptr-ptr through the existing-but-dead
> `_qbe_huge_cmp` helper (returns signed linear(p)-linear(q)); the element-size
> `div` post-step still scales it, so int*/long* diffs are correct too.  Flat
> sub stays for compact/large/near; huge comparison stays flat (normalisation
> makes seg:off monotonic).  `farlocal_probe` now covers huge as well.
> FOOTGUN hit: `int*/long*` in a minic.y action-body comment closes the block
> comment (`*/`).  See [[long-and-huge-ptrdiff-gaps]].
>
> `make check` green throughout; compact MicroPython sweep stayed 106/106; DOS
> gate 142→**145**.

# Next session — far-data MicroPython core COMPILES clean (compact+large); next = FAR_SETJMP_EXE then link under far placement (post §1v)

> **§1v — the "27 far-data TU compile fails" are CLEARED (commit `75bf7d0`).**
> The curated MicroPython core now compiles **106/106 TUs** minic→qbe(-t i8086)
> under `-m compact` AND `-m large`, 0 fail (was ok=79 / minicfail=2 /
> qbefail=25).  Sweep harness: `bash build/mp-far-probe/sweep.sh compact`
> (and `large`) — re-run to reproduce.  `make check` green, DOS gate **140→142**,
> 111 s/r 0 r/r (no grammar change — fix #3 only edits action bodies).
>
> Three independent root causes, three fixes (all far-data; NEAR_DATA models
> byte-identical for #2/#3; #1 model-independent):
> 1. **i8086/isel.c** — the fast-alloc slot loop scanned only `fn->start`, so a
>    constant-size `alloc4` in a NON-entry block (a block-scoped local declared
>    inside a loop/if body — py/bc.c's `mp_bytecode_get_source_line` lineinfo
>    buffer is the canonical case) reached emit as `Oalloc4 cls Kl` and died
>    ("unsupported 32-bit op 81 (cls Kl)").  Now slots constant allocs in EVERY
>    block (C block-scoped locals reuse one frame slot; real `alloca` is routed
>    to the GC heap by `MICROPY_NO_ALLOCA`, so no dynamic alloc survives — the
>    simple fixed-slot fix beats amd64's salloc/Osalloc dynamic path and dodges
>    far-pointer-to-SS:sp).  Cleared 23 TUs.
> 2. **minic.y** — member-base address of a LOCAL aggregate under far-data
>    emitted `=w add %localKl, off`, truncating the Kl slot address
>    (`ALLOC_T()` is 'l'); the following far `loadfX` then read the wrong place,
>    and the const-fold case tripped gvn `assoccon`'s `KWIDE` assert
>    (parse.c, compile.c).  `base_far` now includes `|| !NEAR_DATA()` at all
>    three member-address sites (expr read, bitfield store, lval addr) — under
>    far-data every object address is a far Kl pointer.
> 3. **minic.y** — the `type '*'` declarator rule did `IDIR_FAR($1 & ~FAR)`,
>    stripping the pointee's FAR bit, so `char **` was built as far-ptr-to-NEAR-
>    char*.  `*pp` then came out near, making `q - *pp` a near-vs-far
>    "non-homogeneous pointers in subtraction" error (bc.c, objint.c) and a
>    silent miscompile elsewhere.  Fix: keep `$1`'s FAR (`IDIR_FAR($1)` —
>    IDIR_FAR shifts it to the inner-far position, bit 27).  3 sites
>    (`*`, `* CONST`, `* VOLATILE`).
>
> New probe `farlocal_probe.c` (+golden), wired compact+large in `test-dos.sh`.
> Huge is omitted ONLY for the pointer-MINUS-pointer (`q - *pp`) case — that
> needs seg*16+off linearization the backend doesn't do (a separate pre-existing
> huge gap); the alloc/member/struct-return cases all pass under huge too.
>
> **THE next moves (unchanged goal — get MicroPython data out of DGROUP):**
> 1. **FAR_SETJMP_EXE** — a far-data setjmp/longjmp variant (4-byte env ptr +
>    ES), gated by far_data_model() the way FAR_STDIO_EXE is.  The medium
>    SETJMP_EXE (§1r, in `tools/libstub_to_exe.py`) reads a 2-byte near env ptr;
>    under compact/large the env arg is a 4-byte far ptr.  Mirror FAR_STDIO_EXE's
>    ES handling; extend `setjmp_probe.c` to compact/large.
> 2. **Link MicroPython under far placement.**  Parametrize
>    `tools/build-micropython.sh` to take `--model=compact` (or large) and set
>    `QBE_FAR_STATIC_DATA=1` (so each module's statics go to its own FAR_DATA
>    segment outside DGROUP, freeing DGROUP for heap+stack — the §1r runtime-hang
>    fix).  The compile step is now clean (this session); expect to surface
>    link-layer gaps (far-data far_stdlib mangling already exists) and then a
>    runtime attempt at `print(1+2)` → `3`.  `gc_collect` is still a no-scan STUB.
> See [[minic-far-data-segment]], [[minic-setjmp-longjmp]].

# (DONE §1v compile) Next session — far-data DONE for opt-in; either flip placement to default or build MicroPython under far placement (post §1u)

> **§1u — FARSTORAGE landed (commit `cfde49b`): direct far-GLOBAL access
> (load/store/member/struct-copy/++/pointer-global) now works under far
> placement.  Gate 137→140 green, `make check` green, 111 s/r 0 r/r.**
>
> New `FARSTORAGE(s)` predicate (true for a Glo/Ext symbol under a far-data
> model — a STORAGE-location property, distinct from ISFAR's value-type bit;
> NO PTR/FUN exclusion since a global pointer's 4-byte value also lives far).
> Threaded through `load()` (delegates to loadfar), the assignment +
> prefix/postfix inc-dec STORE conditions (`|| FARSTORAGE`), the three
> member-address sites (Kl `=l add` + FAR propagation when base_far), and
> `emit_struct_copy` (far per-word path when either side is a direct global).
> KEY finding while verifying: the i8086 backend's IMPLICIT far-lowering of a
> near `storew/loadw $sym` already covered SIMPLE scalar global access (so most
> cases "worked"), but FAILED `emit_struct_copy` and some RMW — FARSTORAGE makes
> minic emit the explicit reliable storefX/loadfX so it's correct everywhere.
> Bug 1 (§1t far-store AX/DX bracket) is a prerequisite (the storefw-to-CAddr
> path it enables).  Probe `farglobal_probe.c` (compact/large/huge, built with
> `QBE_FAR_STATIC_DATA=1` so globals sit at offset 0 of their own FAR_DATA
> segment); verified bug-loud without FARSTORAGE ("ptcopy FAIL 5764",
> "g_i_rw FAIL 23").  NEAR_DATA models byte-identical (predicate false there).
>
> **Placement is still OPT-IN** (`QBE_FAR_STATIC_DATA=1` / `--far-static-data`).
> With FARSTORAGE done, far placement + far globals now work TOGETHER, so the
> two honest next moves are:
> 1. **Build the MicroPython subset under far placement (compact/large)** — the
>    actual goal: `tools/build-micropython.sh` with `QBE_FAR_STATIC_DATA=1` and
>    `-m compact`/`-m large`, freeing DGROUP for heap+stack.  Needs a far-data
>    setjmp/longjmp variant (`FAR_SETJMP_EXE`, 4-byte env ptr + ES — mirror
>    FAR_STDIO_EXE gating), and likely surfaces the 27 far-data TU compile
>    failures noted at §1s (`gvn.c:210` KWIDE assert + minic "non-homogeneous
>    pointers in subtraction").
> 2. **Flip placement to DEFAULT under far-data** (drop the `--far-static-data`
>    gate).  ONLY blocker now is neutralizing caddr_cmp_probe's k_lo cases (the
>    segmented-semantics non-bug from §1t — `&g-1` offset-wrap; keep that probe's
>    symbol in DGROUP or drop the k_lo asserts when far-placed).  Then re-run the
>    whole far-data gate with placement on for all probes.
> See [[minic-far-data-segment]].

# (DONE §1t/§1u) Next session — finish far-data: re-apply FARSTORAGE (far-GLOBAL direct access), then decide default vs opt-in (post §1t)

> **§1t — backend bug 1 FIXED & committed (`2e76a99`); "bug 2" DEMYSTIFIED as a
> segmented-pointer semantic limit, NOT a codegen defect.  FARSTORAGE NOT yet
> re-applied (deferred by user choice — "stop here for now").**
>
> **Bug 1 (DONE): far-store AX/DX save bracket.**  `Ostorefb/Ostorefh/Ostorefw`
> saved ES/BX/CX but not AX/DX; when the dest address is an RCon CAddr (far store
> to a constant global address, e.g. `arr[CONST]=v`), `load_farptr_con`'s
> `mov ax, seg sym` clobbered an AX-resident live temp (return value).  Fix:
> `kl_save_axdx`/`kl_restore_axdx` bracket, mirroring `Oloadf*`/`Ostorefl`.  Probe
> `caddr_store_probe.c` (compact/large/huge) — verified bug-loud ("ret_w FAIL 908"
> = `seg arr` leaked into AX).  Gate 134→137, `make check` green.
>
> **Bug 2 (NOT A BUG — do not try to "fix" cmp32).**  Reproduced via
> `QBE_FAR_STATIC_DATA=1 caddr_cmp_probe` (g_long at off=0 of its far segment):
> ltu_sym/leu_sym/gtu_lo/geu_lo FAIL.  ROOT CAUSE: QBE folds `&g_long - 1` into a
> CAddr `$g_long + (-1)`; on i8086 far that −1 addend WRAPS the 16-bit offset
> (0→0xFFFF) WITHOUT borrowing into the segment word, so k_lo materializes as
> `S:0xFFFF` (asm literally `mov ax, _g_long+-1`), not flat `(S-1):0xFFFF`.  cmp32
> then faithfully compares the wrapped representation — it is CORRECT.  The
> probe's k_lo assertions assume FLAT 32-bit pointer arithmetic, which segmented
> far pointers don't honor (`&g-1` is UB; far ptr ±n is offset-only in
> compact/large; there is no single seg:off that is "the byte before a paragraph
> base" without normalization).  So the cmp32 path needs NO change.
>
> **What this means for the default-flip:** the original step (c) "fix bug 2"
> dissolves.  To make far-static-data the DEFAULT you must instead NEUTRALIZE
> caddr_cmp_probe's k_lo cases under far-segment-offset-0 placement (keep that
> probe's symbol in DGROUP, or drop the k_lo cases when far-placed) — they test
> ill-defined cross-segment-boundary far-pointer ordering, not codegen.
>
> **REMAINING WORK — re-apply FARSTORAGE (the real far-global-access codegen).**
> This is the actual prize and is needed for MicroPython data regardless of the
> default decision.  Open question (asked, user chose to defer): wire it as an
> OPT-IN far-globals mode (gated behind a minic flag mirroring the
> `--far-static-data` opt-in; default gate stays byte-identical) vs UNCONDITIONAL
> under far-data + flip placement to default.  Opt-in is lower-risk and still
> unblocks MicroPython (it needs far placement + far globals together anyway).
> Reconstruction recipe is below (the prototype was correct in direction).
> Verify with the all-on experiment: with bug 1 fixed, the ONLY all-on failures
> should be caddr_cmp's k_lo cases (the segmented-semantics non-bug above).
> See [[minic-far-data-segment]].

# (superseded) Next session — finish far-data: make direct global access far so far-static-data can be the default (post §1s)

> **§1s — additional far data segment(s): the placement INFRASTRUCTURE is in
> and proven, landed OPT-IN.  The remaining work is the minic/qbe far-GLOBAL
> direct-access codegen so it can become the default and unblock MicroPython.**
>
> **What landed (opt-in, gate green at 132→… with `fardata_probe`):**
> Under a far-data model (compact/large/huge), passing
> `asm_to_omf.py --far-static-data` routes a module's statics into its OWN far
> segment `<BASE>_DATA`/`<BASE>_BSS` (class FAR_DATA/FAR_BSS) placed by
> `omf_link.py` DISTINCTLY, OUTSIDE DGROUP.  Each segment has its own `seg sym`
> selector (the same mechanism `_HUGE_<sym>` arrays already use), so **total
> static data can exceed the single 64 KB DGROUP** — DGROUP is left holding only
> the hand-asm crt0/libstub near data + the stack.  No 64 KB bin-packing needed:
> each module gets its own segment.  Proven by `fardata_probe.c` (medium-…er,
> compact/large/huge): **48 KB of statics in a far segment, read back correctly**
> (`big[0]`/`big[6000]`/`big[11999]`, a strided sum, an initialized `seed[]`/`tag[]`)
> — a link that overflows 64 KB under the old all-in-DGROUP scheme.
> `build-example.sh` opts in via env `QBE_FAR_STATIC_DATA=1`; `test-dos.sh`
> sets it for `fardata_probe` only (basename-gated).  Default OFF, so every
> existing compact/large/huge probe is byte-identical (DGROUP, near) and the
> gate stays green.  KEY ENABLER discovered: qbe ALREADY addresses every global
> far under far-data (`mov ax, seg _sym; mov es,ax; es:[bx]` — never assumes
> DGROUP), and the linker already resolves `seg sym` for non-DGROUP segments —
> so ACCESS needs no codegen change, only PLACEMENT.  See [[minic-far-data-segment]].
>
> **Why it's OPT-IN, not default — the far-GLOBAL-access gap (THE next task).**
> Turning placement on for ALL probes surfaced that minic emits **near**
> load/store for DIRECT global access (`g`, `the_thing.v`, `g = x`, `g++`) under
> far-data — it only ever worked because globals lived in DGROUP (=DS).  Array
> subscript (`arr[i]`) already goes far (Kl pointer arith), which is why
> `fardata_probe` passes without any minic change.  I prototyped the fix — a
> `FARSTORAGE(s) = (!NEAR_DATA() && (s.t==Glo||s.t==Ext))` predicate threaded
> through `load()`, the store sites, and member-access (clean storage-vs-value
> separation, handles scalar AND pointer globals, no type pollution) — and it
> took the all-on gate from **12 → 6** failures (fixed extern_struct, tentative_def,
> const_init, phase_bprime, storefl).  **REVERTED** it because it exposed TWO
> more latent backend bugs it doesn't itself fix, and shipping a half-far
> global model would be worse than opt-in:
> 1. **`storefw`/`store*` to a CAddr (`$g_sink`) destination corrupts a live
>    slot** — surfaced in `farretprobe` `two_live_a` (the `g_sink` write made the
>    `p`-return path return garbage).  A far store whose DEST is a global symbol
>    address (not a register far pointer) mis-targets / clobbers.  Likely an
>    i8086 `Ostoref{b,h,w}` RCon-CAddr-dest register-save gap (cf. caddr_arith).
> 2. **Segment-boundary unsigned compare vs a CAddr** — `caddr_cmp_probe`
>    `ltu_sym`/`leu_sym`: `(k-1) < &g_long` where `&g_long` now has off=0 in its
>    own far segment, so `k-1` borrows into the segment word; the cmp32 CAddr
>    unsigned high-word path gives the wrong order.  Only reachable once a global
>    sits at offset 0 of a far segment (which far placement makes common).
>
> **Plan to make far-static-data the DEFAULT (and unblock MicroPython data):**
> (a) re-apply `FARSTORAGE` in minic (the prototype was correct in direction;
> reconstruct from this note / git reflog), (b) fix bug 1 in `i8086/emit.c`
> (`Ostoref*` with RCon CAddr dest — push/pop the scratch regs, mirror the load
> path), (c) fix bug 2 (cmp32 CAddr unsigned high-word ordering at a segment
> boundary), (d) then flip placement on by default under far-data models and
> drop the `--far-static-data` gate.  Each bug wants its own probe.  THEN
> MicroPython under `large` needs its 27 far-data TU compile failures fixed
> (separate: `gvn.c:210` KWIDE assertion + minic "non-homogeneous pointers in
> substraction") before it links far.  See [[minic-far-data-segment]].

# (prior) Next session — MicroPython port: MicroPython LINKS but HANGS — DGROUP is too small; move static data to far segments (post §1r)

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
