# Claude Session Status: QBE C11/C17 8086 Compiler

**Project:** C11/C17 + GNU-extensions C Compiler for 8086 DOS using QBE Backend
**Standard:** C11 feature set (`_Static_assert`, `_Generic`, `_Alignof`/`_Alignas`, compound literals, designated initializers, anonymous struct/union) + GNU extensions (`__attribute__`, inline `__asm__`, `__far` pointers).  Equivalently **C17-level** since C17 added no new language features over C11.  **No C23 language features.**  C only — no C++.
**Real target hardware:** Victor 9000 / Sirius 1 (~896 KB RAM) — NOT the IBM-PC 640 KB ceiling.  DOSBox is the fast loop for images that fit; MAME victor9k + SASI disk for the real thing.
**Last Updated:** 2026-06-11

---

## Where the project is (one paragraph)

The original compiler goal is **complete**, and so is the MicroPython hardening campaign that followed it.  All **six memory models work** (tiny .COM / small / medium / compact / large / huge — small was the last broken one, fixed 2026-06-11).  The per-model runtime gate is **287/287** (`tools/test-dos.sh`).  MicroPython (108 TUs) runs on a real Victor 9000: full language surface, FLOAT, the `math` module, a 114 KB split GC heap, an interactive REPL — all Victor-verified byte-exact vs host python3.  MicroPython's remaining feature tracks are **PARKED**; its role now is a **regression corpus** (rebuild compact, byte-compare the 731,088-byte body after toolchain changes; Victor re-runs only when bytes move).  Upstream QBE is in sync through `e786f06`.  Stevie ships as a 146 KB medium-model .EXE, interactively verified on the Victor.

## 🚧 Next frontier: newlibc for the Victor 9000 (Phase 6, designated 2026-06-11)

Compile **`~/projects/newlibc`** — a much-progressed Victor 9000 C library + driver suite (bare-metal newlib port: crt0, libgloss, display/keyboard/timer/SASI drivers, VFS + read-only FAT, ~40 MAME-driven tests; phase 1 DOS drivers built with OpenWatcom, phase 3 bare-metal built with ia16-elf-gcc) — with **this** toolchain, and adopt its test suite as a standing robustness harness.  End state: newlibc replaces libstub as the real libc.  **Step 1 (triage sweep) DONE 2026-06-11 (§6a)**: 46/66 phase-3 TUs compile under small AND medium after seven minic dialect fixes.  **Step 2 (DOS-hosted portable subset) DONE 2026-06-11 (§6b)**: ELEVEN newlibc tests (snprintf, six FAT/VFS, ramfs, stdio_route, bss, terminal_meta) run DOS-hosted through the full newlibc stack (printf wrappers → syscalls → VFS → FAT-over-RAM-block) and are standing gate entries (`tools/build-newlibc-test.sh`, `minic/dos/newlibc/`, `libstub_to_exe.py --no-stdio`); two toolchain bugs fixed en route (static file-scope data linkage; decimal `UL` literal typing).  **See ROADMAP.md → Phase 6** for the plan and the remaining challenges (ISR definitions + extended-asm dialect; omf_link raw-binary output; newlib proper out of near-term scope).  Next: step 3 — omf_link raw-binary output + minic-built crt0 + MAME bare-metal hello.

---

## 📍 Status & history pointers

- **[ROADMAP.md](./ROADMAP.md)** — current component status, Phase 5 close-out, the Phase 6 (newlibc) plan.  *(Updated 2026-06-11.)*
- **[NEXT_SESSION.md](./NEXT_SESSION.md)** — per-session engineering notes, newest first (§5c at top).  The detailed history of every bug and fix lives here and in git, not in this file.
- **[MICROPYTHON_PORT.md](./MICROPYTHON_PORT.md)** — the MP port reference.
- **[I8086_TARGET.md](./I8086_TARGET.md)** / **[i8086/README.md](./i8086/README.md)** — backend reference.
- Memory files (auto-loaded) carry the distilled bug-class lessons ([[…]] links).

## Open tracks (pick by appetite; none urgent)

1. huge `_qbe_huge_add` ≥0x8000 variable-index gap (§4i).
2. `jmp_buf bufs[6]` cross-frame longjmp (§4v) — unreduced; reduce first.
3. minic static-init float const-expr folding (`static float x = 2.0f*3.14f;`) — also unlocks MICROPY_PY_MATH_CONSTANTS.
4. Small-model setjmp/longjmp (near env) — only if a small-model consumer needs it (newlibc may).
5. Multi-decl items after the first skip block_scope_decl (loud "double definition").
6. Kw spill-slot sharing — frame-size lever, no consumer pain.

## House rules (process invariants that cost real time to relearn)

- **Gate before trust:** every fixed bug gets a reduced `minic/dos/examples/*_probe.c` wired into `tools/test-dos.sh` (bug-loud — verify the probe fails on the unfixed code) BEFORE the behavior is relied on.
- **minic staleness trap:** top-level make skips minic.y rebuilds.  Always `rm -f minic/minic && touch minic/minic.y && make minic/minic` and verify the .ssa changed before trusting a gate run.  See [[minic-make-staleness]].
- **After ANY i8086/emit.c change:** run `tools/run-emit-audit.sh` (the §4y standing tool — exact-liveness CHK markers + symbolic checker; 339 files / 112k regions / 0 violations is the baseline).
- **After ANY toolchain change:** rebuild MP compact (`tools/build-micropython.sh --model=compact`, needs a far-data model since MP_SPLIT_STACK=1) and byte-compare the body.  Byte-identical ⇒ no Victor run.
- **Victor-bound probes MUST print per-phase progress** — a 5 MHz 8088 makes silence unreadable (a 700 s "hang" was just slowness).
- **Victor harness is deterministic** — empty serial output means a host-invocation mistake or a real hang, never "MAME flakiness".  Run `tools/run-victor-sasi.sh` foreground.
- **DOSBox fast loop for MP:** `MP_HEAP_SIZE=8192 MP_HEAP2_SIZE=12288 MP_STACK_SIZE=16384` → ~600 KB image that loads in DOSBox; PROG.PY beside the exe.
- **miniyacc gotchas:** no standalone `/* … */` comment between a `;` and the next production head; action-body comments must avoid `'` `/` `[` `]`.
- **Grammar conflict baseline:** 115 shift/reduce, 0 reduce/reduce.  Any change to that count needs justification.
- **Commit at green-gate milestones** to master without asking; stage only the session's files; no push/PR unless asked.

## Build & test quick reference

```sh
make check                      # QBE SSA tests
tools/test-dos.sh               # the 274-entry per-model runtime gate (DOSBox)
tools/build-example.sh --model=<tiny|small|medium|compact|large|huge> <file.c>
tools/build-stevie.sh --exe     # stevie editor
tools/build-micropython.sh --model=compact   # the MP regression corpus
tools/run-victor-sasi.sh <exe>  # real-hardware-equivalent MAME victor9k run
tools/run-emit-audit.sh         # emit-bracket liveness audit (after emit.c changes)
```

---

## Repository Information

**Repository:** https://github.com/pauldevine/qbe  (branch: master; PRs #23 upstream rebase, #24 §3p–§4y, #25 §5c)
**Upstream:** `upstream` remote → c9x.me/qbe.git — check upstream first for generic spill/SSA/regalloc bugs.  In sync through `e786f06` (2026-06-11).
**Related trees:** `~/projects/newlibc` (Phase 6 target), `~/projects/micropython` (dos8086 port), `/Applications/dosbox.app`, MAME victor9k.

This project is developed by Paul Devine with assistance from Claude (Anthropic).
