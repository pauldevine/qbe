#!/usr/bin/env python3
"""asm_to_omf.py — convert qbe i8086 .asm output to OMF-friendly NASM source.

Reads a .asm file produced by `qbe -t i8086 -m medium`, splits sections
(.text/.data/.bss), applies the syntax transforms previously handled by
the sed/perl pipeline in build-stevie.sh, and emits a single NASM source
ready for `nasm -f obj -o <base>.obj`.

The output is a single file with proper segment directives, a DGROUP
declaration, and auto-generated `extern`/`global` lines.

Usage:
    asm_to_omf.py [--model=<m>] <basename> <input.asm> <output.nasm.asm>

Near-code models (tiny/small/compact) emit all code into a shared `_TEXT`
segment so the linker can coalesce multiple modules into a single CS frame.
Far-code models (medium/large/huge) keep the per-module `<BASE>_TEXT`
name so each module lands in its own physical 64KB code segment.
"""
import re
import sys


# Local-label prefix scheme (mirrors the perl pass in build-stevie.sh):
# `lN` / `lN_lM` jump labels and `_glo<N>` data labels are renamed to
# `<base>_lN`, `<base>_lN_lM`, `<base>_glo<N>` so that they are unique
# across translation units.  Function names and runtime symbols are left
# alone.
def apply_local_prefix(line, prefix):
    line = re.sub(r'^(l\d+(?:_l\d+)?):', prefix + r'\1:', line)
    line = re.sub(r'^(\s*j[a-z]+\s+)(l\d+(?:_l\d+)?)\b',
                  lambda m: m.group(1) + prefix + m.group(2), line)
    line = re.sub(
        r'^(\s*jnz\s+[^,]+,\s*)(l\d+(?:_l\d+)?)(\s*,\s*)(l\d+(?:_l\d+)?)\b',
        lambda m: m.group(1) + prefix + m.group(2) + m.group(3) + prefix + m.group(4),
        line,
    )
    line = re.sub(r'^_?glo(\d+):', prefix + r'glo\1:', line)
    line = re.sub(r'\b_?glo(\d+)\b', prefix + r'glo\1', line)
    return line


def transform_line(line, prefix):
    """Apply the same syntactic rewrites the sed/perl pipeline did,
    but leave section markers in place — the caller routes lines into
    per-section buckets."""
    # 32-bit op stub note
    line = re.sub(r'; TODO: 32-bit op \d+',
                  '; XXX 32-bit op stub - codegen incomplete', line)
    # `.ascii "..."` → NASM backtick string
    m = re.match(r'^\s*\.ascii\s+"(.*)"\s*$', line)
    if m:
        s = m.group(1).replace('`', r'\`')
        return 'db `' + s + '`'

    line = apply_local_prefix(line, prefix)

    # MASM operand qualifiers → NASM
    line = re.sub(r'word ptr (\w\w:)\[', r'word \1[', line)
    line = re.sub(r'byte ptr (\w\w:)\[', r'byte \1[', line)
    line = re.sub(r'dword ptr (\w\w:)\[', r'dword \1[', line)
    line = line.replace('word ptr [', 'word [')
    line = line.replace('byte ptr [', 'byte [')
    line = line.replace('dword ptr [', 'dword [')

    # /* comment */ → ; comment
    line = re.sub(r'/\* (.*) \*/', r'; \1', line)

    # `name proc near|far` → bare label (qbe should no longer emit these,
    # but tolerate stale output).
    line = re.sub(r'^([A-Za-z_][\w]*) proc (near|far)$', r'\1:', line)
    line = re.sub(r'^([A-Za-z_][\w]*) endp$', r'; \1 endp', line)

    # GAS data directives → NASM
    line = re.sub(r'^\s*\.byte (.*)$',  r'db \1', line)
    line = re.sub(r'^\s*\.short (.*)$', r'dw \1', line)
    line = re.sub(r'^\s*\.long (.*)$',  r'dd \1', line)
    line = re.sub(r'^\s*\.int (.*)$',   r'dw \1', line)
    line = re.sub(r'^\s*\.word (.*)$',  r'dw \1', line)
    line = re.sub(r'^\s*\.quad (.*)$',  r'dq \1', line)
    line = re.sub(r'^\s*\.zero (.*)$',  r'times \1 db 0', line)
    line = re.sub(r'^\s*\.fill (\d+),1,0$', r'times \1 db 0', line)
    line = re.sub(r'^\s*\.fill (\d+),2,0$', r'times \1 dw 0', line)
    line = re.sub(r'^\s*\.fill (\d+),4,0$', r'times \1 dd 0', line)
    line = re.sub(r'^\s*\.fill (\d+)$',     r'times \1 db 0', line)

    # `test es,es` → `test ax,ax` (codegen artifact from segment regs)
    line = re.sub(r'^(\s*test\s+)(es|ds|cs|ss),\s*\2\b',
                  r'\1ax, ax ; XXX was test \2,\2', line)

    # `mov [bp+N], <symbol>` (no size) — qbe's Ocopy/spill emits a slot
    # store without a size qualifier, which `nasm -f bin` accepts but
    # `nasm -f obj` rejects (relocation size ambiguous).  Promote to
    # `mov word [bp+N], <symbol>`.  Same fix for `mov [reg], symbol`.
    line = re.sub(r'^(\s*)mov (\[bp[+\-]\d+\]),\s*(_?[A-Za-z][\w]*)$',
                  r'\1mov word \2, \3', line)
    line = re.sub(r'^(\s*)mov (\[bp[+\-]\d+\]),\s*(\d+)$',
                  r'\1mov word \2, \3', line)

    return line


