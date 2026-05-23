#!/usr/bin/env python3
"""libstub_prune.py — pull only referenced chunks out of libstub.asm.

The .COM concat pipeline (tools/build-stevie.sh --com) concatenates
the whole libstub.asm into the final flat binary even when most of the
symbols are never called.  For small .COM utilities (and for stevie at
the 64KB tiny-model ceiling), every kilobyte counts.

Usage:
    libstub_prune.py <libstub.asm> <out.asm> [<per-tu.asm>...]

The tool parses libstub.asm into per-symbol chunks (each `global _foo`
starts a new chunk; chunk body extends through the lines before the
next `global` directive), computes the transitive closure of symbols
referenced from the supplied per-TU asm files, and writes only the
chunks in the closure to <out.asm>.

Headers/comments at the top of libstub.asm (before the first `global`)
are always emitted.

The matching is conservative: false-positive references (e.g., a
symbol mentioned in a comment) just keep an extra chunk; false
negatives would break the build.
"""

import re
import sys
from pathlib import Path


_GLOBAL_RE = re.compile(r"^\s*global\s+(.+?)\s*(?:;.*)?$")
_LABEL_RE = re.compile(r"^(_[A-Za-z_][A-Za-z0-9_]*)\b")
_SYM_RE = re.compile(r"\b(_[A-Za-z_][A-Za-z0-9_]*)\b")


def _strip_comment(line: str) -> str:
    out = []
    in_str = None
    for ch in line:
        if in_str:
            out.append(ch)
            if ch == in_str:
                in_str = None
        elif ch in ("'", '"', "`"):
            in_str = ch
            out.append(ch)
        elif ch == ";":
            break
        else:
            out.append(ch)
    return "".join(out)


def parse_libstub(path: Path):
    """Return (header_lines, chunks).

    header_lines: list[str] from top of file up to (but not including)
        the first `global` directive.
    chunks: list[dict] each with:
        - 'globals': set[str] of symbol names declared via `global`
        - 'lines'  : list[str] of body lines (includes the `global` line(s))
        - 'defines': set[str] of symbols defined inside the body
            (i.e. any `^_foo:` or `^_foo  dw/db/dd/resb` token at column 0)
        - 'refs'   : set[str] of symbols referenced in the body
            (excluding ones defined within this chunk)
    """
    lines = path.read_text().splitlines(keepends=False)
    header = []
    chunks = []
    cur = None

    for raw in lines:
        mg = _GLOBAL_RE.match(raw)
        if mg:
            # A `global` line starts a new chunk unless the previous chunk
            # has not yet seen any non-global content — in which case we
            # treat adjacent `global` lines as additional globals of that
            # same chunk.
            syms = [s.strip() for s in mg.group(1).split(",") if s.strip()]
            if cur is not None and all(
                _GLOBAL_RE.match(l) or l.strip() == "" or l.strip().startswith(";")
                for l in cur["lines"]
            ):
                # No body yet — merge into the current chunk.
                cur["globals"].update(syms)
                cur["lines"].append(raw)
                continue
            # Otherwise, start a new chunk.
            cur = {
                "globals": set(syms),
                "lines": [raw],
                "defines": set(),
                "refs": set(),
            }
            chunks.append(cur)
            continue

        if cur is None:
            header.append(raw)
            continue

        cur["lines"].append(raw)

        # Track top-of-line label/data definitions: `_foo:` or `_foo  dw 0`.
        ml = _LABEL_RE.match(raw)
        if ml:
            # Only count it as a definition if the line really does define
            # a symbol — either `:` after the name or a data directive token.
            rest = raw[ml.end():].lstrip()
            if rest.startswith(":") or re.match(
                r"(db|dw|dd|dq|resb|resw|resd|equ)\b", rest, re.IGNORECASE
            ):
                cur["defines"].add(ml.group(1))

    # Second pass: collect refs (after we know what each chunk defines).
    for chunk in chunks:
        for raw in chunk["lines"]:
            stripped = _strip_comment(raw)
            for m in _SYM_RE.finditer(stripped):
                sym = m.group(1)
                if sym in chunk["defines"]:
                    continue
                if sym in chunk["globals"]:
                    continue
                chunk["refs"].add(sym)

    return header, chunks


def collect_external_refs(per_tu_paths):
    refs = set()
    for p in per_tu_paths:
        for raw in Path(p).read_text().splitlines():
            stripped = _strip_comment(raw)
            for m in _SYM_RE.finditer(stripped):
                refs.add(m.group(1))
    return refs


def closure(chunks, header_defines, seed_refs):
    """Return the set of chunk indices in the transitive closure of seed_refs.

    header_defines: symbols defined in the header (none today, but allowed).
    """
    # Build symbol → chunk index.
    sym_owner = {}
    for i, c in enumerate(chunks):
        for s in c["globals"] | c["defines"]:
            sym_owner.setdefault(s, i)

    needed = set()
    queue = list(seed_refs)
    while queue:
        sym = queue.pop()
        if sym in header_defines:
            continue
        i = sym_owner.get(sym)
        if i is None or i in needed:
            continue
        needed.add(i)
        for r in chunks[i]["refs"]:
            queue.append(r)
    return needed


def main(argv):
    if len(argv) < 3:
        print("usage: libstub_prune.py <libstub.asm> <out.asm> [<tu.asm>...]",
              file=sys.stderr)
        return 2

    libstub = Path(argv[1])
    out_path = Path(argv[2])
    per_tu = [Path(p) for p in argv[3:]]

    header, chunks = parse_libstub(libstub)
    seed = collect_external_refs(per_tu)
    keep = closure(chunks, set(), seed)

    out_lines = list(header)
    out_lines.append("; ---- pruned by tools/libstub_prune.py ----")
    out_lines.append(f"; kept {len(keep)}/{len(chunks)} chunks")
    out_lines.append("")
    for i, c in enumerate(chunks):
        if i not in keep:
            continue
        names = ",".join(sorted(c["globals"]))
        out_lines.append(f"; ---- chunk {i}: {names} ----")
        out_lines.extend(c["lines"])
    out_path.write_text("\n".join(out_lines) + "\n")

    # Diagnostic summary to stderr.
    kept_syms = sum(len(chunks[i]["globals"]) for i in keep)
    total_syms = sum(len(c["globals"]) for c in chunks)
    sys.stderr.write(
        f"libstub_prune: kept {len(keep)}/{len(chunks)} chunks "
        f"({kept_syms}/{total_syms} globals)\n"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
