# gcc.dg runtime-test triage (tools/gccdg)

The EDG C++ front end (https://github.com/edgcpp/compiler, open-sourced
2026) ships a curated import of GCC's `gcc.dg` C testsuite under
`tests/tests/imported/gnu/c`, each file tagged with a `//type:` line.  The
`rp` (runtime-positive) files are self-checking programs: `abort()` on a
wrong answer, return 0 / `exit(0)` on success — the same shape as this
project's probe gate, written by GCC developers to pin real optimizer and
front-end bugs.  These tools run that corpus (≈2,600 tests) through the
8086 toolchain and under DOSBox.

The tests are GPLv3 (GCC's), so they stay OUT of this repo: `fetch.sh`
makes a sparse, shallow, rev-pinned checkout under `build/gccdg/edg/`.

| tool | what |
|---|---|
| `fetch.sh` | sparse checkout of `tests/tests/imported/gnu/c` at `EDG_REV` (no-op when already there) |
| `wrap.sh` | select the `rp` single-file tests (minus the ones needing a 32-bit `int`, see the header) and wrap each in a PASS/ABORT driver → `build/gccdg/wrapped/gd_<id>.c`, pre-expanded with `clang -E` (macOS `cpp` is traditional: `F (x)` with a space is not a macro call there) |
| `gccshim.h` | GCC builtins / libm names → what minic + the libstub-free runtime provide |
| `sweep.sh [model]` | compile-only triage through the exact build-example.sh stages: one line per test (`PASS` or first failing stage `cpp minic qbe omf nasm`, plus `minic-crash`/`qbe-crash` on a signal) in `build/gccdg/out-<model>/sweep.txt` |
| `run.sh [model] [--save-baseline]` | build every sweep-PASS test (`build-example.sh --softfloat`, parallel) and run them all under DOSBox in chunks (a hang is charged to the first silent program and the rest of its chunk re-run); verdicts in `out-<model>/results.txt`; compared against `baseline-<model>.txt` — a PASS that stops passing is a regression (exit 1) |

Typical loop:

    tools/gccdg/fetch.sh && tools/gccdg/wrap.sh
    tools/gccdg/sweep.sh small          # ~5 min: what compiles, and the first error of what doesn't
    tools/gccdg/run.sh small            # builds + DOSBox; diff against baseline-small.txt
    ONLY="torture__pr101009 pr38902" tools/gccdg/run.sh small   # a few by id

Reading the results:
- `sweep.txt` stage `qbe` = minic emitted SSA that QBE rejects → a minic
  codegen bug by definition (the first thing to look at after a change).
- `minic-crash` / `qbe-crash` = a compiler crash; reduce and gate.
- `minic` parse errors are mostly GNU-only material (preprocessed glibc
  headers, nested functions, `_BitInt`, `_Complex`, vector types,
  `__int128`, x86 `fenv_t`) — noise for this target, but a class that
  shows up many times is worth a look.
- `results.txt` `ABORT`/`FAIL` on a test that compiled cleanly is either a
  miscompile or a test that silently assumes a 32-bit `int` (no dg marker,
  e.g. a `0x7fffffff` constant).  Check the source before chasing it; the
  baseline records the accepted state so only CHANGES are loud.

What the first sweep found (2026-10-10, see NEXT_SESSION.md §9g): a
sub-int bitfield class bug, two compiler crashes (array-of-struct brace
init, 1024-byte string init), and a char-arithmetic promotion bug — each
now a bug-loud probe in `tools/test-dos.sh`.

Note: the classic `gcc.c-torture/execute` suite is NOT in EDG's import
(it lives in GCC itself); it is the natural next corpus to point these
tools at (same `wrap.sh` driver, different source tree).