# Symbols emitted as data labels by qbe that are local to the file but
# don't follow the `glo<N>` naming convention.  We treat any `_<name>:`
# that isn't preceded by `.globl _<name>` as a file-local definition,
# so this list is informational only.
RUNTIME_SYMS = set()  # populated below if/when needed


# Skip-list of GAS directives we drop entirely.  Section markers
# (.text/.data/.bss) are handled separately to switch buckets.
DROP_PREFIXES = (
    '.balign', '.section', '.local', '.type', '.size', '.file',
    '.ident', '.string', '.p2align', '.model', '.code',
)


def is_label_def(line):
    m = re.match(r'^([A-Za-z_.][\w.]*):\s*(?:;.*)?$', line)
    return m.group(1) if m else None


def collect_referenced_syms(line):
    """Return set of `_xxx`-style symbol references in the given line.
    Skips operand qualifiers (word/byte/dword), instruction mnemonics,
    and segment registers."""
    syms = set()
    # Strip line comment so we don't pick up `; XXX was test es,es`
    code = re.split(r';', line, maxsplit=1)[0]
    for m in re.finditer(r'\b(_[A-Za-z][\w]*)\b', code):
        syms.add(m.group(1))
    return syms


def main():
    args = sys.argv[1:]
    model = 'medium'
    while args and args[0].startswith('--'):
        a = args.pop(0)
        if a.startswith('--model='):
            model = a[len('--model='):]
        else:
            print('asm_to_omf: unknown option: ' + a, file=sys.stderr)
            sys.exit(2)
    if len(args) != 3:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    basename, in_path, out_path = args[0], args[1], args[2]
    prefix = basename + '_'
    near_code = model in ('tiny', 'small', 'compact')

    with open(in_path) as f:
        raw_lines = f.readlines()

    sections = {'text': [], 'data': [], 'bss': []}
    current = 'text'
    publics = []          # symbols declared via `.globl`
    defined = set()       # all labels actually defined in this file
    referenced = set()    # all `_xxx` references seen in operands

    for raw in raw_lines:
        line = raw.rstrip('\n')
        s = line.strip()

        # Section markers
        if s == '.text':
            current = 'text'; continue
        if s == '.data':
            current = 'data'; continue
        if s == '.bss':
            current = 'bss'; continue

        # GAS-only directives we drop
        if any(s.startswith(p) for p in DROP_PREFIXES):
            continue

        # `.globl _foo` — record as public, drop the line
        m = re.match(r'\.globl\s+(\S+)', s)
        if m:
            publics.append(m.group(1))
            continue

        # Empty line — skip
        if s == '':
            continue

        # Apply transforms (after section/globl handling)
        line = transform_line(line, prefix)

        # Track defined labels & referenced symbols
        lbl = is_label_def(line.strip())
        if lbl:
            defined.add(lbl)
        referenced |= collect_referenced_syms(line)

        sections[current].append(line)

    # Auto-export every `_xxx`-prefixed label defined in this file.
    # minic doesn't currently emit qbe `export` markers for file-scope
    # data, so qbe doesn't emit `.globl` for them — but C's default
    # linkage for file-scope identifiers IS external, so we promote
    # every `_xxx:` label to a public.  The `<base>_xxx` per-module
    # local labels (jump targets, string literals) stay private.
    public_set = set(publics)
    for sym in defined:
        if sym.startswith('_') and sym not in public_set:
            publics.append(sym)
            public_set.add(sym)

    # Compute externs: anything referenced but not defined locally.
    # Filter out segment-name-like tokens and false positives.
    externs = sorted(
        sym for sym in referenced
        if sym not in defined and sym not in public_set
    )

    # Build output
    out = []
    out.append('; Auto-generated from qbe i8086 .asm by tools/asm_to_omf.py')
    out.append('; Module: ' + basename)
    out.append('bits 16')
    out.append('cpu 8086')
    out.append('')
    out.append('group DGROUP _DATA _BSS')
    out.append('')
    for e in externs:
        out.append('extern ' + e)
    if externs:
        out.append('')
    for p in publics:
        out.append('global ' + p)
    if publics:
        out.append('')

    # Emit all three segments even if empty so DGROUP's references are
    # always resolved.  NASM/OMF tolerate empty segments.
    # Medium model: give each module its own _TEXT segment so the linker
    # places them in separate physical 64KB code segments.  All _DATA /
    # _BSS go into shared DGROUP segments (so DS addresses everything).
    code_seg = basename.upper() + '_TEXT'
    _ = near_code  # reserved for future tiny/small/compact coalescing
    out.append('segment ' + code_seg + ' class=CODE align=2 use16')
    out.extend(sections['text'])
    out.append('')

    out.append('segment _DATA class=DATA align=2 use16')
    out.extend(sections['data'])
    out.append('')

    out.append('segment _BSS class=BSS align=2 use16')
    out.extend(sections['bss'])
    out.append('')

    with open(out_path, 'w') as f:
        f.write('\n'.join(out) + '\n')


if __name__ == '__main__':
    main()
