# Next session — BUILD A MAME VICTOR 9000 HEADLESS TEST HARNESS (replace DOSBox); then re-gate on-target

> **WHY THIS IS THE NEXT SESSION (user direction 2026-05-30):** the real target
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
