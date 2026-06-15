#!/usr/bin/env python3
"""near_to_far_rt.py — convert a near-form standalone runtime asm TU to the
medium-model far-call ABI (§7p, Phase-6 libstub retirement).

minic/dos/qbe_rt.asm and minic/dos/dos_syscall.asm are authored in NEAR form
(plain `ret`, args at [bp+4]) so the small-model libstub-free link assembles
them raw.  Under the MEDIUM model the compiler emits a far call to these
helpers (verified: `call far _qbe_div32u`, `call far _qbe_rem32u`,
`call far _int86`), which pushes a 4-byte CS:IP return address and returns via
`retf`.  This script applies the same mechanical rewrite that
tools/libstub_to_exe.py performs on libstub.asm, so the near files stay the
single source of truth and the medium variant is generated at build time:

  - `ret` (bare, optional trailing comment) -> `retf`.
  - every positive `[bp+N]` -> `[bp+N+2]` (the far return CS occupies an extra
    word between saved bp and the first incoming arg).  `[bp-N]` locals are
    untouched.  `[bx+N]` / `[cs:...]` (pointer derefs, SMC) are untouched.
  - the near `_TEXT` code segment is renamed to a UNIQUE far-code segment
    (`--seg-name`) so omf_link keeps it in its own paragraph (its own CS);
    near-code `_TEXT` would otherwise coalesce into the single small-model
    frame.  Matches asm_to_omf.py's per-module `<BASE>_TEXT` far-code naming.

Near data is identical across the two models, so the BSS heap TU (heap.asm)
does NOT go through this — only the two pure-code runtime TUs.

Usage: near_to_far_rt.py --seg-name=QBE_RT_TEXT <in.asm> <out.asm>
"""
import re
import sys


def shift_bp_offset(line):
    """Bump every `[bp+N]` (N>=0) by +2 so far-call args land correctly.
    Mirrors tools/libstub_to_exe.py.  Only `[bp+...]` matches, so `[bx+N]`
    pointer derefs and `[cs:...]` SMC references are left alone."""
    def repl(m):
        return '[bp+{}]'.format(int(m.group(1)) + 2)
    return re.sub(r'\[bp\+(\d+)\]', repl, line)


def transform(line):
    """Convert a single instruction line from near to far form."""
    stripped = line.lstrip()
    indent = line[:len(line) - len(stripped)]
    # Bare `ret` (optionally with a trailing comment) -> `retf`.  A comment
    # line (`; ...`) never matches, nor does `retf`, `ret <imm>`, or a label.
    m = re.match(r'^ret\b(?!\w)(.*)$', stripped)
    if m:
        return indent + 'retf' + m.group(1)
    return shift_bp_offset(line)


def main():
    args = sys.argv[1:]
    seg_name = None
    while args and args[0].startswith('--'):
        a = args.pop(0)
        if a.startswith('--seg-name='):
            seg_name = a[len('--seg-name='):]
        else:
            print('near_to_far_rt: unknown option: ' + a, file=sys.stderr)
            sys.exit(2)
    if seg_name is None or len(args) != 2:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    in_path, out_path = args

    with open(in_path) as f:
        lines = f.read().split('\n')

    out = []
    for line in lines:
        # Rename the near `_TEXT` code segment to the unique far-code name.
        # Both the `segment _TEXT class=CODE ...` declaration and any bare
        # `segment _TEXT` re-open are rewritten; `_DATA`/`_BSS` (none here)
        # and the `_TEXT` token elsewhere are left alone.
        m = re.match(r'^(\s*segment\s+)_TEXT\b(.*)$', line)
        if m:
            out.append('{}{}{}'.format(m.group(1), seg_name, m.group(2)))
            continue
        out.append(transform(line))

    with open(out_path, 'w') as f:
        f.write('\n'.join(out))


if __name__ == '__main__':
    main()
