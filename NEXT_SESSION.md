# Next session (§4q — find WHY the X00 mark phase fails to mark the qstr-string chunk.  §4p NAILED THE MECHANISM: churn(120) NameError = `mp_load_global(MP_QSTR_churn)` misses because the "churn" qstr STRING was FREED-WHILE-LIVE during the churn(100) collection and reused — its recomputed hash then lands the lookup on an empty slot.  NOT a wild write (§4o's framing was wrong); it's a freed-while-live GC marking failure, layout-sensitive per §4o's period-4.  The exact failing lookup is fully understood; the open question is the marking link that breaks.)

## 2026-06-09 §4p notes (CRACKED the failure mechanism: freed-while-live of the "churn" qstr string → lookup miss; instrumentation via SERIAL, MAME debugger ruled out)

**§4p turned §4o's "layout-sensitive wild write" into a precise, fully-traced mechanism** using
SERIAL instrumentation in the EXTERNAL micropython tree (all guarded by `-DMP_DBG_GLOBALS=1`;
normal builds unaffected).  The MAME-debugger watchpoint path was investigated and RULED OUT
(see below).  No qbe/minic source changed this session — pure diagnosis.

### THE MECHANISM (airtight, multi-step, all serial-verified on real Victor)
1. **The failure is a name LOOKUP miss, not a structural corruption.**  Instrumented
   `py/runtime.c::mp_load_global`'s terse-NameError path to print the failing qstr + globals
   map state.  Result on a FAIL run: `NFq=00DB` → qstr **219 = `churn`** (a MODULE global, not a
   builtin — that's why it's not found in builtins either → NameError).
2. **The globals TABLE is fully intact at the miss.**  Dumped every slot: `s03 k=000006DA
   v=<fnptr>` — slot 3 holds exactly `MP_OBJ_NEW_QSTR(219)=(219<<3)|2=0x6DA` and churn's
   function pointer.  `a=4 u=3`, table ptr unchanged.  So nothing overwrote the dict.
3. **The lookup misses purely on the HASH.**  Globals is a hash map; `pos = qstr_hash(219) %
   alloc`, then linear-probe until found or an EMPTY slot.  Slot 1 is empty; churn is at slot 3.
   So the lookup finds churn IFF `qstr_hash(219)%4 ∈ {2,3}` (reaches slot 3 before empty slot 1)
   and MISSES iff `∈ {0,1}`.  churn(20..100) all worked, churn(120) misses → **`qstr_hash(219)`
   CHANGED**.
4. **`qstr_hash` RECOMPUTES from the string** (`MICROPY_QSTR_BYTES_IN_HASH=0`, confirmed:
   `qstr.ssa` `qstr_hash` calls `qstr_compute_hash(pool->qstrs[q], pool->lengths[q])`).  Dumped
   the string: `ql=0005` (correct len 5) but `qs=0402BBB91000` — **the "churn" string bytes are
   GARBAGE**: a reused-object header (a far type-pointer `0xB9BB:0x0204` + a length word `0x0010`).
   So the "churn" string's heap memory was REALLOCATED to a new object.
5. **=> The "churn" qstr string (packed in `qstr_last_chunk`, py/qstr.c) was FREED-WHILE-LIVE
   during the churn(100) "X00" collection, then reused by churn(120)'s allocations**, overwriting
   it → `qstr_hash` recomputes garbage → `pos∈{0,1}` → lookup hits empty slot 1 → NameError.
   This is the §4f/§4o "freed-while-live qstr_pool" family — now CONFIRMED with the smoking-gun
   reused-header bytes.  §4o's "wild write" framing was WRONG; it's a marking failure.
6. **The string, gc_pool_start, gc_pool_end all share ONE segment** (`qp=BE0F1F50 gp=BE0F1500
   ge=BE0FD200`), so `VERIFY_PTR`'s range check (`ptr>=gp && ptr<ge`) is correct for the string
   itself — the freed-live is from the **MARK phase failing to mark the qstr-string chunk**, not
   a VERIFY_PTR rejection of the string.  Layout-sensitive exactly per §4o (FAIL iff the *pool
   start's* normalized segment ≡ {0,3} mod 4; period 64 bytes = one ATB byte = 4 blocks).

### THE OPEN QUESTION FOR §4q — which marking LINK breaks?
The "churn" string is reachable for marking via `last_pool` (root) → pool chunk → `qstrs[]` →
the string-data chunk (`qstr_last_chunk`).  Strings are PACKED into a shared chunk (qstr.c
`qstr_from_strn_helper`: `m_new` a chunk, append each string at `qstr_last_used`), so `qstrs[idx]`
point INTO the chunk at non-block-aligned offsets; the chunk survives all-or-nothing via the ONE
block-aligned pointer to its HEAD (the first string at `chunk_base`).  Statically every link
"should" mark fine (all same-segment, `BLOCK_FROM_PTR` flat-sub and `PTR_FROM_BLOCK` flat-add are
self-consistent with no 16-bit carry on this 49 KB heap — verified in `gc.ssa`).  So the break is
a RUNTIME/alignment effect not visible statically.  §4q must instrument the MARK phase directly:
- In `py/gc.c` `gc_sweep_free_blocks`, print the address of every block freed during the X00
  collection; confirm the `qstr_last_chunk` block (whose addr = the healthy `qstr_str(219)` rounded
  to its chunk HEAD) is among them → proves swept-while-live and gives the chunk's block #.
- Then in `gc_collect_start`/`gc_mark_subtree`, trace whether that chunk block ever gets
  `ATB_HEAD_TO_MARK`'d, and if the pool-chunk scan produces the chunk-base pointer and what block
  `BLOCK_FROM_PTR(chunk_base)` computes.  The period-64 = ATB-byte hint points at a block↔ATB
  index mismatch or a far-ptr representation divergence on ONE link of the chain.
- **CAUTION**: the `dbg_churn_atb` ATB-kind probe added to the port `gc_collect` this session is
  UNRELIABLE — it printed `k=0` (FREE) for the churn block in PASS runs too (impossible for a live
  block), so its far-byte ATB read (`mp_state_ctx.mem.area.gc_alloc_table_start[blk/4]`) is itself
  miscompiled/misreading.  FIX or replace that probe before trusting it; don't read the §4p `CB`
  lines as truth.  (It may even hint at a SECOND minic far-array-index bug — worth a 2-line probe.)

### MAME headless watchpoint — RULED OUT in this MAME (0.287); needs a source patch
The §4o "approach 1" (debugscript `wpset`) does NOT work: MAME's `process_source_file()` only runs
commands **while the CPU is stopped** (debugcpu.cpp), but `-debugger none`'s `wait_for_debugger`
immediately `go()`s (and `DEBUG_FLAG_OSD_ENABLED` is always set, machine.cpp:95), so the
debugscript is opened but NEVER executed → `wpset`/`trace` produce nothing (matches §4o's empty
output).  gdbstub only supports the i486 reg map (`debuggdbstub.cpp`), NOT the Victor's 8088.
A watchpoint's ACTION *does* run in `debug_watchpoint::triggered()` regardless of frontend, and
`trace`/`tracelog` write to a file — so the ONLY missing piece is getting the initial `wpset` to
run.  **Fix if pursued**: patch `~/projects/mame/src/osd/modules/debugger/none.cpp`
`wait_for_debugger` to call `m_machine->debugger().console().process_source_file()` before `go()`,
then rebuild MAME (slow full relink).  New harness `tools/run-victor-wp.sh` (committed) already
drives `-debug -debugger none -debugscript` + captures the trace file; it's ready once none.cpp is
patched.  Lower priority than the serial gc.c instrumentation above.

### Reproduction cheat-sheet (verified this session)
- Shipping clean FAIL image restored: `build/mp-link/mpython.exe` heap_seg **0xBA8B** body 817840.
- The FAIL alignment is **NOT predictable from the .map** (it depends on the *runtime-normalized*
  `gc_pool_start` segment mod 4, not `main_BSS` para mod 4 — those differ by a build-dependent
  offset).  So after ANY instrumentation edit, SWEEP `--stack-size` over 4 paragraphs (relink-only,
  to /tmp/*.exe) and run all 4; ~2 of 4 FAIL.  Recipe used:
  `for ss in 16384 16400 16416 16432; do omf_link.py -o /tmp/hN.exe --stack-size $ss --gc-sections
  --pack-code <objs>; done` then `run-victor-sasi.sh /tmp/hN.exe 240` (parallelises; ~2.5 wall-min
  each, 4 parallel competes for cores so ~4 min).
- Instrument via `MP_EXTRA_CPPFLAGS="-DMP_DBG_GLOBALS=1" tools/recompile-mp-tu.sh runtime …` then
  `… main …` (relinks).  Image ceiling ~824416 body; the §4p probes (qstr dump + table dump) just
  fit at ~820400 — keep probes lean.  `/tmp/mp_objs.txt` (109 objs) current.
- The §4p instrumentation is LEFT IN PLACE (guarded) in `~/projects/micropython/py/runtime.c`
  (`mp_load_global` NFq+qstr dump + `dbgp_s`/`dbgp_x` helpers) and `ports/dos8086/main.c`
  (`dbg_churn_atb` + the older `dbg_dump_globals` under `MP_DBG_GC`).  Reuse for §4q.

---

# (§4p done above) Next session (§4p — FIND THE WRITER.  §4o nailed the churn(120) corruption to a FAR-DATA-SEGMENT-ALIGNMENT-sensitive wild write with a clean PERIOD-4 signature (FAIL iff far-data seg ≡ {0,3} mod 4), reproducible/maskable purely via `--stack-size` (relink-only).  This also CRACKS the §4k "instrumentation hides it" heisenbug — it was just the far-data segment's mod-4 flipping — so instrumentation is now usable by re-pinning a FAIL alignment with `--stack-size`.  Two armed approaches below.)

## 2026-06-09 §4o notes (BISECT cracked the heisenbug: period-4 far-data-segment-alignment sensitivity; instrumentation now unblocked)

**§4o ran the layout bisect (the §4n approach #1) and it paid off hugely.**  No source
changed; all work was relink-only experiments + reading generated SSA.  The shipping image
(`build/mp-link/mpython.exe`, body **817840**, heap_seg **0xBA8B**) was confirmed to
deterministically FAIL: `mp-churn-scale2.py` prints churn(20…100) correct then churn(120)
→ `NameError` (markers `DE` then `C5`) — exactly §4j.

### The bisect lever: `--stack-size` shifts the far-data SEGMENT, relink-only, no recompile
`STACK` (para 0xB0A1) sits BEFORE every `FAR_DATA` segment, and CODE + near-`DGROUP`
(0xA790) sit before STACK.  So growing `--stack-size` by Δ shifts EVERY far-data segment
(qstr/objstr/…/`main_BSS` heap) up by Δ/16 paragraphs **as a unit**, while CODE and
near-data stay put, and the intra-heap layout (offsets) is byte-identical.  Recipe (≈5 s,
no TU recompile):
```
OBJS=(); while IFS= read -r l; do [ -n "$l" ] && OBJS+=("$l"); done < /tmp/mp_objs.txt
tools/omf_link.py -o /tmp/X.exe --map /tmp/X.map --entry _start \
    --stack-size $((16384+16*k)) --gc-sections --pack-code "${OBJS[@]}"
# heap_seg (main_BSS para) = 0xBA8B + k
```
Relink at k=0 (stack 16384) is **byte-identical** to the committed `mpython.exe` — lever
validated.

### THE RESULT — clean PERIOD-4 flip in the far-data segment value
8 runs on real Victor (`run-victor-sasi.sh`, scale2), one per paragraph:
| stack | heap_seg | seg mod 4 | result |
|---|---|---|---|
| 16384 | 0xBA8B | 3 | **FAIL** |
| 16400 | 0xBA8C | 0 | **FAIL** |
| 16416 | 0xBA8D | 1 | PASS |
| 16432 | 0xBA8E | 2 | PASS |
| 16448 | 0xBA8F | 3 | **FAIL** |
| 16464 | 0xBA90 | 0 | **FAIL** |
| 16480 | 0xBA91 | 1 | PASS |
| 16496 | 0xBA92 | 2 | PASS |

Clean `FFPPFFPP`: **FAIL iff far-data segment ≡ {0,3} mod 4, PASS iff ≡ {1,2}** (period 4
in the segment = period **64 bytes** in the linear base; equivalently the far-data base's
linear-address bits 4–5).  The shipping image is mod-4 = 3 → FAIL.

### What this proves / re-opens
- **It IS a layout/segment-sensitive WILD WRITE** (§4o's hypothesis), and the sensitive
  quantity is the far-data base's **alignment mod 64 bytes** — i.e. some far-pointer
  computation whose overshoot AMOUNT depends on a pointer's segment low-2-bits.  There is NO
  explicit `& ~0x3F`/`+0x3F` alignment mask in the generated asm (grepped) — so it is a
  subtler carry/shift interaction, not a literal round-up.
- **CRACKS the §4k "instrumentation hides it" heisenbug.**  Adding code shifts the far-data
  base segment's mod-4, which has a 50 % chance of flipping FAIL→PASS — that is the entire
  "heisenbug."  **Decoupling fix:** add instrumentation freely, then re-pin a FAIL alignment
  with `--stack-size` (it moves the segment mod-4 INDEPENDENTLY of code size).  This removes
  the §4e/§4k wall — runtime instrumentation is finally usable on this bug.
- **§4l's "GC core is CLEAN" is NOT conclusive.**  That standalone probe ran at some other
  far-data segment whose mod-4 was likely a PASS value, so a segment-mod-4-sensitive GC op
  would not have fired.  **The GC is back on the suspect list** alongside the VM/runtime —
  but only for an op that actually uses a far pointer's SEGMENT in arithmetic.
- The GC MARKING itself is segment-robust for genuine roots (read `gc_collect`,
  `gc_collect_root`, `gc_mark_subtree` SSA): every genuine heap pointer (stack-resident or
  in-object) shares the heap segment, so the same-segment `VERIFY_PTR` bounds checks and the
  `(ptr-pool_start)/16` block math are segment-independent.  So the writer is more likely a
  far STORE in the churn workload (list-comp fill / dict store+rehash / `str(i)` intern) than
  in marking — but verify, don't assume.

### LATENT BUG found en route (NOT the churn cause; fix separately)
minic lowers C pointer **relational** comparisons (`<`,`<=`,`>`,`>=`) as **SIGNED**
(`cslel`/`csltl` in the `VERIFY_PTR` SSA), but C pointer comparisons must be UNSIGNED.
Harmless in THIS image only because every segment is ≥ 0x8000 (all "negative", so signed
ordering matches unsigned), but it is wrong whenever pointers straddle the 0x8000 segment
boundary.  Real bug, own probe + fix when convenient (same family as
[[feedback-minic-unsigned-widen-extsw]] §2r and §4h).

### THE GOAL FOR §4p — catch the writer's PC (now well-armed)
Two approaches, both newly viable:
1. **MAME debugger watchpoint on the shipping FAIL image** (heap_seg 0xBA8B, no rebuild → no
   perturbation).  `mp_state_ctx` is at a FIXED addr (`mpstate_BSS` para 0xBA7A : off 0 in
   the 817840 build; see `mpython.map`).  Boot under `~/projects/mame/mame victor9k … -debug
   -debugger none -debugscript <f>` (adapt the launch from `run-victor-sasi.sh`); break after
   `mp_init` (e.g. at the `C4` marker tx, or `do_str` entry); read `thread.dict_globals` /
   `vm.last_pool` far pointers from mp_state_ctx (offsets per §4n: dict_locals@8,
   dict_globals@12, vm.last_pool@32) to get the long-lived globals-dict / qstr-pool heap
   address; `wpset` a write-watch on its `map.table` / chunk bytes; `go`; the PC that writes
   garbage → the offending function → the far-arith.  Work item = headless 8088 segmented
   debugger scripting (physical addr = seg*16+off; reading a far ptr = combine two words).
   **MAME feasibility ALREADY TESTED this session (partial):** `~/projects/mame/mame victor9k
   … -debug -debugger none -debugscript F -debuglog` runs **headless without hanging**, a
   debugscript of `printf "…",pc` + `go` executes and the machine runs to completion
   (`Average speed: …` on stdout), and `debug.log` (in cwd) is created.  **OPEN PROBLEM = the
   OUTPUT channel:** under `-debugger none` the console is dropped, so the `printf` text does
   NOT reach `debug.log` (only the debugger banner does), and `trace <file>` / `wpset …` either
   error or produce nothing (run exits ~2 s with empty stdout, no trace file, no error in
   debug.log).  So a watchpoint that FIRES can't yet be observed.  NEXT: solve emission — try
   `-debugger gdbstub -debugger_port N` then drive via `gdb` (set the wp + `commands`), OR find
   the correct MAME-0.287 `trace`/`wpset` action syntax that lands in a file, OR `-oslog`.  A
   reusable disk (FAIL `mpython.exe` + scale2 PROG.PY) is staged at `/tmp/mamedbg/run.img`
   (rebuild via `vtg_image_util copy … :0:\\PROG.EXE` / `\\PROG.PY`); the exact launch flags
   are in this session's transcript.
2. **Instrument the external micropython (now that --stack-size re-pins FAIL).**  Add to
   `~/projects/micropython/ports/dos8086/main.c` (or py/gc.c) a check that CHECKSUMS the
   globals dict (`mp_state_ctx.thread.dict_globals` → its `map.table` entries) and/or the
   first qstr-pool chunk after EACH churn iteration (or each `gc_collect`), printing the
   iteration where it first changes unexpectedly → which churn op corrupts it.  Build with
   `tools/build-micropython.sh --model=compact` (or relink one TU via `recompile-mp-tu.sh`),
   then RELINK at a `--stack-size` that lands the far-data segment on mod-4 ∈ {0,3} (the
   instrumented body size differs, so compute k to hit a FAIL seg — try a few; each MAME run
   ~2.5 wall-min with `-nothrottle`).  This is the §4e marking-completeness plan, finally
   unblocked.

### Reproduction cheat-sheet (verified this session)
- FAIL (shipping): `VICTOR_SRC=build/mp-churn-scale2.py tools/run-victor-sasi.sh build/mp-link/mpython.exe 220`
- Toggle alignment by relink (above); FAIL k∈{0,1,4,5,…} (seg mod4∈{3,0}), PASS k∈{2,3,6,7,…}.
- Runs PARALLELISE cleanly (separate temp dirs/serial files) — ran all 6 intermediates at once.
- `/tmp/mp_objs.txt` (109 link objects) is current; `build/mp-link/` is fully populated.

---

# (§4o done above) Next session (§4o — the churn(120) corruption is now isolated to a LAYOUT-SENSITIVE NON-GC WILD WRITE — the ONLY hypothesis left after EVERY structural/algorithmic one was ruled out (GC core §4l, all live-type ptr alignment §4m, mp_state root scan §4n, mark-stack overflow §4k).  Static analysis is fully spent.  §4o MUST observe the wild write at runtime on the shipping image without perturbing it (instrumentation hides it — §4k).  §4i+§4j fixed+verified the far-ptr bug; three GC probes gated.)

## 2026-06-09 §4n notes (mp_state root-scan re-audit — CLEAN; suspect B ruled out; only the wild-write hypothesis remains)

**§4n extended `gc_offset_probe.c`** (the gated §4m audit) to the mp_state root section that
`gc_collect_start` scans, and **statically confirmed it is correct** — closing suspect B
without any MAME run.  The scan covers `[offsetof(thread.dict_locals), offsetof(vm.qstr_last_chunk))`
at a void**=4 stride; the probe prints (compact/large, far-data):
- `root_start`(dict_locals)=**8** (4-aligned — §4g padded it from the packed-6), `root_end`
  (qstr_last_chunk)=**108**, scan window `[8,108)`.
- Every root pointer is 4-aligned AND inside the window: thread.dict_locals@8, dict_globals@12,
  nlr_top@16, pending_exc@24; vm.last_pool@32, dict_main.base@64, dict_main.map.table@72,
  readline_hist@76 (the last root, ending exactly at 108).
So `gc_collect_start` finds every mp_state root → **suspect B (a missed root) is RULED OUT**,
which also agrees with "16 KB works" (a missed root is layout/heap-size-INDEPENDENT and would
fail on the small heap too).

### The suspect set is now a SINGLE hypothesis
RULED OUT (static + faithful repro): GC core algorithm/codegen (§4l); pointer-field alignment
in every live object type incl. the value stack (§4m); the mp_state root scan (§4n); mark-stack
overflow (§4k).  ONLY ONE hypothesis fits ALL the evidence —
**a LAYOUT-SENSITIVE NON-GC WILD WRITE**: some compiled VM/runtime code (NOT the GC, NOT the
gate-verified addfo path) does far-pointer arithmetic that, on the 49 KB heap's specific
address range, computes a target a little past an object and overwrites a live object (the qstr
pool / globals dict → NameError).  This is the UNIQUE fit for: §4k's heisenbug (a ~32-byte
image shift moves the target onto harmless padding); "16 KB works, 49 KB fails" (the bad target
only lands on a live object when the heap occupies the larger offset range); and all
GC/structural analysis being clean.

### THE GOAL FOR §4o — find the wild write at runtime (the only avenue left)
A wild write's target depends on RUNTIME addresses, so no static audit can find it — §4o must
observe it on the SHIPPING `build/mp-link/mpython.exe` (no source change → no layout shift;
§4k proved any added code hides it).  Concrete approaches, by tractability:
1. **Layout-sensitivity BISECT (most tractable, mechanical).**  Add a sized UNUSED global
   (e.g. `static char pad[N];` in a TU, or a linker pad) to shift the image by N bytes, and
   binary-search N over [0, 64] for the fail↔pass threshold (each step = recompile-one-TU +
   one ~3-min MAME run).  §4k bracket: clean 817840 FAILS, +112 (817952) PASSES.  The flip
   granularity (does it flip every 2 / 4 / 16 bytes?) reveals the target's alignment, and the
   absolute address at the flip, cross-referenced with `build/mp-link/mpython.map`, localizes
   WHICH data region gets clobbered → which code writes near it.
2. **MAME debugger observation (definitive, harder).**  Boot under MAME `-debug` with a
   `-debugscript`: break at `gc_collect` (or `do_str`), walk `mp_state_ctx` (static addr from
   the .map) → dict_globals → map.table to get the live globals-table heap address, `wpset`
   write-watch it, continue, and catch the PC of the instruction that writes garbage to it.
   That PC → the function → the offending far-arith.  Headless MAME debugger scripting
   (expressions reading memory via `dword(...)`, conditional `wpset`) is the work item; adapt
   the launch from `tools/run-victor-sasi.sh`.
3. **Suspect-guided code audit.**  The wild write is far-arith that overshoots on large
   offsets.  §4i fixed `far_ptr ± idx` (addfo, gate-verified); look for OTHER far-arith shapes
   minic emits that DON'T go through addfo and could overrun: e.g. `memcpy`/`memmove`/`memset`
   length or dest computed with a far pointer near the segment top, struct-copy byte loops,
   `m_renew`/array-grow far-pointer recomputations, or a far-pointer COMPARE used as a bound
   that mis-orders at high offsets.  Grep the generated `build/mp-link/*.asm` for far stores
   (`mov [es:...]`) whose address is computed by a non-addfo add/adc pair.

   **Already CLEARED under approach 3 (don't re-tread):**
   - **libstub `_far_memset`/`_far_memcpy`/`_far_memmove` are correct for heap buffers** — the
     count is 16-bit (size_t=2) and `dest+count` stays inside the heap `[0x1200,0xD200]` (<
     0xFFFF), so `rep stosb/movsb`'s DI never wraps the segment.  Not the overshoot source.
   - **A missed REGISTER root (live far ptr only in a callee-saved reg across the collection,
     not on the scanned C stack) is ruled out** by spill.c `force_kl_slot`: every Kl far-ptr
     temp is slot-resident, so the value is always in a stack slot the conservative scan
     covers.  §4l's repro (which roots its retained chain ONLY via the same C-stack scan +
     globals) was clean, corroborating the invariant.  Also, a missed register/root would be
     layout-INDEPENDENT (regalloc is fixed per binary), contradicting §4k.
   So approach 3 should focus on **minic-emitted far STORES from the VM/runtime** (objstr /
   objlist / vm value-stack writes / mp_obj_new_* fill loops) whose dest far-arith is NOT the
   addfo path and could compute a target past the object — OR just do the runtime bisect (1)
   which doesn't depend on guessing the shape.

### Three gated GC probes (regression guards locked in this session)
`gc_churn_probe` (§4l) — faithful GC-core + far-ptr-fix guard.  `gc_offset_probe` (§4m+§4n) —
§4g far-data alignment guard for every live object type AND the mp_state root section.  Plus
`gc_bigheap_probe` (§4i).  Gate 228/228 (gc_offset_probe golden extended with the mp_state
lines; same 2 gate entries).

---

# (§4n done above) Next session (§4n — all STRUCTURAL/algorithmic GC hypotheses for churn(120) are now EXHAUSTED by static audits + a faithful repro; the bug is a LAYOUT-SENSITIVE NON-GC WILD WRITE (or an mp_state-scan residual) that needs RUNTIME OBSERVATION of the shipping image.  §4i+§4j fixed+verified the far-ptr bug; §4k=heisenbug; §4l=GC core CLEAN; §4m=all live-type pointer fields 4-aligned.  Two probes gated.  No more static angles — observe the corruption at runtime without perturbing.)

## 2026-06-09 §4m notes (static layout audit — all live-type pointer fields are 4-aligned; structural hypotheses exhausted)

**§4m built `minic/dos/examples/gc_offset_probe.c`** (GATED compact+large, no GC at runtime —
pure `offsetof`/`sizeof` prints; struct defs copied VERBATIM from `build/mp-link/*.pp.c`).
It verifies §4l's hypothesis #1: does every far-POINTER field in MicroPython's live heap
object types sit at a `sizeof(void*)`=4-aligned offset, so the conservative GC's 4-stride
`gc_mark_subtree` scan finds it (a 2-mod-4 pointer is split across reads → freed-while-live,
the §4f bug class)?

**RESULT: ALL 4-ALIGNED** under far-data (`sizeof void*=4, size_t=2, mp_obj_t=4`):
- `qstr_pool_t`: prev@0, **lengths@12, qstrs@16** (§4g correctly padded lengths from the
  packed-10 to 12), sizeof 16.
- `mp_map_t.table`@4; `mp_map_elem_t.key`@0/`value`@4 (sizeof 8); `mp_obj_dict_t` → `map.table`@8.
- `mp_obj_list_t.items`@8.
- stackless `mp_code_state_t`: fun_bc@0, ip@4, sp@8, old_globals@16, prev@20, **value stack
  state[]@24** — all 4-aligned (sizeof 24).
So §4g's alignment is correct for every type behind a name lookup AND the live value stack →
the GC scan finds every child pointer.  **Hypothesis #1 (off-stride pointer field) is RULED OUT.**

### Suspect set after §4k+§4l+§4m (structural causes exhausted)
RULED OUT: GC core algorithm/codegen (§4l); pointer-field misalignment in
qstr_pool/map/dict/list/code_state incl. the value stack (§4m); mp_state root-scan alignment
(§4g, offsetof(thread.dict_locals) 4-aligned); mark-stack overflow (§4k).
REMAINING (the only ones left):
- **(A) A layout-sensitive NON-GC WILD WRITE** — far-pointer arithmetic somewhere in the
  compiled VM/runtime (NOT the GC, NOT the simple gate-verified addfo path) that, on the big
  heap's specific addresses, overruns an object boundary into a live object.  This is the BEST
  fit for §4k's heisenbug profile (the overrun lands on something critical in one image layout,
  harmless padding in another).
- **(B) An mp_state root-scan residual** not caught by §4g's single alignment check (e.g. the
  `gc_collect_start` scan's start/length rounding misses a root at the section's edge, or a
  root field added since §4g).  Cheap to re-audit statically: extract `mp_state_ctx_t`/
  `mp_state_thread_t`/`mp_state_vm_t` from a `*.pp.c`, confirm offsetof(thread.dict_locals)%4==0
  and that every pointer between it and offsetof(vm.qstr_last_chunk) is 4-aligned AND inside the
  scanned `[root_start/4*4, root_end)` window.

### THE GOAL FOR §4n — observe the corruption at runtime (static angles are spent)
Static audits + the faithful repro have exhausted the structural hypotheses, so the next step
MUST observe the actual wild write/freed object at runtime on the SHIPPING image (no source
change → no layout perturbation; §4k proved instrumentation hides it).  Options, hardest-payoff
first:
1. **MAME debugger, non-perturbing.**  Boot `build/mp-link/mpython.exe` under MAME `-debug`
   with a `-debugscript` file (adapt the launch from `tools/run-victor-sasi.sh`).  The
   corrupted memory is dynamic (heap), so a fixed `wpset` is hard; instead consider: (a) a
   TRACE of all far writes (`mov [es:...], ...`) whose target is in the heap segment during the
   churn(120) collection window, diffed against a passing run; or (b) break at `gc_collect`
   entry/exit and DUMP the qstr-pool / globals-dict region (addresses found by walking
   mp_state_ctx in the debugger) to see WHICH bytes change to garbage and WHEN.  Headless MAME
   debugger scripting is the real work item.
2. **Static re-audit of the mp_state root section** (suspect B above) — purely static, cheap,
   do it FIRST as it may close B without any run.
3. **Bisect the layout sensitivity** to localize: pad the image by N bytes (a sized unused
   global) in small steps and find the fail↔pass threshold; the threshold maps to which data
   region's address alignment triggers the wild write — narrows (A) to a specific structure.

### Two gated probes this session
`gc_churn_probe` (§4l, GATED compact+large) — faithful GC-core + far-ptr-fix guard.
`gc_offset_probe` (§4m, GATED compact+large) — §4g far-data alignment guard (a future minic
alignment regression would re-introduce the §4f freed-while-live class; this catches it).
Gate 226 → 228.

---

# (§4m done above) Next session (§4m — the churn(120) GC corruption is NOT in the GC core algorithm: a faithful self-contained mark/sweep repro on a 49 KB far-data heap (18 collections, multi-level marking, far-array indexing) is CLEAN.  The bug is in the MicroPython-specific layer (object internals / mp_state roots) or a layout-specific wild access from non-GC code.  Next: a NON-PERTURBING MAME-debugger watchpoint on the real image (instrumentation hides it).  §4i+§4j DONE; §4k+§4l narrowed it sharply.)

## 2026-06-09 §4l notes (built the fast-repro; it CLEARS the GC core; bug is MicroPython-layer or a non-GC wild access)

**§4l built `minic/dos/examples/gc_churn_probe.c`** — a self-contained, faithful copy of the
gc.c CORE (2-bit ATB FREE/HEAD/TAIL/MARK, gc_setup_area table/pool split, gc_alloc first-fit
block scan, the bounded mark stack(64) + gc_deal_with_stack_overflow rescan, sweep, and a
conservative dual-aligned root+C-stack scan), driven by a churn workload that EXPLICITLY
verifies a retained singly-linked chain (40 nodes) AND a two-level dict-like container
(header → far table[] → value nodes) after each forced collection.  The explicit verify makes
it MORE sensitive than MicroPython (which only notices corruption when a wild access hits the
qstr pool/globals and raises NameError).  GATED compact+large (golden layout-independent —
counts derive from HEAP_BYTES; identical compact/large).  Gate **224→226**.

**RESULT: CLEAN.** compact AND large far-data, 49 KB heap, **18 collections** under heavy churn
(varied-size garbage → fragmentation; 0 overflows, consistent with §4k), the retained chain
AND dict survive every collection → `ALL OK`.  So the GC core is correct on far-data:
mark/sweep/alloc, the conservative dual-aligned scan, MULTI-LEVEL marking (dict→table→values),
FAR-ARRAY indexing (`table[i]` = the §4i addfo path), and varied sizes/fragmentation are all
fine.  **The MicroPython churn(120) corruption is NOT reproduced by a faithful standalone GC.**

### What this RULES OUT and what's LEFT
RULED OUT (by §4k + §4l): mark-stack overflow path; the GC core mark/sweep/alloc algorithm;
the conservative scan; multi-level marking; far-array indexing; far-ptr arith (addfo).
LEFT (the bug must be one of):
1. **MicroPython object INTERNALS** the probe doesn't model — a specific type whose pointer
   still sits at a non-stride offset despite §4g's struct-member alignment (e.g. a flex-array
   member, a union, an embedded sub-struct, or the qstr_pool/str/dict-map exact layout), so
   `gc_mark_subtree`'s sizeof(void*) stride skips a live child.  → Re-audit the ACTUAL offsets
   of pointer fields in the live object types (qstr_pool_t, mp_obj_dict_t/mp_map_t,
   mp_obj_str_t, the stackless code_state frame) in the GENERATED far-data layout, not on paper.
2. **The mp_state root section** scan (my probe used simple explicit roots) — re-verify
   offsetof(thread.dict_locals)..vm.qstr_last_chunk are all 4-aligned AND the void**-stride
   `gc_collect_start` scan covers every root in the real generated struct.
3. **The stackless VM value-stack / code_state** rooting — frames are heap objects reached via
   the code_state chain / a C-stack pointer; a live value-stack slot at a deep collection may
   not be covered.
4. **A non-GC WILD WRITE** — far-arith somewhere in the VM/runtime that, on the big heap's
   specific addresses, writes past an object into a live one (consistent with §4k's
   layout-sensitivity: it hits something critical in one layout, padding in another).

### THE GOAL FOR §4m — a NON-PERTURBING observation of the real image
Since the bug is layout-sensitive (any added code hides it — §4k) and a simplified probe
doesn't reproduce it (§4l), stop trying to add markers/probes.  Use a **MAME debugger memory
WATCHPOINT** on the SHIPPING clean image (no source change → no layout perturbation): boot
`build/mp-link/mpython.exe` under MAME `-debug` with a scripted command file, set a write
watchpoint on the qstr-pool / a known-live object's memory (address from the `mpython.map` for
statics, or discovered live), run `mp-churn-scale2.py`, and catch the instruction that writes
the wild value.  That PC → the offending function → the codegen/source bug.  Headless MAME
debugger scripting is the hard part (a `-debugscript` file with `wpset`/`bpset`/`trace`); the
Victor harness (`tools/run-victor-sasi.sh`) shows the launch/serial plumbing to adapt.
Alternative if watchpoints are impractical: audit the generated far-data field offsets of the
live object types (item 1 above) directly from the `build/mp-link/*.ssa`/`*.asm` — purely
static, no runs, no perturbation.

### Probe is also a permanent regression guard
`gc_churn_probe` stays gated: it's the strongest in-tree exercise of the §4i far-ptr fix +
GC-core far-data correctness (multi-level marking + far-array indexing across 18 real
collections).  Build: `QBE_FAR_STATIC_DATA=1 tools/build-example.sh --model=compact
minic/dos/examples/gc_churn_probe.c`; the heap size is `-DGC_HEAP_BYTES` overridable (49152
default; a NEAR-data/medium build needs a small heap — a 49 KB near heap overflows DGROUP,
which itself proves the bug requires far data).

---

# (§4l done above) Next session (§4l — churn(120) GC corruption on the 49 KB heap is a LAYOUT-SENSITIVE near-miss; the MAME loop is unsuitable to chase it (instrumentation hides it).  Build the MEDIUM-MODEL DOSBox fast-repro.  §4i+§4j are DONE (far-ptr fix landed, gate 224/224, Victor-verified).  §4k narrowed the residual: overflow RULED OUT, collections work, 16 KB clean — it's a layout-dependent heap-corruption heisenbug.)

## 2026-06-09 §4k notes (diagnosed the churn(120) corruption: layout-sensitive heisenbug; overflow ruled out; NO qbe code change)

**§4k is a DIAGNOSIS pass on the §4j-surfaced churn(120) NameError — no compiler bug
found, no qbe source changed (only a fast-loop harness fix + docs).**  The bug is in the
MicroPython conservative GC under heavy churn on the BIG heap; it is **layout-sensitive**
(a near-miss wild access), which makes the slow + perturbing MAME loop the wrong tool.

### What was measured (real Victor, `tools/run-victor-sasi.sh`, scale2 = churn 20→120)
All builds compact far-data, stackless, MP_STACK_SIZE=16384.  Markers added temporarily to
EXTERNAL `py/gc.c` (now reverted): `O` = a mark-stack overflow round (`gc_deal_with_stack_overflow`);
`g` = one per collection (`gc_collect_start`).
| build | heap | result |
|---|---|---|
| clean §4i (body 817840) | 49 KB | **FAIL** churn(120) `NameError` (the §4j result) |
| + `O` marker only (817920) | 49 KB | **FAIL** churn(120) `NameError`, and **NO `O`** → overflow path never fires |
| + `O`+`g` markers (817952) | 49 KB | **PASSES** `120 7980` DONE, only 2 collections (`g` at churn100, churn120), no `O` |
| + `O`+`g` markers | 16 KB | **PASSES** `120 7980` DONE, ~8 collections, no `O` |

### Conclusions (sharp)
1. **Mark-stack overflow is RULED OUT** — `O` never printed before the failure.  The
   `gc_deal_with_stack_overflow` O(blocks) path is NOT involved (so raising
   `MICROPY_ALLOC_GC_STACK_SIZE` is moot, again).
2. **Collections themselves work** — 16 KB does ~8 clean collections and completes; the
   markers don't change marking logic.
3. **It is a LAYOUT-SENSITIVE heisenbug.**  A ~32-byte image shift (adding the `g` marker:
   817920 FAIL → 817952 PASS) makes the corruption vanish.  MAME is deterministic
   per-binary, so this is across BINARIES (layout), not across runs — but the *shipping*
   clean build (817840) deterministically FAILS.  The corruption is a near-miss wild
   access that lands on something critical in the clean layout and on harmless padding in
   the shifted layout.  → ANY on-target instrumentation perturbs the layout and can hide
   the bug, so the MAME loop is the WRONG tool.
4. It fires at the churn(120) collection point on the big heap (the marker build's 2nd `g`
   is exactly there).  On 49 KB, `MICROPY_GC_ALLOC_THRESHOLD`=0 means collection only
   happens when the heap is nearly full, so the *first* collection is very late (deep into
   churn) with a large, specific live state — unlike 16 KB which collects early and often.
   This is the §4d/§4e/§4f "a LIVE heap object is freed across a collection" family; §4g's
   struct-alignment fix cleared it for moderate pressure (feature-probe) and the small heap,
   but not for this big-heap late-collection case.

### THE GOAL FOR §4l — build the MEDIUM-MODEL DOSBox fast-repro (the §4e plan's key tool)
The MAME loop (~3 min/run) + layout-sensitivity make on-target debugging impractical.  Build a
self-contained DOS probe that links `py/gc.c` with stub `mp_state`/`mphal`, allocates
cross-linked objects, drops most, forces `gc_collect`, reallocates, and verifies a retained
object's contents — at DOSBox speed (seconds/iter), where layout can be controlled and
instrumentation added freely.
- If it reproduces in the MEDIUM model (near-data, 2-byte pointers) → it's a GC LOGIC bug
  (mark/sweep/block-math), debuggable fast in DOSBox.
- If it does NOT reproduce in medium but DOES in a compact/far-data probe → it's
  far-data-specific (a far load/store / far-ptr value read at a wrong offset in the
  collection paths), which itself narrows it sharply.
- Either way, instrument MARKING COMPLETENESS directly (§4e step 1): capture a known-live
  object's block before the collection, assert its ATB kind != FREE after `gc_collect_end`,
  and print WHICH block is swept-while-live.  In a self-contained probe the live set is
  known exactly, so the smoking gun is unambiguous.
Suspects to check in the probe (codegen is otherwise clean — far-ptr arith now via addfo):
the conservative C-stack scan range `[sp, stack_top)` vs where a live root actually sits at
the deep collection point; a multi-block live object whose tail words (child pointers) aren't
traced; or a far-pointer VALUE inside a live container read at a wrong offset by the trace.

### Harness fix committed this session (real bug found while diagnosing)
`tools/recompile-mp-tu.sh` defaulted `MP_STACK_SIZE=24576` but `build-micropython.sh` uses
**16384** (the §4b stackless default), so a fast-loop relink reserved 8192 more stack than the
full build → a clean gc.c relink came out **826032**, OVER the ~824416 "Program too big"
ceiling → the relinked .exe would not load on Victor.  Fixed the default to 16384 (override via
env).  Use `MP_STACK_SIZE=16384` explicitly if on an older copy.  Reminder: the
`gc_bigheap_probe`/§4i scope-note items (huge `_qbe_huge_add` >=0x8000 gap; build-example.sh
-DFAR_DATA) are still open, independent, lower-priority.

---

# (§4k diagnosed above) Next session (§4k — the NEW frontier is churn(120) GC-pressure corruption on the 49 KB heap.  §4i+§4j are DONE: the offset-only far-pointer fix LANDED, is gate-verified 224/224, AND is Victor-verified on real MicroPython — the far-ptr churn(80) stall is GONE.  The remaining failure is a SEPARATE, newly-reachable GC bug.)

## 2026-06-09 §4j notes (VICTOR RE-VERIFY of the §4i far-pointer fix — DONE; far-ptr stall fixed; a further GC frontier surfaced)

**§4i+§4j land the far-pointer fix end-to-end.**  §4i implemented offset-only
far-pointer arithmetic (`addfo`/`subfo`); §4j re-verified it on a REAL Victor 9000
(MAME, `tools/run-victor-sasi.sh`, compact far-data, stackless, 49 KB heap — the exact
config where §4g/§4h saw the churn(80) stall).  **The far-pointer bug is fixed on the
actual consumer**, and a §4i refinement (below) made the MicroPython image SMALLER.

### §4i refinement committed after the first build: VARIABLE-index only
The first §4i `far_ptr_offset_binop` fired for BOTH constant and variable offsets.
That GREW the MicroPython image +2304 B (body 820400→822704) because a CONSTANT scaled
offset (`arr[const]`, `&arr[const]`, `p + const`) that QBE used to FOLD into a single
relocated `CAddr` was being routed through the opaque (non-foldable) `addfo`, defeating
the fold.  The §4h scope was always **variable-index only** (the bug needs a runtime
index >= 0x8000 or a runtime-wrapped negative delta; a constant is folded + linker-resolved).
Added `if (soff.t == Con) return 0;` to `far_ptr_offset_binop`.  Net effect: constant
far-arith folds again AND variable far-arith drops the `adc` → image **820400 → 817840
body (-2560 B vs the §4g baseline)**, more headroom under the ~824416 ceiling.  Gate
re-run **224/224 ok** (the restriction changes no runtime output — constant far-arith
reverts to the previously-passing flat add).

### Victor results (real hardware; redirect-to-file, never pipe through tail — [[feedback-victor-harness-pipe-buffer]])
| probe | result |
|---|---|
| `build/mp-churn-scale2.py` (49 KB heap) | `20 330` `40 1060` `60 2190` **`80 3720` `100 5650`** then churn(120) → `NameError`.  **churn(80)/(100) now CORRECT — was a hard hang at churn(80) pre-§4i.** |
| `build/mp-feature-probe.py` | **23/23 OK** (mul…enum incl. `comp`/`gen`/`sort` — the exact checks the §4h naive-extuw attempt REGRESSED to `ER list`/`XX gen`).  No regression. |
| `build/mp-fill-probe.py` | clean: all 16 markers `0`…`960` + `D4`/`C5` — 1000 iters force MANY gc_collects on the 49 KB heap (non-retained garbage), no corruption/hang. |

### THE NEW FRONTIER FOR §4k — churn(120) NameError (a SEPARATE GC-pressure bug, NOT far-ptr)
`scale2` now advances from the old churn(80) hang all the way to **churn(120)**, where it
raises `NameError: name not defined` at module scope (marker `DE`, then clean `C5`).  This
is NOT the far-ptr bug (the 49 KB heap maxes at offset ~0xC000, all < 0x10000 and all
covered by addfo; churn(80)/(100) at the same offsets are correct) and NOT a §4i regression
(pre-§4i it never even reached churn(120) — strictly more iterations complete correctly now).
It is the **§4d/§4e/§4f "a live heap object is freed across a GC collection under heavy
churn"** family — `NameError` at module scope = a freed-while-live qstr_pool / global-dict
(the §4f symptom).  §4g's struct-alignment fix cleared it for MODERATE pressure
(feature-probe 23/23, fill-probe clean) but NOT for scale2's extreme churn on the big heap —
and §4g never actually confirmed scale2 completing on 49 KB (it was blocked first by the
far-ptr bug, then mis-attributed to a perf cliff).  Now the far-ptr bug is gone, this is the
exposed remaining issue.  Plan (per the §4e discipline — measure, don't reason):
- Reduce on real Victor with the `build/mp-churn-*.py` family + a SMALLER heap to force the
  collection earlier (`MP_HEAP_SIZE=16384 tools/recompile-mp-tu.sh main …` then run scale2 /
  churn-lit).  §4e already showed churn-lit corrupts on a 16 KB heap when a live object spans
  a collection — re-confirm it still does post-§4g+§4i (it may now, since the far-ptr fix
  changes nothing about marking completeness).
- Instrument MARKING COMPLETENESS directly (§4e step 1/2): in the port `gc_collect` (external
  tree), capture a known-live object's block before the collection and check its ATB kind is
  not FREE after `gc_collect_end`.  Find WHICH live block is swept.
- Suspect: conservative C-stack scan range, or a multi-block live object's tail words (child
  pointers) not traced, or a far-pointer VALUE inside a live container read at a wrong offset.
- A medium-model DOS probe linking `py/gc.c` with stub `mp_state` (cross-linked objs, drop
  most, force gc_collect, verify a retained object) would give a DOSBox-speed repro — if it
  reproduces in medium it's not far-data-specific; if not, it's in the far load/store paths.

### OPTIONAL §4k side-tracks (lower priority, independent)
- **Huge-mode `_qbe_huge_add` >= 0x8000 gap**: `gc_bigheap_probe` still FAILS under
  `--model=huge` (NOT gated there).  Orthogonal to §4i (huge uses huge_ptr_binop →
  `_qbe_huge_add`, untouched; huge codegen byte-identical before/after).  Probe `rt`
  (far-ptr DIFF) is correct under huge so `*p` writes fine; only `pool[off]` (the
  `_qbe_huge_add` read) is wrong.  Fix in the libstub helper / `huge_ptr_binop` unsigned
  widening.  Real consumer (MicroPython) runs compact, so low priority.
- **build-example.sh -DFAR_DATA gap**: still self-#defined by gc_bigheap_probe.  Clean fix =
  build-example.sh adds `-DFAR_DATA=1 -DDOS_FAR_DATA=1` for compact/large/huge; VERIFY it
  doesn't shift farglobal/fardata/farstruct_ptr goldens (medium stdint_probe asserts sizeof==2).

---

# (DONE — §4i+§4j landed, Victor-verified) Next session (§4j — VICTOR RE-VERIFY the §4i far-pointer fix on real MicroPython, then optionally close the orthogonal huge-mode `_qbe_huge_add` >=0x8000 gap.  §4i LANDED the offset-only far-pointer arithmetic fix in the compiler; DOSBox gate is 224/224 and the reduction probe is ALL OK, but the MicroPython end-to-end payoff on Victor is NOT yet confirmed.)

## 2026-06-08 §4i notes (THE FIX LANDED — offset-only 16-bit segment-preserving far-pointer arithmetic; DOS gate green; Victor re-verify still pending)

**§4i implemented the §4h-scoped fix.**  `far_ptr ± idx` on compact/large (and explicit
`__far` in any non-huge model) now lowers to dedicated **offset-only** backend ops
`addfo`/`subfo`: add/sub ONLY the 16-bit OFFSET word, segment preserved (no `adc`/`sbb`).
That is correct for BOTH a true large offset >= 0x8000 (MicroPython gc_alloc's
`pool_start + start_block*16` on a >32 KB heap) AND a 16-bit-wrapped "negative" `size_t`
delta — which neither `extsw` nor `extuw` of a flat 32-bit add can handle at the same time.

### What changed (4 tracked files; see [[project-far-ptr-unsigned-index-bug]])
- `ops.h`: new public ops `O(addfo,…)`/`O(subfo,…)`, `T(e,l,e,e, e,l,e,e)`, ALL flags 0
  (opaque to fold/gvn/copy so nothing rewrites them back to plain `add`).  Placed right
  after `faroff`, outside every `INRANGE` op range.  **No `tools/lexh.c` / `parse.c` K
  regen needed** — the existing perfect-hash `K=362902335`/`M=23` had slack for two more
  tokens (verified empirically: asserts are ON, and qbe `lexinit()`'s collision assert did
  NOT fire; `addfo` parses).  If you ever add MORE ops and it DOES collide, regenerate via
  lexh.c — but note lexh.c's `tok[]` is already stale vs the real op set (missing
  loadfs/storefs/vargp/callfar), so sync it to all public optab names first.
- `i8086/emit.c`: `case Oaddfo: case Osubfo:` in the Kl switch.  Loads the far ptr to DX:AX
  (DX=segment, AX=offset), `add/sub ax, <arg1 LOW word>` with NO adc/sbb, stores DX:AX
  back (segment word unchanged).  arg1's HIGH word is deliberately ignored — that's exactly
  the part that would wrongly carry into the segment.  5 insns vs the old flat-add's 6.
  Bracketed with `kl_save_axdx`/`kl_stage_arg` like Osub Kl; `die()`s on a CAddr offset.
- `minic.y`: new `far_ptr_offset_binop()` emits `=l addfo/subfo`; called in the prefix
  inc/dec site AND the default-Binop site (covers `a[i]`, `p+i`, `p-i`, postfix `p++/--`).
  Excludes MHuge (huge_ptr_binop runs first), fn-pointers (CS), and near (16-bit) pointers.
  **The Scale path is UNCHANGED** — the backend reads only arg1's low 16 bits, which already
  equal `(idx*sz) mod 0x10000` regardless of the sext, so no front-end extension change.

### Verified (this session, all on macOS/DOSBox — NO Victor run yet)
- `make check` green (generic backends unaffected; new ops are i8086-only in emit).
- `tools/test-dos.sh` **224/224 ok** (was 222; +2 = `gc_bigheap_probe` compact+large).
  Every pre-existing far-data/medium/huge probe still passes → no regression from addfo/subfo.
- `gc_bigheap_probe.c`: was `FAIL` (b>=2048 `direct` wrong/zero), now `ALL OK` under compact
  AND large.  Generated asm confirms the offset-only shape: `mov ax,[off]; mov dx,[seg];
  add ax,[idx]; mov [res],ax; mov [res+2],dx` — no `adc`.  Probe is now GATED (compact+large).

### THE GOAL FOR §4j (the real payoff, NOT yet done)
1. **Victor MicroPython re-verify** (`tools/run-victor-sasi.sh`, compact far-data, stackless).
   The DOSBox probe proves the codegen; this proves the consumer.  Run on real Victor:
   - `build/mp-churn-scale2.py` on the **49 KB** heap must now reach `120 7980` / `DONE`
     (the §4g/§4h stall point was churn(80) ≈ 33 KB ≈ just past offset 0x8000 — exactly
     the bug; it should be GONE).
   - `build/mp-feature-probe.py` must stay **23/23** (the §4h extuw attempt regressed this to
     `ER list`/`XX gen`; addfo must NOT — it preserves the wrapped-negative-delta path).
   - `build/mp-fill-probe.py` → `g G DE` / DONE.
   - HARNESS GOTCHA (cost runs before): do NOT pipe `run-victor-sasi.sh` through `tr`/`tail`/
     `head` — the watchdog subshell inherits the pipe fd and blocks ~WALL_SECS.  Redirect to
     a file then filter.  `-nothrottle` makes a 300 emulated-sec run finish in ~2 wall min.
   - The committed MicroPython image is the §4g baseline; rebuild it from the EXTERNAL tree
     with `tools/build-micropython.sh --model=compact` (no external-tree change needed — the
     §4g struct-alignment fix + §4i offset-only arith are both in the qbe repo now).
2. **OPTIONAL — close the orthogonal huge gap.**  `gc_bigheap_probe` still FAILS under
   `--model=huge` (NOT gated there).  That path is `huge_ptr_binop` → `_qbe_huge_add`
   (segment-normalising libstub helper, needed because huge objects can exceed 64 KB), which
   has its OWN >=0x8000 bug — untouched by §4i (huge codegen is byte-identical before/after).
   The probe's `rt` (far-ptr DIFFERENCE) is correct under huge, so `*p` writes fine; only
   `pool[off]` (the `_qbe_huge_add` read) is wrong.  Reduce to the `_qbe_huge_add` helper in
   `tools/libstub_to_exe.py` / `minic.y::huge_ptr_binop` (the unsigned `extuw` widening +
   the helper's offset normalisation for an offset whose top bit is set).  Lower priority —
   the real consumer (MicroPython) runs compact, not huge.
3. **Incidental harness gap (do alongside, verify goldens):** `tools/build-example.sh` does
   NOT pass `-DFAR_DATA` to cpp (only `build-micropython.sh` does), so compact/large probes
   get 16-bit `uintptr_t`.  `gc_bigheap_probe.c` self-`#define`s `FAR_DATA`/`DOS_FAR_DATA` to
   work around it.  Clean fix = build-example.sh adds `-DFAR_DATA=1 -DDOS_FAR_DATA=1` for
   compact/large/huge — but VERIFY it doesn't shift farglobal/fardata/farstruct_ptr goldens
   (medium stdint_probe asserts sizeof==2, so leave medium alone).

---

# (DONE — §4i landed) Next session (§4i — IMPLEMENT THE FIX: offset-only 16-bit segment-preserving far-pointer arithmetic.  §4h root-caused the scale2 churn(80) stall to a REAL minic bug — `far_ptr + unsigned_index >= 0x8000` sign-extends → wild pointer; the §4f/§4g "perf cliff" framing was a FALSE hypothesis; no GC even runs before the stall.  Reduction probe + evidence committed; naive fix reverted because it regresses wraparound deltas.)

## 2026-06-08 §4h notes (root-caused the churn stall to a far-pointer codegen bug; reduction probe committed; proper fix scoped, NOT yet landed)

**The §4f/§4g "perf cliff" attribution was FALSIFIED.**  Those notes *hypothesised*
(never instrumented) that scale2 stalling at churn(80) on the 49 KB heap was a
`gc_block_stack` overflow → O(blocks) rescan perf cliff.  This session instrumented
it and the hypothesis is wrong: **no garbage collection runs at all before the
stall.**  So raising `MICROPY_ALLOC_GC_STACK_SIZE` (this session's original task)
is moot and was abandoned.  The stall is a genuine compiler bug.

### How it was found (the discipline paid off)
1. Added a depth probe (`gc_block_stack` high-water "H<n>" print) + `g`/`G` markers
   around the port `gc_collect` + an `o` overflow marker — all behind a config
   macro, in the EXTERNAL tree (now reverted).  Ran scale2/loc80 on real Victor:
   **zero `g`/`G`/`o`/`H`** ever printed → no collection, no overflow, no marking
   before the hang.  (Markers proven present in `main.asm`/`gc.asm`; `gc_alloc`'s
   only collect-trigger is gc.c:934/984, confirmed reachable.)  `MICROPY_GC_ALLOC_
   THRESHOLD` is 0 at MINIMUM ROM level, so nothing forces an early collection.
2. `build/mp-fill-probe.py` (forced fill, nothing retained) → `g G DE`: a
   collection *does* run/complete once the heap fills, then the program raises.
   `build/mp-churn-loc80.py` (per-iteration markers) → dies at churn(80) iter ~12
   ≈ **~33 KB of monotonic allocation — just past 32 KB (offset 0x8000)** into the
   single-segment heap.  16 KB heap completes (offsets < 0x4000); 49 KB breaks.
   The "just past 0x8000" signature pointed straight at a 16-bit-offset sign flip.
3. Reduced to `minic/dos/examples/gc_bigheap_probe.c` (compact far-data, DOSBox —
   a SECONDS loop, no Victor).  Its SSA is the smoking gun.

### THE BUG (committed repro: `gc_bigheap_probe.c`)
minic's pointer-scale path (`minic.y`, `prom()` label `Scale:`) lowers
`far_ptr + <variable index>` as a **flat 32-bit add of a SIGN-EXTENDED index**:
it unconditionally `sext(r)`s a sub-long index before `=l mul`/`=l add`.  When the
index is an UNSIGNED byte offset ≥ 0x8000 (top bit of the 16-bit offset set),
`extsw` makes it negative, so `ptr + off` lands at a wild address BELOW the object.
MicroPython's `gc_alloc` returns exactly this shape — gc.c:1020
`ret_ptr = area->gc_pool_start + start_block * BYTES_PER_BLOCK` — so on a >32 KB
heap, any block in the upper half (`start_block >= 2048`, offset ≥ 0x8000) is
handed back at a bogus address → heap corruption.  Same family as
`[[feedback-minic-unsigned-widen-extsw]]` (§2r), but at the pointer-scale site,
not a cast.  Probe SSA: `%t155 =l extsw %t154` for `pool[off]`.

### WHY a naive extuw fix is WRONG (do NOT just flip extsw→extuw)
Tried `if (ISUNSIGNED(r->ctyp)) extuw else extsw` in the Scale path.  It fixed
`gc_bigheap_probe` (ALL OK) and `make check` stayed green — but on Victor it
**REGRESSED the common path**: MicroPython `feature-probe` went 23/23 → `ER list`
/ `XX gen`, and scale2 raised immediately (`<class 'iterator'>`).  Reason: the
flat-32-bit-add model is fundamentally wrong for far pointers.  `extsw` was
"accidentally correct" for code that builds a *negative* `size_t` delta (16-bit
wraparound, e.g. `ptr + (a - b)` with `a < b`): `extsw` of the wrapped 16-bit
value reproduces the intended backward move, whereas `extuw` turns it into a huge
forward jump.  So:
- `extuw` ✔ true-large-offset (gc_alloc), ✘ wrapped-negative delta (list/gen).
- `extsw` ✔ wrapped-negative delta, ✘ true-large-offset ≥ 0x8000 (gc_alloc).
No single extension on a flat 32-bit add handles both.  **REVERTED** (minic.y back
to unconditional `sext`; MicroPython image byte-restored to the §4g baseline
843648 / body 820400; gate untouched, `make check` green).

### THE FIX TO IMPLEMENT NEXT (§4i) — offset-only far-pointer arithmetic
On 8086 compact/large, a far pointer's segment is FIXED per object (objects ≤ 64 KB)
and arithmetic stays within the segment.  The correct lowering of `far_ptr ± idx`
is **add/sub `idx` to the 16-bit OFFSET only, with 16-bit wraparound, segment
preserved** — NOT a flat 32-bit add/sub.  That is correct for BOTH cases:
- gc_alloc: `off(small) + start_block*16` (< 0x10000 within the segment) → right
  offset, segment kept.
- wrapped-negative: `off + 0xFFFF` wraps to `off-1`, segment kept.
Recommended implementation: a dedicated backend op (e.g. `Oaddfo`/`Osubfo`, "far
offset add/sub") that emits `add word <ptr-low>, idx16` with NO `adc`/`sbb` into
the segment word — both CORRECT and SMALLER than today's `add ax,lo / adc dx,hi`.
minic emits it for `far_ptr ± index` under compact/large (the `Scale:` else
branch + the postinc/preinc far paths).  This is meaty: new op → `ops.h` + `all.h`
+ the IL-lexer perfect-hash regen (`tools/lexh.c`, the `K` constant) + `i8086/emit.c`
handlers + minic emission, then `make check` + full `tools/test-dos.sh` + Victor
re-verify (scale2 must reach `120 7980` / `DONE`; feature-probe 23/23; fill probe
→ DONE).  Do NOT rush it at end-of-session — that's exactly why §4h reverted
rather than shipped.  A pure-IR alternative (`(ptr & 0xFFFF0000) | ((ptr_lo+idx)
& 0xFFFF)`) is correct but bloats every variable-index far-arith site (~5 insns);
the image is already near the ~824 KB ceiling, so prefer the compact backend op.
NOTE the **only-variable-index** scope: constant-index far arith goes through the
`r->t == Con` path (`r->u.n *= sz`; a large *constant* index ≥ 0x8000 could carry
into the segment too — handle if a real case appears, but it's rare).

### Verify the fix with the committed repro
`QBE_FAR_STATIC_DATA=1 tools/build-example.sh --model=compact minic/dos/examples/gc_bigheap_probe.c`
then `tools/run-dos-exe.sh build/examples/gc_bigheap_probe/gc_bigheap_probe.exe`.
BUGGY (today): `b>=2048` lines show wrong/zero `direct` + `FAIL`.  FIXED: every
`direct=<0x41+i>` + `ALL OK`.  Then GATE it (compact/large/huge) — but build-example.sh
must pass `-DFAR_DATA` first (see below), or keep the probe's self-`#define FAR_DATA`.

### Incidental harness gap found (fix alongside §4i)
`tools/build-example.sh` does NOT pass `-DFAR_DATA` to its cpp step (only
`build-micropython.sh` does), so a probe built compact/large/huge gets 16-bit
`uintptr_t`/`intptr_t` (stdint.h `#else` branch) instead of the 32-bit a far
pointer needs.  `gc_bigheap_probe.c` self-`#define`s `FAR_DATA` to work around it.
The clean fix: build-example.sh should add `-DFAR_DATA=1 -DDOS_FAR_DATA=1` for
compact/large/huge — but VERIFY it doesn't shift existing far-data probe goldens
(farglobal/fardata/farstruct_ptr reference these macros; stdint_probe is
medium-only and asserts `sizeof==2`, so leave medium alone).

### Build/run cheat-sheet (so next session doesn't re-derive it)
- Victor harness GOTCHA (cost a run this session): do NOT pipe `run-victor-sasi.sh`
  through `tr`/`tail`/`head` — the watchdog subshell inherits the pipe fd and
  blocks ~WALL_SECS.  Redirect to a file, then filter: `... > /tmp/x.out 2>&1` then
  `LC_ALL=C tr -cd '\11\12\15\40-\176' < /tmp/x.out`.  `-nothrottle` makes a 300
  emulated-sec run finish in a couple wall minutes.  (See [[feedback-victor-harness-pipe-buffer]].)
- Scratch repros (untracked `build/*.py`): `mp-fill-probe.py` (forced fill → `gGDE`),
  `mp-churn-loc80.py` (per-iter localization → dies iter ~12), `mp-churn-scale2.py`
  (the canonical 20→120 churn).

---

# Next session (§4g — ROOT-CAUSE FIX in the COMPILER: minic now far-data NATURAL-ALIGNS 4-byte struct members so MicroPython's sizeof(void*)-strided conservative GC works as-designed; §4f scanner workarounds REVERTED; verified on real Victor)

## 2026-06-08 §4g notes (the §4f workaround replaced by the real fix; user-chosen direction)

**§4g fixes the §4d/§4e/§4f churn GC corruption at its ROOT, in minic, and reverts
the §4f scanner workarounds.**  §4f had adapted MicroPython's conservative collector
to minic's packed struct ABI (2-byte scan stride + a dual-aligned mp_state rescan);
§4g instead makes minic emit a pointer-aligned ABI under far-data, so the *unmodified*
upstream collector works.  Committed to master as `d389d63` (compiler change, green-gate
milestone); the workaround revert lives in EXTERNAL `~/projects/micropython` (not the
qbe repo), same as the §4f fixes did.

### The fix (minic.y, `d389d63`)
Lay struct members out with **natural alignment under far-data** (NEAR_DATA stays PACKED
→ tiny/small/medium byte-identical, whole medium gate untouched):
- `structh[].align` = max member alignment; new `alignof_ctyp()` returns **1 under
  NEAR_DATA**, else **4** for a 4-byte member (long / far data ptr / float / 4-byte
  fn-ptr), the aggregate's own align for a struct/union member, and **1** for sub-4-byte
  scalars (char/short/int/near-ptr — they can't hold a pointer, so their alignment is
  irrelevant to the collector; this keeps padding, hence image growth, minimal).
- pre-pad `size` to the member's alignment + bump struct align in
  `structaddmember`/`structaddarrmember`/`structaddbitfield`.
- `structfinish()` tail-pads a struct to its own alignment (idempotent); called at all
  **9** struct-close grammar sites + `emit_struct_global_array`.
- `hoistanonymous()` aligns the anonymous body's base offset + propagates its align.
- The static-initializer machinery already gap-fills (`agg_zfill(m->offset - cursor)`)
  and tail-fills (`if (cursor < structsize)`), so it followed the aligned offsets with
  no change; `offsetof`/`emit_clit_aggr`/member access read `m->offset` directly.

**Minimal blast radius:** a struct with NO 4-byte member has align 1 → byte-identical
even under far-data.  The MicroPython compact image grew only **+224 B** vs the §4f
baseline (843424→843648, body 820160→820400; the alignment padding is +304 and the
reverted-workaround code is −80) — well under the ~824416 "Program too big" point.

### Why this works (the §4f bug, now at its source)
Upstream `gc_collect_start` scans the mp_state roots with
`gc_collect_root(ptrs + root_start/sizeof(void*), …)`, root_start =
`offsetof(mp_state_ctx_t, thread.dict_locals)`.  PACKED that was 6 → void**-arith rounds
to byte 4 → the whole root scan was 2 bytes out of phase (the §4f bug).  ALIGNED,
`dict_locals` (a pointer) sits at a 4-aligned offset, so `root_start/4*4 == root_start`
exactly and every root pointer is at a stride boundary → all found.  Likewise
`gc_mark_subtree`'s `sizeof(void*)` stride now lands on every heap-object child
(qstr_pool_t hashes/lengths/qstrs[] are 4-aligned).  So both §4f workarounds are
redundant.

### §4f workarounds REVERTED (external `~/projects/micropython`)
- `py/gc.c` → **`git checkout`** (full revert to upstream): the `MICROPY_GC_SCAN_PTR_STRIDE`
  macro and the byte-offset `gc_mark_subtree` loop are gone; stride is `sizeof(void*)` again.
- `ports/dos8086/main.c` `gc_collect()` → the mp_state dual-aligned **rescan block removed**
  (upstream gc_collect_start now scans it correctly).
- **KEPT** the pre-existing C-stack dual-aligned scan in `gc_collect()` — backend FRAME
  SLOTS are still only 2-aligned (§4g aligns struct member offsets, not stack slots), so a
  far pointer in a stack slot can still sit at a 2-mod-4 frame offset.  Unrelated to §4f.

### Verified on real Victor (`tools/run-victor-sasi.sh`, compact far-data, stackless, workarounds reverted)
| test | heap | result |
|---|---|---|
| `build/mp-churn-lit.py` (minimal corrupting repro) | 16 KB | `R 124750` ✓ clean `D4`/`C5` — **DECISIVE** |
| `build/mp-feature-probe.py` (23 std-surface checks) | 49 KB | **23/23 OK** ✓ — no regression |
| `build/mp-churn-scale2.py` (churn 20→120) | 16 KB | `20 330`…`120 7980`,`DONE` ✓ |
| `build/mp-churn-scale2.py` (churn 20→120) | 49 KB | stalls at churn(80) — see below |

Gates (qbe repo): `make check` green; `tools/test-dos.sh` **222/222** (3 new
`struct_align_probe` entries compact/large/huge; `scalar_array_probe` reworked from
hardcoded packed offsets to model-independent offsetof relationships — it was the ONLY
gated probe that asserted a packed layout, the entire blast radius).

### The 49 KB-heap extreme-churn STALL is the UNCHANGED §4f perf cliff (NOT a regression)
scale2 on the 49 KB heap stops after `60 2190` (values all correct) at churn(80) — and
does so identically at RUN_SECS 300 and 600.  This is exactly the §4f "perf cliff":
extreme churn on the big (3072-block) heap → `gc_block_stack` overflow
(`MICROPY_ALLOC_GC_STACK_SIZE`=64) → repeated O(blocks) full-heap rescans in
`gc_deal_with_stack_overflow`.  §4f's table only ever verified scale2 completing on the
**16 KB** heap and explicitly noted it "does NOT finish in 700 emulated seconds" on 49 KB
— so §4g matches §4f exactly here.  Correctness is unaffected (scale2 completes the full
20→120 on 16 KB; the values printed on 49 KB are correct, it just doesn't finish).
Reverting the §4f stride-2 amplifier should make the scan *faster*, but the fundamental
cause (heap size + stack-overflow rescans) dominates and is untouched by alignment.

### Known follow-up (unchanged from §4f — a perf cliff, correctness-unaffected)
If heavy-GC programs on the big heap need to finish: raise `MICROPY_ALLOC_GC_STACK_SIZE`
(cheap external port-config change, needs its own Victor check) to cut the O(blocks)
rescans.  Otherwise the integer-feature surface, deep recursion (stackless), and moderate
GC pressure are all verified-good on Victor.  No qbe/minic/i8086 source remains to change
for this; pick whichever frontier a real consumer needs next.

---

# (ARCHIVED) §4f — churn GC corruption FIXED via SCANNER WORKAROUND (now superseded by §4g's compiler fix and reverted)

# Next session (§4f — churn GC corruption FIXED: minic packed-struct 4-byte far pointers at 2-mod-4 offsets defeat MicroPython's sizeof(void*)-strided conservative GC scans; dual-aligned root re-scan + 2-byte gc_mark_subtree stride)

## 2026-06-08 §4f notes (§4d/§4e CLOSED — root cause found and fixed; verified on real Victor)

**The §4e "scan misalignment RULED OUT" was a FALSE NEGATIVE.**  The misalignment
WAS the bug — in TWO scan sites — and §4e's single, partial `+2` patch to
`gc_collect_start` fixed neither completely, which is why it "still corrupted."
Lesson: a botched experiment that *appears* to refute a hypothesis is not
evidence the hypothesis is wrong.

### Root cause (SSA-confirmed)
minic lays out struct members **packed**, with NO natural-alignment padding
(`minic.y` `structaddmember`: `m->offset = size; size += SIZE(ctyp)`).  Under
far-data a far pointer is **4 bytes** but `size_t` is **2 bytes**, so a 4-byte
pointer that follows a `size_t`/`uint16_t` lands at a **2-mod-4 byte offset**
(e.g. `mp_state_thread_t.dict_globals`@10, `qstr_pool_t.qstrs[]`@18).
MicroPython's conservative GC scans stride by `sizeof(void *)` = 4, so each such
pointer is **split across two read words and never recognised** → the block it
roots is freed while live → use-after-free → garbage output + hang.  This is
**far-data-specific**: in the medium model everything is 2-byte and 2-aligned,
so pointers are always at `sizeof(void*)` multiples and the bug cannot occur
(exactly as §4e point 4 predicted — a medium-model probe would NOT reproduce it).

The generated `gc_collect_start` SSA proved it: `root_start =
offsetof(mp_state_ctx_t, thread.dict_locals)` is **6**, but
`gc_collect_root(ptrs + root_start / sizeof(void *), …)` does `void**`-pointer
arithmetic that rounds the start DOWN to byte offset **4** (`6/4*4`), so the
*entire* root scan is 2 bytes out of phase and recognises none of the 2-mod-4
root pointers (`dict_globals`, the embedded `dict_main`, `last_pool`, …).

### The fix — two sites, same cause (both in EXTERNAL `~/projects/micropython`)
1. **`ports/dos8086/main.c` `gc_collect()`** — re-scan the `mp_state_ctx` root
   section (offsets `thread.dict_locals` … `vm.qstr_last_chunk`) **byte-accurate
   and at BOTH even alignments** (base and base+2), mirroring the dual-aligned
   C-stack scan already there.  `gc_collect_root` only marks unmarked heads, so
   this is purely additive on top of `gc_collect_start`'s (broken, ~no-op) scan.
   → cleared the **hang/heap corruption** (churn-lit reached clean `D4`/`C5`).
2. **`py/gc.c` `gc_mark_subtree()`** — the subtree (heap-object child) scan had
   the *same* flaw: it strides `sizeof(void*)` from each block start, missing a
   child pointer at a 2-mod-4 block offset.  After fix 1 the hang was gone but
   print still emitted garbage (`\x01`) because `qstr_pool_t`'s
   `hashes`@10/`lengths`@14/`qstrs[]`@18 (the interned-string pointers behind
   `"R"`/`"DONE"`) were skipped → the string chunk was freed/reused.  Changed the
   child scan to step by **`MICROPY_GC_SCAN_PTR_STRIDE`** (= **2** under
   `FAR_DATA`, else `sizeof(void*)` — byte-identical on aligned targets).
   → cleared the print garbage.

NOT a minic/qbe bug: minic's packed layout is intentional and relied on
throughout the tree.  The bug is MicroPython's conservative scanner assuming a
pointer-aligned ABI; the fix adapts the scanner, exactly like the pre-existing
dual-aligned C-stack scan.

### Verified on real Victor (`tools/run-victor-sasi.sh`, compact far-data, stackless)
| test | heap | result |
|---|---|---|
| `build/mp-churn-lit.py` (minimal corrupting repro) | 16 KB | `R 124750` ✓ clean `D4`/`C5` |
| `build/mp-churn-disc.py` churn(80) (dict+comprehension+str) | 16 KB | `A 3720` ✓ |
| `build/mp-churn-scale2.py` (full bisection, churn 20→120) | 16 KB | `20 330`…`120 7980`,`DONE` ✓ |
| `build/mp-feature-probe.py` (23 checks, std surface) | 49 KB | **23/23 OK** ✓ (matches §4c) |

Force a collection cheaply with `MP_HEAP_SIZE=16384 tools/recompile-mp-tu.sh
main …`.  Committed image is back to the proper 49 KB heap: **843424 / body
820160** (vs §4c 843344 / 820096 — +80 B for the two fixes; still under the
~824416 "Program too big" point).  No qbe/minic/i8086 source changed, so
`make check` and `tools/test-dos.sh` are unaffected (definitionally green).

### Known follow-up (NOT corruption — a perf cliff)
`scale2`'s **420-iteration extreme churn on the 49 KB heap** does NOT finish in
700 emulated seconds (stalls mid-run), whereas the identical workload completes
on a 16 KB heap and `feature-probe`'s moderate GC pressure is fast on 49 KB.
The 2-byte stride does ~2× the candidate reads and a 49 KB heap (3072 blocks)
collection is far slower; the likely amplifier is `gc_mark_subtree` finding more
(incl. false-positive) pointers → `gc_block_stack` overflow
(`MICROPY_ALLOC_GC_STACK_SIZE` = 64) → repeated O(blocks) full-heap rescans in
`gc_deal_with_stack_overflow`.  Correctness is unaffected.  If heavy-GC programs
on the big heap need to be fast, candidate mitigations (each needs its own
Victor check): raise `MICROPY_ALLOC_GC_STACK_SIZE`; or have minic 4-byte-align
far-pointer struct members so the `sizeof(void*)`-strided scans suffice (large,
risky ABI change — the packed layout is relied on elsewhere).

---

# (ARCHIVED) §4e — churn GC corruption NARROWED to "a live heap object across a GC collection is freed"; NOT a scan-misalignment; collection completes; needs marking-completeness instrumentation

## 2026-06-08 §4e notes (deep diagnostic pass on the §4d churn GC-pressure corruption — characterized, NOT yet fixed)
**No tracked source changed.** All work was external-MicroPython instrumentation (since
reverted) + scratch `build/mp-churn-*.py` repros + reading generated `build/mp-link/*.ssa`
/ `*.asm`.  `make check` green; the MP image was rebuilt to the **byte-identical** committed
baseline (843344 / body 820096).  `tools/test-dos.sh` unaffected (no qbe/minic/i8086 change).
Full working notes in `/tmp/churn-investigation-notes.md` (not tracked).

### What the bug IS (much sharper than §4d's "GC pressure corruption")
- **A heap object that must stay LIVE across a GC collection is freed**, corrupting the heap
  → garbage output + hang.  The collection itself **runs to completion** (all phases).
- **Trigger = (heap forced to collect) AND (≥1 heap object live across that collection).**
  With NOTHING live across the collection it is harmless.
- **NOT cross-call specific, NOT comprehension/dict/str specific** — reproduces with a single
  flat loop of plain list literals.  The §4d "churn(60) ok / churn(80) fail" boundary was just
  "when does the 48 KB heap first fill enough to force a collection"; standalone churn(80)
  passes ONLY because it never collects (its garbage < 48 KB).

### The Victor experiments that pinned it (all real-Victor, 16 KB heap to force a collection early)
- `build/mp-churn-disc.py` standalone `churn(80)` on the **48 KB** heap → PASSES (never collects).
- Shrink heap to **16 KB** (recompile-mp-tu.sh MP_HEAP_SIZE knob), same `churn(80)` → HANGS.
  => forcing one collection is the trigger; a single churn call suffices.
- Port `gc_collect` instrumented with raw phase markers `[g s 1 2 e]` (no heap alloc):
  output is **`[gs12e]` then hang** → gc_collect COMPLETES (start, both root scans, end),
  returns, VM resumes, then corrupts.  NOT a GC-internal infinite loop.
- `build/mp-churn-dead.py` (`[0,0,0,0,0,0,0,0]` as a bare auto-printed expr — nothing live) →
  **SURVIVES 2+ collections, keeps printing cleanly.**  => corruption requires a LIVE object.
- `build/mp-churn-lit.py` (`b=[...]; x=x+b[7]+i` at module scope — `b` is a live global) →
  **`[gs12e]` then garbage `Y[ZXPRS` + hang.**  The minimal corrupting repro.

### What is RULED OUT (verified in generated SSA + i8086 asm, free of Victor runs)
- **Scan misalignment of the mp_state root section** — hypothesized the single-4-byte-stride
  `gc_collect_start` scan skips root pointers shifted to odd-2-byte offsets by 2-byte `size_t`
  fields inside embedded `mp_obj_dict_t`/`mp_obj_exception_t` (the port's C-stack scan already
  scans BOTH 2-byte alignments for this reason; `gc_collect_start` does not).  **TESTED a
  +2-alignment scan of the mp_state section → churn-lit STILL corrupts (`[gs12e]ZXPRS`).**
  REFUTED and reverted.  With BOTH scans dual-2-byte-aligned, the live object is still lost.
- **All GC bit/pointer codegen is correct:** `PTR_FROM_BLOCK` (block*16 in 16-bit, safe ≤4095
  blocks), `BLOCK_FROM_PTR` (32-bit far-sub, same-segment), `VERIFY_PTR` (signed compares but
  true heap ptrs share the heap segment so never wrongly rejected), `ATB_HEAD_TO_MARK`/
  `MARK_TO_HEAD`/`ANY_TO_FREE` shifts (`mov ax,<k>; shl ax,cl` — §2k holds), the
  `gc_sweep_free_blocks` free_tail state machine, `gc_deal_with_stack_overflow` (terminates),
  `mp_state_mem_area_t` field offsets (table_start@0, byte_len@4, pool_start@6, pool_end@10,
  last_free@14, last_used@16 — self-consistent), and `gc_get_ptr`.  Heap is a single
  segment (`main_BSS:0x1200..0xD200`, 16-aligned, no 64 KB wrap).
- So it is **NOT a missed root from scan alignment, NOT a GC-internal loop, NOT the obvious
  block-math/ATB codegen.**  The live object is reachable on paper (e.g. global `b` via
  `dict_main.map.table` → entry value, all at 4-byte-aligned offsets that a 4-byte-stride
  trace covers) yet is freed.

### THE GOAL FOR NEXT SESSION — instrument MARKING COMPLETENESS directly
The contradiction ("reachable on paper but freed") means a marking/trace step is silently
incomplete for a live object, in a way not explained by scan alignment.  Stop reasoning;
**measure which live block is swept.**  Concrete plan:
1. In the port `gc_collect` (or a patched `py/gc.c`), capture the address of the known-live
   object before the collection (e.g. expose `MP_STATE_VM(dict_main).map.table` and a known
   global's value pointer) and, after `gc_collect_end`, check whether that block's ATB kind is
   FREE (i.e. it was swept while live).  Print a raw marker if so.  This directly confirms
   freed-while-live and identifies WHICH object (the table array? the list? an intermediate?).
2. Alternatively instrument `gc_sweep_free_blocks` to raw-print each freed block's address, and
   `gc_mark`/`gc_mark_subtree` to print marked-block addresses + total marked count, for the
   ONE collection in `build/mp-churn-lit.py` on the 16 KB heap.  Compare marked-set to the
   expected live set; a live block absent from the marked set is the smoking gun.
3. Suspect list (given codegen is clean): (a) `gc_mark_subtree` n_blocks undercount for a
   MULTI-block live object so its tail words (holding child pointers) aren't traced — re-derive
   on-target, not just from the SSA; (b) a far-pointer VALUE inside a live container read by the
   trace as a non-block-aligned/garbage value (check the actual `loadfl` offsets vs entry
   layout on-target); (c) the conservative C-stack scan range `[&stack_dummy, stack_top)`
   genuinely not covering the slot holding the live root at the collection point (verify
   `nbytes` and that the live `code_state`/value-stack slot is within range on-target —
   churn-dead's in-progress list survives, so module-frame value-stack rooting works for the
   transient case, but a RETAINED global may sit only in `dict_main`).
4. FAST-LOOP idea: try to reproduce in a **medium-model DOS probe** that links `py/gc.c` with
   minimal `mp_state` stubs, allocates cross-linked objects, drops most, forces `gc_collect`,
   reallocates, and verifies a retained object's contents.  If it reproduces in medium model
   (DOSBox, seconds/iteration), the 12-min Victor loop is no longer the bottleneck.  If it
   does NOT reproduce in medium, the bug is **far-data-specific** (compact-model far heap),
   which itself narrows it to the far load/store paths in the collection.
- Repro scripts live in `build/mp-churn-{disc,lit,dead,loc,bisect}.py` (untracked).  Use
  `MP_HEAP_SIZE=16384 tools/recompile-mp-tu.sh main ~/projects/micropython/ports/dos8086/main.c`
  to force a collection in a single small loop (fast relink), then
  `VICTOR_SRC=build/mp-churn-lit.py tools/run-victor-sasi.sh build/mp-link/mpython.exe 220`.
  Restore the baseline image afterwards with `tools/build-micropython.sh --model=compact`
  (843344 bytes).  HARNESS GOTCHA still applies: redirect to a file + poll, never pipe through
  `tail`/`head` (see [[feedback-victor-harness-pipe-buffer]]).

# Next session (§4d — TRY THIS: reduce the pre-existing churn(~80) GC-pressure corruption — the real compiler-bug candidate)

## 2026-06-08 §4c notes (§4b DONE: stackless-strict is the dos8086 port default — clean win, no compiler bug)
- **§4b landed.**  `MICROPY_STACKLESS (1)` + `MICROPY_STACKLESS_STRICT (1)` are
  now the dos8086 port default (external `~/projects/micropython/ports/dos8086/
  mpconfigport.h`), with `mp_raise_recursion_depth()` provided as a real port
  symbol in `ports/dos8086/main.c` (py/runtime.c only defines it under
  MICROPY_STACK_CHECK, which we keep off).  The qbe-repo artifact is the harness
  default **`MP_STACK_SIZE` 24576 → 16384** in `tools/build-micropython.sh`
  (committed).  Build: 107/107 TUs, **image 843344 / body 820096** (under the
  ~824416 "Program too big" point; loads with margin).  `make check` green;
  `tools/test-dos.sh` was **219/219 ok** at session start and is unchanged (NO
  minic/qbe/i8086/runtime/probe source changed this session — the only qbe edit
  is the shell-script stack default + this doc).
- **Why 16384, not the plan's 8192:** 8192 corrupts.  Deep PLAIN recursion is
  now heap-framed (stackless), so the C stack stays shallow — but generator
  RESUME still C-recurses (`mp_execute_bytecode`, objgenerator.c:210; STACKLESS
  does NOT cover generator resume), so deep generator nesting overflows the C
  stack into DGROUP data.  At 8192 that corruption is catastrophic (garbage
  output + `Divide overflow` INT 0); 16384 degrades it gracefully (wrong value,
  clean exit).  16384 is the largest stack that still fits the load ceiling
  (body 820096 < ~824416; 24576 → body 828224 → won't load).
- **On-Victor verification (real Victor via `tools/run-victor-sasi.sh`):**
  - `build/mp-recsum-probe.py` → `recsum(6/12/20/30)` = 21/78/210/465, clean
    `D4`/`C5`.  **The documented HARD frontier (recursive image corrupted at
    recsum(20) with `DE`+`(nil)`) is GONE.**
  - `build/mp-frontier2.py` → reaches **`OK recsum`** (the old recsum(30) wall),
    then hits the pre-existing churn(80) frontier (see §4d below).
  - `build/mp-feature-probe.py` → ALL 23 checks OK (mul…enum), clean `D4`/`C5`.
- **Stackless is strictly ≥ the committed recursive image on every axis** (all
  measured on real Victor this session):
  | workload | recursive 24 KB (was committed) | stackless 16 KB (now) |
  |---|---|---|
  | deep plain recursion recsum(30) | ✗ corrupt (`DE`+`(nil)` @ 20) | ✓ clean 465 |
  | deep generator recursion `sum(gc(15))` | ✗ **machine REBOOT** | ~ wrong 99, clean exit |
  | GC pressure churn(80) | ✗ corrupt (NameError) | ✗ corrupt (hang) — TIE, pre-existing |
- **No compiler bug surfaced** — the §4b "stress for a codegen bug" prize did NOT
  materialise (the honest-caveat outcome).  The deep-generator-recursion limit is
  target-fundamental (finite DOS C stack vs. generator C-recursion, no fit-able
  MICROPY_STACK_CHECK), not a minic/qbe/i8086 bug and not a stackless regression.
- **Probes written this session (untracked `build/*.py` scratch):**
  `mp-stackless-stress.py` (mutual/raise-catch/generator/GC recursion),
  `mp-gen-probe.py` (generator-recursion bisection), `mp-churn-scale2.py`
  (churn 20→120 GC-pressure scale).

## §4d — THE GOAL FOR NEXT SESSION: reduce the churn(~80) GC-pressure corruption
**`churn(n)` corrupts between n=60 (ok) and n=80 (fails) on BOTH the stackless
and the recursive images** — a pre-existing, VM-mode-independent bug, and the
most promising remaining compiler/runtime-bug candidate.  `churn` is a FLAT loop
(no recursion), so it is NOT a C-stack issue — it is **GC pressure**: each
iteration allocates `[i+j for j in range(8)]` (a list + a comprehension
frame) + `{str(i):row,"last":row[-1]}` (a dict + a str).  At ~churn(80) a live
object is lost: symptoms are nondeterministic (`NameError: local variable
referenced before assignment` in `<listcomp>`, `TypeError: object isn't
subscriptable` on `table["last"]`, or a hang), all consistent with a GC
root-scan miss or heap corruption under pressure.
- **Repro:** `VICTOR_SRC=build/mp-churn-scale2.py tools/run-victor-sasi.sh
  build/mp-link/mpython.exe 240` → prints `20 330`, `40 1060`, `60 2190`, then
  fails at 80.
- **Likely loci** (reduce to a `minic/dos/examples/*_probe.c` FIRST, per the
  discipline): the conservative C-stack root scan in `ports/dos8086/main.c`
  `gc_collect()` (does it miss a live far pointer at some alignment under deep
  allocation?), gc.c block/ATB math under near-full heap, or a codegen bug in
  the list-comprehension / dict-store path that only bites once the heap is
  churned.  Instrument MicroPython gc at the C level (mark/sweep of the listcomp
  frame + the per-iteration dict) for `churn(80)` to find which object is freed
  while live, then reduce that shape to a DOS probe and fix QBE/minic/runtime.
- **HARNESS GOTCHA (cost me a wasted run this session — see
  [[feedback-victor-harness-pipe-buffer]]):** do NOT pipe `run-victor-sasi.sh`
  through `tail`/`head`.  Its watchdog subshell `( sleep WALL_SECS; kill )&`
  inherits the pipe write-fd (~1080 s for a 240 s run), so `tail` blocks for an
  EOF that never comes and the run looks empty/hung.  Redirect straight to a
  file (`... > /tmp/run.out 2>&1`), background it, and poll the file.  macOS has
  **no `setsid`**.

# (ARCHIVED) §4b plan — land stackless-strict as the port default

## THE GOAL FOR NEXT SESSION
**Enable `MICROPY_STACKLESS=1` + `MICROPY_STACKLESS_STRICT=1` as the dos8086 port
default, rebuild compact far-data, and re-verify on Victor — then stress the
recursion paths to see if the different VM code paths shake a compiler/backend
bug loose.**  This is the most concrete remaining frontier with a known payoff:
it eliminates the one documented HARD frontier (deep Python recursion) while
staying under the Victor image ceiling.

### Why this is the pick
- The committed port today uses MicroPython's **recursive** VM: each Python call
  is a C-level recursive call into `mp_execute_bytecode`, so deep recursion
  burns the hard-capped DOS stack.  `build/mp-recsum-probe.py` reaches
  `recsum(12)` but fails by `recsum(20)` with an **uncaught** `DE` + `(nil)`
  (corruption, not a clean exception).  `build/mp-frontier2.py` dies at the
  `recsum(30)` case.
- You **cannot fix it by growing the stack**: 28 KiB → body 824416 → Victor
  "Program too big to fit in memory"; 32 KiB → won't link (DGROUP+stack > 64 KB).
  And `MICROPY_STACK_CHECK=1` is both too big AND consumes more transient C stack
  per frame, so it trips during *shallow* recursion (see the 2026-06-07 Codex
  stack-check experiment notes below).  Both are dead ends.
- **Stackless-strict is already proven to work via build knobs** (2026-06-07
  Codex notes, lines ~441-445): with `MP_STACK_SIZE=8192` it links at total
  **835088 (well under ceiling)** and `build/mp-recsum-probe.py` completes
  `recsum(6/12/20/30)` with clean `D4`/`C5`; `mp-test.py`/`mp-feature-probe.py`/
  `mp-frontier.py` all still pass; `mp-frontier2.py` reaches `OK recsum`.  The
  deep-recursion frontier disappears.

### Concrete steps (promote experiment → committed port default)
1. **External MicroPython checkout** (`~/projects/micropython`, NOT this repo):
   - `ports/dos8086/mpconfigport.h`: add `#define MICROPY_STACKLESS (1)` and
     `#define MICROPY_STACKLESS_STRICT (1)` (both default to `0` in
     `py/mpconfig.h:386,393`; the port does not currently override them).
   - Provide `mp_raise_recursion_depth` **properly** as a real port source symbol
     (e.g. in `ports/dos8086/main.c` or a small port .c), NOT via the generated
     `runtime.pp.c` sed-patch.  The existing build knob
     `MP_DOS_STACKLESS_RECURSION_RAISE=1` (tools/build-micropython.sh:67,136)
     proves the one-liner body; just make it a committed symbol so the build is
     reproducible without the env knob.
   - Set the DOS port stack to the value that fit: `MP_STACK_SIZE=8192` worked
     (vs the current 24576 default).  Decide whether to bake 8192 into the
     harness default or keep it an env override — but RECORD the chosen value.
2. **Build:** `tools/build-micropython.sh --model=compact` (with the stackless
   config above).  Confirm 106/107 TUs → objects and a clean link.  **MEASURE
   the image** and compare to the 844256 NONE baseline / the 835088 stackless
   experiment number.
3. **Re-verify on Victor** (`VICTOR_SRC=... tools/run-victor-sasi.sh
   build/mp-link/mpython.exe 240`), in this order:
   - `build/mp-recsum-probe.py` — must reach `recsum(30)` + clean `D4`/`C5`
     (the whole point).
   - `build/mp-test.py`, `build/mp-feature-probe.py`, `build/mp-frontier.py`,
     `build/mp-frontier2.py`, `build/mp-frontier3.py` (NEW this session — see
     §4a-followup below) — full feature surface must still pass.
4. **Stress the new VM paths for a compiler bug** (the REAL prize): stackless
   uses heap frame-chaining + a different nlr/exception interaction.  Push
   deep+wide recursion, mutual recursion, recursion-through-generators,
   recursion-raising-and-catching-exceptions, and recursion under GC pressure.
   If anything mis-behaves, **reduce it to a `minic/dos/examples/*_probe.c`
   FIRST**, fix the QBE/minic/i8086/runtime bug, then gate it in
   `tools/test-dos.sh` — same discipline as every prior §.

### The honest caveat (decide if it's worth it)
This is a **MicroPython port-config improvement, NOT inherently a compiler
change.**  It makes the *port* more capable (clean deep recursion within the
ceiling).  Its value as a *compiler exercise* is indirect: the different VM code
paths MIGHT flush out a latent minic/codegen bug (that reduction would be the
real win), or it might just work — in which case you've improved the port, not
the compiler.  If the session goal is strictly "find compiler bugs," a fresh
untested feature surface may be a better net than port tuning.  But stackless is
the one frontier with a mapped path AND a known payoff, so it's the default pick
unless the user redirects.

## §4a-followup (2026-06-08): frontier3 sweep — CLEAN, no compiler bug
- Re-verified baseline gates green BEFORE any work: `make check` ✅,
  `tools/test-dos.sh` **219/219 ok**.  No tracked changes made this session.
- Wrote `build/mp-frontier3.py` (untracked scratch, alongside the other
  `build/mp-*.py`) to push past the fixed `str(int)` frontier on real Victor.
  **Every minimal-ROM-supported feature passed**, including the codegen-sensitive
  cross-word 32-bit (DX:AX) integer arithmetic that's most likely to expose an
  i8086 bug:
  - int: `100000*5`, `1<<20`, `divmod(100000,7)`, `-7//2`, `-7%2`, big XOR,
    `~0`, `1000000>>3`, `7**6` — all correct.
  - dict: `update`/`get`/`get(default)`/`keys`/`values`.
  - list: `insert`/`extend`/`pop`/`index`.
  - `zip`, `map`, `sorted(key=lambda)`, nested `repr` (list-of-dicts-of-lists),
    `str.format` (positional + reordered), and a 200-iteration GC churn loop
    (list+dict+str per pass, some retained as live roots) → correct checksum,
    clean collection.  `DONE` → `D4` → `C5`.
- The ONLY "failures" were `filter` / `reversed` raising `NameError` — these are
  **deliberate config omissions** (`MICROPY_PY_BUILTINS_FILTER`/`_REVERSED`
  require `AT_LEAST_CORE_FEATURES`; the port is `MINIMUM` ROM level,
  `py/mpconfig.h:1531,1536`).  Same category as the documented `str.count` /
  `%`-format gaps — NOT a compiler bug.
- Net: the port's integer-feature surface is robust wherever the minimal config
  enables it.  This frontier found nothing to fix — hence §4b redirects to the
  stackless-strict recursion direction, which has a mapped path and a real
  payoff.

# Next session (§4a — float flip: all per-TU gaps cleared; FLOAT LINKS but overflows Victor ceiling)

## 2026-06-08 §4a notes (MICROPY_FLOAT_IMPL_FLOAT now LINKS; size wall is the blocker)
- **Goal: clear the 4 remaining per-TU gaps from §3z and actually flip
  `MICROPY_FLOAT_IMPL` → FLOAT.**  All 4 gaps cleared; the flip now produces a
  **clean link (107/107 TUs, compact far-data)**.  BUT the float image is too
  big for the Victor load ceiling, so the flip is REVERTED to NONE (the §3z
  discipline).  `make check` green; `tools/test-dos.sh` **219/219 ok**; the
  NONE image is **844256 — byte-identical** to §3z (all groundwork is
  gc-stripped under NONE).
- **The 4 gaps were NOT 4 distinct compiler bugs — they collapsed to 3 root
  causes, only ONE of which touched compiler-adjacent code:**
  1. **objfloat / objtype / modbuiltins → ONE qstr gap.**  All three referenced
     `MP_QSTR_float` / `MP_QSTR___float__`, absent from the pre-generated
     `ports/minimal/build/genhdr/qstrdefs.generated.h` (built integer-only).
     modbuiltins's "non-constant in case label" was a LAGGED line number —
     instrumenting `const_eval`'s die printed the real culprit `MP_QSTR_float`
     (an undefined identifier in the `mp_module_builtins_globals_table` rom-map
     entry, NOT a real `case`).  **Fix = append two QDEF0 (static-pool) lines**
     to the genhdr (hashes via the verified djb2 `hash*33^b & 0xFFFF` — matched
     known entries __dir__=36730/__call__=63911):
       `QDEF0(MP_QSTR_float,    17461, 5, "float")`
       `QDEF0(MP_QSTR___float__, 28725, 9, "__float__")`
     Safe because pool 0 is **unsorted** (linear search) and
     `MP_QSTRnumber_of_static` is **positional** (auto-counted) — both the enum
     (qstr.h) and the data arrays (qstr.c) scan QDEF0 in file order, so an
     appended line stays index-consistent.  Static pool now 185 (< 256, so
     bytecode short-qstr encoding is unaffected).
  2. **parsenum.c "undefined variable" → missing `INFINITY` macro (header
     gap, NOT the §3z-guessed float-local scope bug).**  `(mp_float_t)INFINITY`
     left `INFINITY` unexpanded — `minic/include/math.h` never defined it.
     **Fix:** new public `float sf_inff(void)` in `minic/dos/softfloat.c`
     (`sf_frombits(sf_inf(0))`) + `#define INFINITY/HUGE_VALF/HUGE_VAL
     (sf_inff())` and `#define NAN (sf_nan(""))` in math.h.  (Reduced the
     suspected scope shape first — it compiled clean — which pointed at the
     macro.)
  3. **binary.c "parse error" → `_Float16` (config decision, NOT a minic
     parse bug).**  host clang defines `__FLT16_MAX__`, so mpconfig.h
     auto-selected the native `_Float16` union path; minic/i8086 has no
     `_Float16` and MicroPython ships a portable `uint32_t`-bit fallback for
     exactly that.  **Fix = `#define MICROPY_FLOAT_USE_NATIVE_FLT16 (0)`** in
     the port config.
- **Probe:** extended `minic/dos/examples/softlibm_probe.c` (+golden, medium
  `--softfloat`) with the new `INFINITY`/`NAN`/`HUGE_VALF` macros
  (`inf_bits=7f800000`, `isinf(INFINITY)=1`, `signbit(-INFINITY)=1`,
  `huge_bits=7f800000`, `isnan(NAN)=1`).  This is the only compiler-surface
  artifact of the session (the qstr + FLT16 fixes are external/config).
- **THE WALL — why FLOAT is reverted:** the FLOAT compact far-data image is
  **908944 total / body 882944** (code 742882, far data 77904).  The Victor
  load ceiling is **footprint = body + heap + stack ≤ ~896 KB**; the prior
  data points: NONE body 821152 loads, a 28 KiB-stack body 824416 already
  reported "Program too big".  Float body 882944 is **~59 KB past a
  known-failing point** — and that 59 KB is intrinsic float CODE (objfloat +
  formatfloat + parsenum-float path + the exp2/log2/powf soft-libm + every
  `_sf_*` call the VM now emits).  Heap is BSS, so trimming it cannot shrink
  the body; `--gc-sections` already ran (stripped 201 segments, keeping only
  the reachable powf, not exp2f/log2f/expf/logf).  **Enabling float on Victor
  requires a code-size campaign first** (feature trim won't help — float IS the
  feature; the candidates are the §2-style i8086 backend size levers, or a
  larger-RAM target).  Did NOT run Victor — 882944 vs the recorded
  824416-fails point makes the result certain; no need to burn the long run.
- **To re-attempt the flip** (recorded in `ports/dos8086/mpconfigport.h`'s
  float comment too): flip the 3 mpconfigport defines (FLOAT + COMPLEX 0 +
  FLT16 0) and append the 2 QDEF0 genhdr lines above.  The minic/softfloat/
  math.h groundwork is all in-tree and inert under NONE.
- **DECISION (2026-06-08, user): do NOT pursue MICROPY_FLOAT_IMPL_FLOAT on
  Victor.**  The effort to recover ~60 KB of code is not worth the payback in
  this RAM-limited environment.  Soft-float stays a fully-gated MEDIUM-MODEL
  DOS capability (the `--softfloat` probes), and the float groundwork in tree
  (softfloat.c, math.h, double→single, static float init) is inert under
  MicroPython's `MICROPY_FLOAT_IMPL_NONE`.  The MicroPython port stays
  integer-only; the FLOAT path is *available* (recipe above) but not a target.
- **Next:** drive the MicroPython port on integer-feature frontiers again
  (slicing/strings/GC pressure/recursion) and reduce any new failure to a
  `minic/dos/examples/*_probe.c` before fixing, as always.  The float flip is
  closed as "won't-fit, not worth it".

# Next session (§3z — MicroPython float flip groundwork: double→single, static float init)

## 2026-06-08 §3z notes (toward MICROPY_FLOAT_IMPL_FLOAT: compiler gaps cleared; flip surfaces per-TU gaps)
- **Goal: flip `MICROPY_FLOAT_IMPL` → FLOAT** (the §3y next step).  §3y's
  soft-libm made the *math* LINK-complete; this session did the build wiring,
  flipped the flag, and cleared the COMPILER gaps the flip exposed.  The flip
  is NOT yet complete — it surfaces 5 further per-TU gaps (below), 2 of which
  are build-infra (qstr regen), not compiler bugs.  **Landed the compiler work
  as a green-gate milestone; the external `mpconfigport.h` flip was REVERTED to
  NONE to keep that checkout clean.**  `make check` green; `tools/test-dos.sh`
  **218→219 ok**.
- **Build wiring (verified inert under NONE):**
  - `tools/build-micropython.sh` always links `minic/dos/softfloat.c` (the
    `_sf_*` arithmetic + algebraic/transcendental libm).  Under
    `MICROPY_FLOAT_IMPL_NONE` `--gc-sections` strips it ENTIRELY → image
    **byte-identical** (844256, 0 `sf_` symbols in the map).
  - `build/mp-spike/stubinc/math.h` was an EMPTY stub that SHADOWED the real
    `minic/include/math.h` (stubinc is `-I`'d first); now it `#include`s the
    real header.  Inert under NONE.
- **`double` aliases to single-precision (Ks)** — the decision (FPU-less i8086,
  no 8087, no 64-bit int to build a soft-double; standard tiny-target
  convention).  `minic/minic.y`: `TDOUBLE` → `INT|FLOAT` (was `LNG|FLOAT`);
  every float literal — suffixed or not — types single; `irtyp`/`irtyp_ret`
  always return `'s'` for a float (backstop so no stray `Kd` reaches the
  backend).  The existing `exts`/`truncd` conversion sites are guarded on a
  float-precision *difference* which can no longer occur, so they go dead (no
  bogus conversion).  This unblocked **93 of 107 MP TUs** (obj.h's
  `mp_obj_get_float_to_d`/`_from_d` inline helpers, emitted into every TU, no
  longer carry a `Kd`).
- **Pre-existing `SIZE(float)`=2 bug FIXED** — the `SIZE` macro never checked
  `FLOAT`, so `float` (`INT|FLOAT`) sized as the 2-byte `int` (masked before
  because `double` was `LNG|FLOAT`→4).  Added `ISFLOAT(x) ? 4` early.  Without
  this, `sizeof(float)`==2 and float struct members overlapped (probe `pb` read
  the wrong 2 bytes).  float LOCALS were unaffected (backend Ks slots are 4B).
- **`Ostosi`/`Ostoui` with a `Kl` result** (`i8086/emit.c`) — float→`long`
  (the `mp_float_hash` `(mp_int_t)val` shape) hit the `i->cls == Kl` switch and
  died.  Excluded them from that switch so they reach the soft-float conversion
  handler, which now stores the full `_sf_to_int` DX:AX into the Kl slot (Kw
  result still takes the low word only).
- **Static float initializers** (`minic/minic.y`) — a file-scope `float g=1.5f;`
  or a const struct float member used to die "unsupported operation in constant
  expression" (integer-only `const_eval`).  New `const_eval_double()` (host
  double; handles literals/casts/`+-*/`/unary-minus, incl. the `0 - x` form
  `mkneg` emits for a negative float) + `cival_float_text()` (`%.17g`) +
  `emit_global_float_init()` + an `ISFLOAT` branch in `agg_emit_scalar`.
  Emits QBE `s s_<value>`.
- **Float DATA truncation FIXED** (`parse.c`) — QBE maps `s` (float) data →
  `DW`, which on i8086 (`wordsz==2`, where `int`/`Kw` is 2 bytes) emits the
  2-byte `int` width → a 4-byte float was truncated.  `case Ts:` now picks `DL`
  (the §ll `.long` = 4-byte directive) when `T.wordsz==2`.  Target-general
  (gated on word size), `make check` green.
- **Probe `minic/dos/examples/double_float_probe.c` (+golden), gated medium
  `--softfloat`** (`tools/test-dos.sh` **219/219 ok**): sizeof(double/float)==4,
  static float globals (incl. negative) + struct float members, double
  single-precision arithmetic, float↔double identity conversion, float→long
  (Ostosi Kl), float→int (Ostosi Kw), int→float (swtof).  Bug-loud: a `Kd`
  double would die() in the backend, a stale static-float init would die in
  minic, and a 2-byte float would mis-read.
- **REMAINING to actually enable `MICROPY_FLOAT_IMPL_FLOAT`** (after re-flipping
  `ports/dos8086/mpconfigport.h` to FLOAT and adding back
  `#define MICROPY_PY_BUILTINS_COMPLEX (0)` — complex defaults on with float,
  mpconfig.h:983, and is niche/costly here so keep it off):
  1. **qstr/genhdr regeneration (build infra, NOT a minic bug)** — objfloat.c
     and objtype.c reference NEW qstrs `MP_QSTR_float` / `MP_QSTR___float__`
     that are ABSENT from the pre-generated `ports/minimal/build/genhdr/
     qstrdefs.generated.h` (built for the integer-only config).  Regenerate the
     qstr/genhdr set with the float-enabled dos8086 config (MicroPython's
     `makeqstrdefs.py`/`makeqstrdata.py`).  This is how the build harness
     borrows genhdr from `ports/minimal/build`; it needs a float-config genhdr.
  2. **parsenum.c** — `dec_val` (a float local in the float-parsing path)
     reported "undefined variable".  Reduce to a minic probe (likely a
     float-local-in-a-conditional-block scope gap).
  3. **modbuiltins.c** — "non-constant in case label" (lookahead-lagged; find
     the real `case` — likely a float-related `round`/builtin switch).
  4. **binary.c** — "parse error" at `mp_decode_half_float`'s
     `union { uint16_t i; ... }` (float16 decode); reduce + fix the minic parse
     gap.
  Then build compact far-data `--keep-going`, **MEASURE the image** (§3y/§3x
  flagged ~3 KB body headroom; objfloat + formatfloat + parsenum-float + the
  soft-libm will likely overflow the ~896 KB Victor ceiling — levers are heap
  trim / feature trim; `--gc-sections` strips unused exp2f/log2f/expf/logf,
  keeping only powf), then run a float feature probe on Victor.
- **Reduction discipline reminder:** the dominant 93-TU blocker reduced cleanly;
  the remaining 4 (objfloat/objtype = qstr; parsenum/modbuiltins/binary =
  compiler) each need their own reduced `minic/dos/examples/*_probe.c` + gate
  before relying on the MP behavior, same as every prior §.

# Next session (§3y — transcendental soft-libm: exp2/log2/exp/log + powf)

## 2026-06-08 §3y notes (powf landed — the last soft-libm LINK blocker for MICROPY_FLOAT_IMPL_FLOAT)
- **Goal: implement the transcendental soft-libm `powf` (and the exp/log it
  needs)** — §3x's audit found `powf` is the one transcendental the curated
  MicroPython core references at LINK time under `MICROPY_FLOAT_IMPL_FLOAT`
  (objfloat `**`, parsenum `1eN`, modbuiltins `round(x,n)`); the algebraic
  surface (floor/ceil/round/fmod/fabs/copysign/isnan/isinf/signbit) was done
  in §3x.  This session closes the `powf` gap.
- **`minic/dos/softfloat.c` — added the transcendental core** (after `sf_fmod`):
  - `ieee_exp2(U32)` — 2^x: split x = n + r (n = nearest int via `sf_round`,
    r in [-0.5,0.5]), `sf_exp2_frac(r)` is a degree-7 Taylor in r with
    coefficients (ln2)^k/k!, then `sf_scalbn(g, n)` adds n to the exponent
    field (clamps to signed inf / signed zero).  Clamps |x| extremes first.
  - `ieee_log2(U32)` — log2(x): decompose x = 2^e·m, recentre m to
    [√½,√2), atanh series `s=(m-1)/(m+1)`, `log(m)=2s·(1+s²/3+s⁴/5+…)`
    (degree-9, 5 bracket terms), `log2(x)=e+log(m)·(1/ln2)`.
  - `sf_expf`/`sf_logf` are derived: `e^x = exp2(x·log2(e))`,
    `ln(x)=log2(x)·ln2`.  `sf_exp2f`/`sf_log2f` are thin wrappers.
  - `sf_powf(x,y) = 2^(y·log2(x))` with an **exact integer-exponent fast
    path** (binary exponentiation, `|y|≤64`) so `2**10`/`10**5`/`round(x,n)`
    are exact (the exp2/log2 round-trip alone gives `10**5 = 99999.977`); the
    squaring loop carries the sign of a negative base for free.  Full edge
    handling: `x^0=1`, `1^y=1`, nan, `0^±`, negative base (`nan` for
    non-integer exponent, signed for odd integer).  `sf_int_parity()` returns
    -1/0/1 (not-integer / even / odd).
  - All built on the exact `sf_add/sub/mul/div`, no float operators inside
    (consistent with the §3x algebraic helpers).
- **`minic/include/math.h`** — declared the 5 helpers and mapped
  `exp2f/exp2/log2f/log2/expf/exp/logf/log/powf/pow` to them.
- **Host validation FIRST** (the fast loop): compiled softfloat.c with
  `-DSF_HOST` + a libm-comparison harness — every case within ~2 ulps of
  glibc (rel ≤ 2.3e-7); integer powers exact.  Two bugs caught on the host
  before DOSBox: (1) initial pass forgot the integer fast path → `pow 10,5`
  off by 2 ulps (added it); (2) the fast path passed `sf_frombits(...)` (a
  float) to `sf_to_int` (which wants a BIT PATTERN) → exponent read as a
  denormal → `ye=0` → every integer power returned 1.  Fixed to
  `sf_to_int(ay & ABS_MASK)`.
- **Probe `minic/dos/examples/softtrig_probe.c` (+golden), gated medium with
  `--softfloat`** (`tools/test-dos.sh` **217→218 ok**).  19 lines: exp2/log2,
  exp/log, integer-pow fast path (`2**10`,`10**5`,`10**-2`,`(-2)**3`,`(-2)**2`),
  fractional pow (`2**0.5`,`9**0.5`,`3**3.3`), and edges (`x^0`,`0^3`,
  `(-2)**2.5`→nan).  Bit patterns round-trip exactly in DOSBox (golden
  generated from the SF_HOST build, 32-bit union to match the target's
  32-bit `unsigned long`).  Hit the known minic limit `{ U32 a=.., b=..; }`
  (multi-declarator-with-init in an inner block) → split into two decls.
- **Gates:** `make check` green; `tools/test-dos.sh` **218/218 ok**.  No
  MicroPython rebuild (float still `NONE` — flip is the next step).
- **Soft-libm is now LINK-complete for `MICROPY_FLOAT_IMPL_FLOAT`.**  Next
  steps (the remaining items 2-3 from §3x, now unblocked on the math side):
  1. **Wire softfloat.c into `tools/build-micropython.sh`** (always link it
     under float) and point the MP build's `<math.h>` at the real
     `minic/include/math.h` — `build/mp-spike/stubinc/math.h` is an EMPTY stub
     that SHADOWS the real one (`stubinc` is `-I`'d first); replace/redirect
     it for the MP build.
  2. **Flip `ports/dos8086/mpconfigport.h`** `MICROPY_FLOAT_IMPL` →
     `MICROPY_FLOAT_IMPL_FLOAT`, build compact far-data `--keep-going`, and
     **MEASURE the image**.  §3x flagged only ~3 KB body headroom; objfloat +
     formatfloat + parsenum-float + the soft-libm will likely overflow the
     ~896 KB Victor ceiling.  If so the levers are heap trim
     (`MICROPY_HEAP_SIZE`) or a feature trim — `--gc-sections`/`--pack-code`
     won't help (the float type is reachable once enabled, and gc-sections
     WILL strip the unused exp2f/log2f/expf/logf, keeping only powf).  Then
     run a float feature probe on Victor.

## 2026-06-07 §3x notes (toward MICROPY_FLOAT_IMPL_FLOAT: soft-libm groundwork)

## 2026-06-07 §3x notes (toward MICROPY_FLOAT_IMPL_FLOAT: soft-libm groundwork)
- **Goal was to enable `MICROPY_FLOAT_IMPL_FLOAT`.**  Audit first: under that
  config the curated MicroPython core references a soft-libm at LINK time
  (`parsenum.c`/`objfloat.c`/`modbuiltins.c` reference `powf`/`floorf`/`fmodf`/
  `copysignf`/`nearbyintf`/`nanf`; `formatfloat.c` references
  `isnan`/`isinf`/`signbit`/`fabsf`).  The existing soft-float surface was only
  `sf_add/sub/mul/div/from_int/to_int/cmp`.  Plus the image has only ~3 KB of
  body headroom (body 821168 loads; 824416 reports "Program too big").  So the
  flip is a multi-front effort, not a flag change — this session built the
  prerequisite **algebraic** soft-libm and fixed a backend bug it surfaced.
- **`minic/dos/softfloat.c` — added the EXACT/algebraic helpers** (no
  transcendentals): `sf_isnan/sf_isinf/sf_signbit`, `sf_fabs`, `sf_copysign`,
  `sf_nan`, `sf_trunc`, `sf_floor`, `sf_ceil`, `sf_round`, `sf_nearbyint`,
  `sf_fmod`.  These take/return honest `float`/`int` (called from C source, not
  emitted by the backend), reinterpreting to bits via a `union sf_cvt`.  All
  work on the 32-bit bit pattern and reuse the existing `sf_add/sub/cmp/to_int`
  (no float operators inside, so no `_sf_` lowering of the helpers themselves).
  `sf_fmod` is exact (exponent-aligned shift-subtract).  **`powf` is
  deliberately ABSENT** — it needs a soft `expf`/`logf` and is the next piece.
- **`minic/include/math.h` (NEW)** — declares the `sf_*` helpers and maps the
  libm names to them (`floorf`→`sf_floor`, `isnan`→`sf_isnan`, `fabsf`/`fabs`,
  `copysignf`, `nanf`/`nan`, `truncf`, `ceilf`, `roundf`, `nearbyintf`,
  `fmodf`, ...).  No `powf` yet.
- **Backend bug found + fixed (`copy.c`):** the soft-libm `floor/ceil/round/
  nearbyint/fmod` came out with INVERTED sign decisions (`fmodf(7,3)`→-1.0,
  `fmodf(-7,3)`→+1.0) while `sf_signbit` standalone was fine.  Reduced to
  `(int)(a >> 31) && (t != a)`: the `(int)` cast emits `%w =w copy %l` (a real
  16-bit truncation on i8086, where `l`=4-byte pair, `w`=2-byte reg), and
  `copy.c`'s `copyref()` folded EVERY `Ocopy` to its source — sound on
  word-uniform targets (registers alias) but on i8086 it let the `jnz` (a `w`
  use) reference the wider `l` temp; spill then parked it in a 4-byte slot and
  rega never reloaded the low word into the branch register, so the branch
  tested garbage.  **Fix:** `copyref()` no longer folds a class-narrowing copy
  (`i->cls==Kw` of a non-`Kw` temp) when `T.wordsz==2`; the explicit low-word
  `mov` is kept.  Generic-pass change gated on the i8086 word size, same shape
  as the `load.c` `T.wordsz` precedent.  `make check` green (no SSA regression).
- **Probes (both NEW, gated medium):**
  - `kl_narrow_copy_branch_probe.c` — pins the copy.c fix directly
    (`(int)(a>>31) && ...`, bug-loud: inverted sign without the fix).  Pure
    integer, no softfloat link needed.
  - `softlibm_probe.c` (`--softfloat`) — exercises every algebraic helper
    against known bit patterns via a union (`fabs/copysign/trunc/floor/ceil/
    round/nearbyint/fmod` + `isnan/isinf/signbit/nan`).
- **Gates:** `make check` green; `tools/test-dos.sh` **215→217 ok**.  No
  MicroPython rebuild this session (float not yet flipped).
- **Next on the float path (to actually enable `MICROPY_FLOAT_IMPL_FLOAT`):**
  1. **Implement `powf`** (and the soft `expf`/`logf`/`exp2f`/`log2f` it needs)
     in softfloat.c + math.h.  Integer-exponent fast path covers parsenum
     (`1e5`), `round(x,n)`, and integer `**`; the general fractional path needs
     exp/log.  This is the remaining hard blocker before MP float can LINK.
  2. **Wire softfloat.c into `tools/build-micropython.sh`** (add a `--softfloat`
     equivalent / always link it under float) and point the MP build's math.h
     at the real one — note `build/mp-spike/stubinc/math.h` is an EMPTY stub
     that currently SHADOWS `minic/include/math.h` (stubinc is `-I`'d first);
     replace/redirect it for the MP build.
  3. **Flip `ports/dos8086/mpconfigport.h`** `MICROPY_FLOAT_IMPL` →
     `MICROPY_FLOAT_IMPL_FLOAT`, build compact far-data with `--keep-going`,
     and MEASURE the image.  Expect the ~3 KB body headroom to be the wall:
     objfloat+formatfloat+parsenum-float+soft-libm will likely overflow.  If so,
     the levers are heap trim (`MICROPY_HEAP_SIZE`), a feature trim, or
     `--gc-sections`/`--pack-code` already in place won't help (float type is
     reachable once enabled).  Then run a float feature probe on Victor.

# Next session (§3w — far-data single-precision float load/store)

## 2026-06-07 §3w notes (float through a far pointer: loadfs/storefs)
- **Closed the last deferred far-data float gap.** Under compact/large/huge a
  `float` global/array/struct-member lives in a far segment, so reading/writing
  it goes through the i8086 far load/store path.  minic routed a far float
  through `loadfw`/`storefw` (16-bit) and silently truncated the 32-bit Ks
  value to its low half.  Now there are dedicated far single-float ops.
- **New QBE ops `loadfs` / `storefs`** (ops.h), mirroring `loadfl`/`storefl`
  but with a Ks value: `loadfs` result Ks ← far ptr (`l`); `storefs` value Ks,
  far addr (`l`).  Wired into `all.h` (`isloadfar`→`Oloadfs`, `isstore` 2nd
  range→`Ostorefs`) and `load.c` `storesz` (Ostorefs = 4 bytes).  The QBE IL
  lexer perfect-hash (parse.c `K`) did NOT collide — no regen needed.
- **i8086 backend** (`i8086/emit.c`): `Oloadfs` falls into the `Oloadfl`
  handler, `Ostorefs` into `Ostorefl` — the far 32-bit DX:AX move is
  class-agnostic.  Oloadfs (cls Ks) is excluded from the soft-float `(2)` Ks
  guard so it reaches the main op switch with the other far ops.
- **minic** (`minic.y`): `loadfar`/`storefar` + the 3 inline member/array
  `storef*` sites gain an `'s'` branch (loadfs/storefs).
- **Also fixed a float usual-conversion bug exposed under far-data:** the
  float↔double conversion sites compared the FULL ctyp (`a.ctyp != b.ctyp`)
  to decide whether to emit `exts`/`truncd`.  Under far-data a float VALUE
  carries an extra `FAR` bit, so `float = float` (and `float + float`, and
  `return float`) spuriously emitted a `truncd` on an already-Ks operand
  (QBE: "invalid type for first operand in truncd").  Fixed 4 sites (binop l/r
  at minic.y ~1843/1868, assignment ~3921, return ~4688) to compare only the
  floating PRECISION: `(KIND(a)==LNG) != (KIND(b)==LNG)`.  Medium codegen is
  unaffected (the FAR bit is never set there).
- **Probe:** `minic/dos/examples/float_fardata_probe.c` (+golden), gated under
  COMPACT/LARGE/HUGE with `--softfloat`.  Exercises far round-trip, far
  arithmetic (`g_c = g_a OP g_b`), float through an explicit far pointer, far
  float array element, far float struct member, and far compare — all via a
  near `union` so a truncated high word prints a wrong `%08lx`.  Bit patterns
  round-trip exactly on all three far-data models in DOSBox.
- **Gates:** `make check` green; `tools/test-dos.sh` **215/215 ok**.  Soft-float
  is now model-complete (medium arith/compare/convert + far-data load/store).
- **Next on the float path:** `MICROPY_FLOAT_IMPL_FLOAT` — enable single-float
  in the MicroPython compact far-data build and run a float feature probe on
  Victor.  Watch image size (MP is currently `MICROPY_FLOAT_IMPL=NONE`;
  enabling float pulls in objfloat.c + float formatting and will grow the
  image — may bump against the Victor load ceiling).

# Next session (§3v — unary-minus-on-float / float-vs-int usual conversions)

## 2026-06-07 §3v notes (unary minus on a float → single precision)
- **Compiler change — float usual-arithmetic-conversions corrected:** minic's
  binop type-promotion promoted `float OP int` (and `int OP float`) to `double`
  (Kd), which the i8086 soft-float backend `die()`s on.  Per C, a `float`
  combined with an integer stays `float` — the integer converts to float; only
  a `double` operand makes the result double.  `minic/minic.y` now computes the
  common floating type as `double` iff either operand is itself double, else
  `float` (action-body only, no grammar change).
- **Closes unary minus on a float:** `mkneg` desugars `-x` to `0 - x` with an
  integer `0`; that subtraction now stays Ks instead of Kd.  Verified `-x` →
  `=s sub` → `call far _sf_sub`, `x + 1` → `=s add` → `call far _sf_add`.
- **Probe:** extended `minic/dos/examples/softfloat_probe.c` (+golden) with
  direct `-f3`, `-fneg7`, `f3 + 1`, `2 - f1`, `f3 * 2`; removed the stale
  "deliberately avoids unary minus" note.  DOSBox run matches golden.
- **Gates:** `make check` green; `tools/test-dos.sh` **212/212 ok**.  MicroPython
  compact far-data rebuild **106/106 objects, image 844288 — byte-identical** to
  §3u (MP is `MICROPY_FLOAT_IMPL=NONE`, so no float-promotion path is reachable).
- **Next on the float path:** far-data `Ks` load/store (`loadfw`/`storefw`
  truncate Ks through a far ptr — the `[[storefar-lacks-storefl]]` family
  extended to Ks), then `MICROPY_FLOAT_IMPL_FLOAT`.

# Next session (§3u — float-literal Ks typing + feature-surface validation)

## 2026-06-07 §3u continuation notes (float literal `1.5f` → Ks; broad MP validation)
- **Validation sweep first (no compiler bug found — current image is solid):**
  - `make check` green; `tools/test-dos.sh` 211/211 before changes.
  - `build/mp-frontier2.py` on the normal compact image: reaches `OK filtercomp`, then stops at the `recsum(30)` case — the KNOWN deep-recursion frontier (a runtime stack / image-size tradeoff documented in §3o/§3t, not a clean compiler bug; stackless-strict fixes it but doesn't fit the ceiling as the default).
  - GC pressure is CORRECT: `build/mp-churn-scale.py` passes churn3/10/20/40 (clean `D4`/`C5`).  `build/mp-churn120.py` prints `XX churn 7980 7860` — but **7980 is the correct answer** (`sum(i+7 for i in 0..119) = 7140+840 = 7980`); the scratch script's golden `7860` is wrong (so is `mp-churn60.py`'s `2130`, should be `2190`).  These `build/*.py` are untracked scratch, not gated probes.
  - New broad probe `build/mp-strfeat-probe.py`: slicing (`[a:b]`, negative), `split`/`join`/`find`/`replace`/`strip`/`upper`/`startswith`/`endswith`, `hex`/`bin`/`int(base)`, string concat, `str.format` — ALL pass.  Exception tracebacks render correctly throughout.  The only failures were minimal-config feature gates (extended `[::-1]` slices → `NotImplementedError: only slices with step=1`; `str.count` → `AttributeError`), i.e. MicroPython config decisions, NOT compiler bugs.
- **Compiler change this session — `1.5f` → single-precision (Ks):** minic used to type every float literal as `double` (Kd), which the i8086 soft-float backend `die()`s on.  Now an `f`/`F`-suffixed literal types as `float` (Ks) and lowers to the `_sf_*` helpers; un-suffixed literals stay double.
  - `minic/minic.y`: lexer tracks a new `single_float` flag and stamps it on the `'F'` node's `nlong` field (unused for `'F'` until now); `expr()` `case 'F'` branches on `n->nlong` to emit `=s copy s_<v>` (ctyp `INT|FLOAT`) vs `=d copy d_<v>` (ctyp `LNG|FLOAT`).  No grammar change.
  - Verified end-to-end: `x + 1.5f` → `s_1.5` → pattern `0x3FC00000` → `call _sf_add` (`/tmp` SSA smoke + DOSBox).
  - Probe `minic/dos/examples/float_literal_probe.c` (+golden), wired into `tools/test-dos.sh` MEDIUM with `--softfloat`.  Combines literals with runtime floats so QBE can't fold them to constants; a mis-typed double literal would `die()` in the backend, so merely running proves Ks.  Updated the stale "float literals → double" note in `softfloat_probe.c`.
  - Gates: `make check` green; `tools/test-dos.sh` **212/212 ok**.  MicroPython compact far-data rebuild **106/106 objects, image 844288 — byte-identical** to the §3t image (MP's `MICROPY_FLOAT_IMPL=NONE` has no reachable `f`-literals, so no MP behavior change).
- **Next on the float path:** unary minus on a float (`-x` still desugars to `0.0 - x` in double → Kd; see softfloat_probe note), then far-data `Ks` load/store (`loadfw`/`storefw` truncate Ks through a far ptr — the `[[storefar-lacks-storefl]]` family extended to Ks), then `MICROPY_FLOAT_IMPL_FLOAT`.

## 2026-06-07 Codex continuation notes (globals-map corruption fixed)
- Root-caused and fixed the `HAS_CK False` globals-map corruption from the prior frontier.
- **Bug:** `block_scope_decl()` in `minic/minic.y` folded a block-scoped *array* and a sibling-block *pointer* of the same name + element type into one stack slot — it compared only `ctyp`, not array-ness.  MicroPython's list-comprehension codegen emits exactly that shape (an `args2[N]` array in one branch, an `obj_t *args2` pointer in another), so the array's `memcpy` wrote through into adjacent storage and clobbered the globals map (visible as the `ck` key turning into a non-string `<>` object).
- **Fix:** `block_scope_decl` now takes an `isarray` arg and renames on `varh[h].ctyp != ctyp || varh[h].isarray != isarray`.  All 8 call sites updated; the two array-decl stmt rules and the unsized/sized local-array-init rules that previously bypassed renaming (`v = $2->u.v`) now route through `block_scope_decl(..., 1)`.  No grammar change (action-body only), `make check` green.
- **Probe:** `minic/dos/examples/local_array_memcpy_probe.c` (compact, gated).  Bug-loud confirmed: without the fix `victim0/victim1` clobber to `286335522`/`858997828`; with it they read the correct `1431660134`/`2004322440`.
- **Verification:**
  - `tools/test-dos.sh`: **211/211 ok** (was 210; +1 for the new probe).
  - MicroPython compact far-data rebuilt: 106/106 objects, body `821184` (+32 B), total `844288` (under the Victor ceiling).
  - `VICTOR_SRC=build/mp-repeat-comp-globals-direct.py tools/run-victor-sasi.sh build/mp-link/mpython.exe 260` now prints `HAS_CK True`, `HAS_BASE True`, `HAS_ARG True`, `DONE`, clean `D4`/`C5`.
- Next: resume the "Candidate next exercises" list below — heavier string formatting / `repr` / GC pressure, and the stackless-strict recursion frontier.  (The float path is CLOSED as of §4a — see top of file.)

## Active focus
Stevie §3r is closed.  Manual MAME testing confirms `dw`/`de` work, matching the scripted Victor/MAME checks and the tracked gates.  Keep Stevie as a regression target, but stop using it as the primary driver unless a new editor regression appears.

Return to the MicroPython port as the main exercise tool for identifying QBE/MiniC/i8086/runtime improvements:
- Run real MicroPython features on the Victor/compact far-data build.
- When MicroPython exposes a failure, reduce it to a tiny MiniC/DOS probe first.
- Fix the compiler/backend/runtime bug underneath it.
- Add the reduced probe to `tools/test-dos.sh` before relying on the MicroPython behavior.
- Keep watching image size; previous MicroPython builds were close to the practical Victor load ceiling.

## Starting point
- Current QBE gates were green after the latest compiler fix: `make check` and `tools/test-dos.sh` (`209/209 ok`).  Re-run them before committing any compiler/backend changes.
- Stevie §3r details are archived in `SESSION_LOG.md`; current status is no `dw`/`de`/`yw` through-EOF reproduction in tracked probes, redirected Victor/MAME edit-loop checks, or manual MAME testing.
- MicroPython compact far-data was rebuilt on 2026-06-06 with current QBE: 106/106 objects, body `821072` after increasing the link stack to 24 KiB.
- Victor/MAME smoke coverage passes on the rebuilt image:
  - `build/mp-test.py`: primes/list-comprehension/fib/dict loop, clean `D4`/`C5`.
  - `build/mp-feature-probe.py`: integer ops, classes/inheritance, string methods, list sort/comprehension, generators, exceptions/finally, min/max/abs/sorted/enumerate, clean `D4`/`C5`.
  - `build/mp-frontier.py`: kwargs/defaults, closures, tuple unpacking, recursion to 6, allocation churn, clean `D4`/`C5`.
- The 24 KiB stack change fixes the old 8 KiB-stack return-path corruption reproduced by `build/mp-recsum8.py` (`recsum(8)` now prints `DONE`, `D4`, `C5`).
- Deep recursion is still bounded by stack/runtime behavior, not yet a compiler probe: `build/mp-frontier2.py` reaches dict/filter comprehensions then fails at `recsum(30)`; `build/mp-recsum-probe.py` reaches `recsum(12)` on the 24 KiB stack and fails by `recsum(20)` with the uncaught path printing `(nil)`.  A 28 KiB link stack produces body `824416` and does not load on Victor (`Program too big to fit in memory`), while 32 KiB cannot link (`DGROUP + stack overflows 64KB`).
- The latest reduced compiler issue was an unsigned int-to-long promotion bug in MiniC: `prom()` sign-extended unsigned `int` operands when comparing/promoting against `long`.  This was fixed with `extuw` for unsigned widening and covered by compact DOS probe `minic/dos/examples/uint_widen_cmp_probe.c`.
- MicroPython stack-check experiments exposed that unsigned-widening bug in `mp_cstack_check()`, but stack checks were not left enabled.  With the normal minimum-ROM no-stack-check config restored, MicroPython still reaches `recsum(12)` and still fails around `recsum(20)` with `(nil)`.
- Software single-precision float is COMPLETE for DOS (medium + far-data, literal/unary typing, double→single, static init, soft-libm) and gated via the `--softfloat` probes.  MicroPython float enablement is CLOSED (§4a, won't-fit on Victor) — float is NOT a target; the port stays integer-only.
- The next session should investigate whether the remaining deep-recursion failure is expected MicroPython stack-limit handling, a bad uncaught-stack-overflow exception path, or excessive i8086 VM C-frame size.  If it becomes a compiler/backend issue, reduce it to a focused `minic/dos/examples/*_probe.c` before fixing.

## 2026-06-06 Codex continuation notes
- Re-verified the normal compact far-data MicroPython image: 106/106 objects, body `821072`, total `844176`.
- `make check` passes and the full DOS gate passes: `tools/test-dos.sh` reports `209/209 ok`, including `uint_widen_cmp_probe`.
- `uint_widen_cmp_probe` also passes standalone under compact DOSBox:
  - `field_hi=1`, `field_lo=0`, `local_hi=1`, `local_lo=0`.
- Victor/MAME MicroPython status with the normal no-stack-check image:
  - `build/mp-test.py` via redirected REPL passes and reaches clean `C5`.
  - `build/mp-frontier.py` via redirected REPL passes and reaches clean `C5`.
  - `build/mp-feature-probe.py` should be run as whole-file `PROG.PY` via `VICTOR_SRC=build/mp-feature-probe.py tools/run-victor-sasi.sh build/mp-link/mpython.exe 260`; in that mode it passes all listed feature checks and reaches `D4`/`C5`.  The redirected REPL harness can split nested compound blocks in this script and produce `IndentationError`/`SyntaxError`; treat that as a REPL harness limitation, not a VM/compiler failure.
  - `build/mp-recsum-probe.py` via whole-file `PROG.PY` still reproduces the frontier: `recsum(6)` and `recsum(12)` complete, then `recsum(20)` enters the uncaught path as `DE` + `(nil)`.
  - `build/mp-frontier2.py` via whole-file `PROG.PY` reaches `OK nonlocal1`, `OK nonlocal2`, `OK tuple-loop`, `OK dictcomp`, `OK filtercomp`, then does not reach `C5` at the `recsum(30)` case.
- Added `MP_EXTRA_CPPFLAGS` support to `tools/build-micropython.sh` and `tools/recompile-mp-tu.sh` so MicroPython config experiments can be driven from the QBE repo without editing the external MicroPython checkout.
- Stack-check experiment:
  - Build command: `MP_EXTRA_CPPFLAGS='-DMICROPY_STACK_CHECK=1' tools/build-micropython.sh --model=compact`.
  - The generated `mp_cstack_check()` now uses `extuw` for `stack_limit` before the unsigned compare, confirming the earlier MiniC unsigned-widen fix applies to the real MicroPython path.
  - The stack-check image links but is too large for the Victor load ceiling: body `825648`, total `849104`, and Victor prints `Program too big to fit in memory`.
  - Conclusion: the remaining deep-recursion failure is presently a MicroPython port/runtime stack-guard and image-size tradeoff, not a proven compiler/backend bug.  To ship clean recursion failure handling, either recover roughly 5 KiB of image/body size, trim another feature/memory consumer, or implement a smaller DOS-port-specific recursion/stack guard.

## 2026-06-07 Codex continuation notes
- Added MicroPython build experiment knobs to `tools/build-micropython.sh` and `tools/recompile-mp-tu.sh`:
  - `MP_STACK_SIZE` overrides the linker stack size, default still `24576`.
  - `MP_STACK_LIMIT` rewrites the DOS port's `mp_stack_set_limit(8192)` in generated `main.pp.c`, default still `8192`.
  - `MP_HEAP_SIZE` rewrites generated `static char heap[(49152)]`, default still `49152`.
  - `MP_DOS_TINY_STACK_CHECK=1` rewrites generated `mp_cstack_check()` to a smaller DOS offset-only guard for stack-check experiments.
  - `MP_DOS_STACKLESS_RECURSION_RAISE=1` appends `mp_raise_recursion_depth()` to generated `runtime.pp.c` so `MICROPY_STACKLESS=1` + `MICROPY_STACKLESS_STRICT=1` can link without enabling global `MICROPY_STACK_CHECK`.
  - Full builds now refresh `/tmp/mp_objs.txt`, making `tools/recompile-mp-tu.sh` usable immediately after a full MicroPython build.
- Standard `MICROPY_STACK_CHECK=1` can be made to fit by reducing linker stack size, but it is not useful as-is on i8086:
  - `MP_STACK_SIZE=18432` links at total `842960`, but raises `RuntimeError: maximum recursion depth exceeded` before `recsum(6)` completes.
  - Raising `MP_STACK_LIMIT` through `14336`, `16384`, `20480`, `21504`, `22272`, and even `30000` still trips during shallow recursion.  The generated limit write is correct; the stack-check-enabled VM path simply consumes too much transient C stack for this byte-limit approach.
  - `MP_DOS_TINY_STACK_CHECK=1` removes the `mp_cstack_usage()` call/divide from `mp_cstack_check()`, but does not materially improve the shallow-recursion frontier.
- `MICROPY_STACKLESS=1 MICROPY_STACKLESS_STRICT=1` is the promising recursion direction:
  - Plain stackless strict initially failed to link because `_mp_raise_recursion_depth` was undefined; the generated `runtime.pp.c` patch above fixes that without enabling global stack checks.
  - With `MP_STACK_SIZE=8192`, stackless strict links at total `835088` with the default 48 KiB heap and passes `build/mp-recsum-probe.py`: `recsum(6)`, `recsum(12)`, `recsum(20)`, and `recsum(30)` all complete, followed by clean `D4`/`C5`.
  - Stackless strict also passes whole-file Victor/MAME runs of `build/mp-test.py`, `build/mp-feature-probe.py`, and `build/mp-frontier.py`.
  - `build/mp-frontier2.py` reaches `OK recsum`, so the old `recsum(30)` frontier is fixed; it does not complete the later heavy churn section within the tested run.
- New MicroPython frontier found while testing stackless: integer-to-string conversion hangs/restarts on both normal and stackless images.
  - Minimal reproducer: `build/mp-str-int-probe.py` with `print(str(0))`.
  - Normal no-stackless 24 KiB image prints startup markers and `A`, then never reaches the string output or `C5`.
  - Stackless image shows the same failure/re-entry pattern, so this is pre-existing, not a stackless regression.
  - A heavier churn expression also stalls at `str(i)` after proving list comprehension and `row[-1]` work (`build/mp-churn-progress.py` and `build/mp-dict-expr-probe.py`).

## 2026-06-07 Codex continuation notes (str(int) fixed)
- Reduced the MicroPython `str(int)` stall to `minic/dos/examples/mp_str_int_probe.c`, now gated under compact in `tools/test-dos.sh`.
- Two bugs were found/fixed:
  - `sizeof(s->buf)` for an array member accessed through a struct pointer decayed to pointer size (`4` in compact) instead of the declared array byte size.  MiniC now detects `sizeof(obj.arr)` / `sizeof(ptr->arr)` before expression decay, matching the existing bare-array `sizeof(arr)` behavior.
  - Far-data calls to `memmove()` were not remapped to a far-pointer runtime helper.  MicroPython's `vstr_add_strn()` passed 4-byte far pointers, but libstub `_memmove` consumed near-pointer args.  Added `_far_memmove` and remapped `memmove` in far-data models.
- Current normal compact far-data MicroPython image after the fix:
  - `tools/build-micropython.sh --model=compact`: 106/106 objects, body `821152`, total `844256` (about +80 bytes vs the prior normal image).
  - `VICTOR_SRC=build/mp-str-int-probe.py tools/run-victor-sasi.sh build/mp-link/mpython.exe 260` now prints `A`, `0`, `B`, `123`, `DONE`, `D4`, `C5`.
  - `build/mp-test.py`, `build/mp-feature-probe.py`, and `build/mp-frontier.py` all pass whole-file Victor/MAME runs and reach clean `D4`/`C5`.
- Final gates after the fix:
  - `make check` passes.
  - `tools/test-dos.sh` passes: `210/210 ok`.

## Candidate next exercises
1. Continue pushing MicroPython features past the fixed `str(int)` frontier: heavier string formatting, `repr()`, dict/list rendering, exception tracebacks, and GC pressure.
2. Revisit stackless strict as the recursion direction now that string conversion is fixed; rerun `build/mp-frontier2.py` and the churn scripts to identify the next real frontier.
3. ~~Resume the float path~~ — CLOSED 2026-06-08 (§4a, user decision): float on
   Victor is "won't-fit, not worth it".  Soft-float stays a gated medium-model
   DOS capability only; the MicroPython port stays integer-only.  Do NOT
   re-attempt the flip.
4. For every failure, follow the same discipline as §3r: reproduce as a focused `minic/dos/examples/*_probe.c`, fix QBE/MiniC/runtime, then gate it.

## 2026-06-07 Codex continuation notes (post-str(int) frontier)
- Normal compact far-data MicroPython was rebuilt at `844256` bytes before probing.  A stackless strict image also rebuilt and fit at `835168` bytes.
- Rendering/string follow-up:
  - `build/mp-render-probe.py` passes on Victor: `repr(int)`, `repr(str)`, `repr(list)`, `repr(dict)`, and `str(ValueError("boom"))` all reach clean `D4`/`C5`.
  - Old-style `%` string formatting is not available in this minimal MicroPython config: `build/mp-format-probe.py` fails immediately with `TypeError: unsupported type for operator` on `"x=%s" % "ab"`.  Treat this as config/support, not a compiler bug unless the port intentionally enables string modulo formatting.
- New reproducible MicroPython frontier:
  - `build/mp-repeat-comp-globals-direct.py` is the cleanest current repro.  It defines `ck`, `comp_base`, and `comp_with_arg`; after two simple list-comprehension calls and one argument-capturing list-comprehension call, `"ck" in globals()` becomes `False` while `"comp_base"` and `"comp_with_arg"` remain `True`.
  - `build/mp-repeat-comp-key-list-direct.py` shows the globals keys after the same sequence as `['comp_with_arg', <>, '__name__', 'comp_base']`: the `ck` key slot appears to have been overwritten/corrupted into a non-string object key rendered as `<>`.
  - Adding an extra global before `ck` avoids the symptom (`build/mp-first-global-probe.py`, `build/mp-first-function-probe.py`), so the failure is globals-map-layout sensitive.
  - Repeated scalar calls and repeated list-literal allocation pass (`build/mp-repeat-call-probe.py`), and the simple negative-`unsigned long` pointer-index/store-pop C shape was tested as a temporary DOS probe and passed.  The current evidence does not support `fastn[-unum] = (*sp--)` as the reduced compiler bug.
  - Speculative GC fixes were tried in generated `gc.pp.c` only: scanning `mp_state_ctx` at extra far-pointer alignments, extending roots through `qstr_last_chunk`, and scanning heap payloads every 2 bytes / with interior-pointer marking.  None fixed `HAS_CK False`; do not re-try those unchanged.
- Suggested next reduction path:
  - Instrument MicroPython at the C level around `mp_store_name()` / `mp_obj_dict_store()` / `mp_map_lookup()` for the failing script to dump the raw key/value words in `mp_state_ctx.vm.dict_main.map.table` before and after `comp_with_arg(0)`.
  - Look for a VM/compiler pattern that writes a non-qstr object into a globals map key slot.  The corruption is visible before any explicit key-list iteration; key-list probes that use `for k in globals()` mutate globals via `k`, so prefer `list(globals())` or direct membership probes.

## 2026-06-07 Codex continuation notes (repeat comprehension globals fixed)
- Fixed the `build/mp-repeat-comp-globals-direct.py` frontier.
  - Instrumentation showed top-level globals stores were clean: `ck` was inserted as qstr key `0:1754`.
  - After `comp_with_arg(0)`, the globals table slot for `ck` changed to two heap pointers, while no `mp_obj_dict_store()` ran.  The corruption happened inside the parent `comp_with_arg` before the child list-comprehension bytecode began.
  - Reduction found MiniC emitted duplicate SSA names for same-named block locals when one declaration was an array and a sibling declaration was a pointer of the same stored C type.  MicroPython's `closure_call()` has exactly this shape:
    - `if (...) { mp_obj_t args2[5]; ... }`
    - `else { mp_obj_t *args2 = ...; ... }`
  - The bad SSA was `%args2 =l alloc4 20` followed by `%args2 =l alloc4 4`; the array arm then loaded `%args2` as a pointer and passed an arbitrary destination to `_far_memcpy`.
- Fix:
  - `block_scope_decl()` now considers both stored type and `isarray` when deciding whether to alpha-rename a colliding block local.
  - Block-scoped array declaration rules now call `block_scope_decl(..., isarray=1)`, including fixed-size, initialized fixed-size, and unsized initialized arrays.
  - Added compact DOS regression `minic/dos/examples/local_array_memcpy_probe.c`, gated in `tools/test-dos.sh`, to pin the shadowed local-array/local-pointer case.
- Verification after the fix:
  - `make check` passes.
  - `tools/test-dos.sh` passes: `211/211 ok`.
  - Clean normal compact far-data MicroPython rebuild: 106/106 objects, image `844272` bytes, body `821168`.
  - `build/mp-repeat-comp-globals-direct.py` now prints `HAS_CK True`, `HAS_BASE True`, `HAS_ARG True`, then clean `D4`/`C5`.
  - `build/mp-repeat-comp-key-list-direct.py` prints `KEYS ['comp_with_arg', 'ck', '__name__', 'comp_base']`, with no `<>` key.
  - `build/mp-test.py` and `build/mp-feature-probe.py` passed on the rebuilt image before the final no-op unsized-array scope hook cleanup; direct repro was rerun after the final rebuild.
- Next MicroPython exercise:
  - Continue past the fixed repeated-comprehension/globals frontier.  Good next scripts are `build/mp-frontier.py`, `build/mp-frontier2.py`, and the churn probes (`build/mp-churn-progress.py`, `build/mp-dict-expr-probe.py`, etc.).
