#!/usr/bin/env python3
"""check_emit_brackets.py — §4y emit-bracket audit checker.

Consumes i8086 asm generated with QBE_EMIT_CHK=1 (see i8086/emit.c): every
IR instruction is preceded by `; CHK <op> cls=<n> to=<dest> live=<regs>`
carrying the exact post-rega GPR live-after set, and every terminator by
`; CHKT <type> live=<regs>`.

For each marked region (marker .. next marker/block label) the checker
symbolically executes the emitted instructions and reports any register in
the live set — other than the instruction's destination (or the ABI
caller-save set for calls) — whose final value is not the region-entry
value.  ES and DS must hold their entry values at the end of EVERY region
(the DGROUP invariants), live or not.

The simulator is deliberately simple: linear scan (conditional jumps inside
a region are treated as fallthrough — emit brackets are always straight-
line), symbolic register values, a push/pop stack, and tracked direct
[bp+N] memory cells so slot-roundtrip saves are recognized.  Calls clobber
AX/CX/DX and preserve BX/SI/DI/ES/DS (the inductive cdecl invariant).

Usage:  check_emit_brackets.py file.asm [file2.asm ...]
        (or:  QBE_EMIT_CHK=1 qbe -t i8086 -m compact f.ssa | check_emit_brackets.py -)
Exit 1 if any violation found.
"""
import re
import sys
from collections import defaultdict

GPRS = ("ax", "cx", "dx", "bx", "si", "di")
SEGS = ("es", "ds")
TRACKED = GPRS + SEGS
SUB8 = {"al": "ax", "ah": "ax", "bl": "bx", "bh": "bx",
        "cl": "cx", "ch": "cx", "dl": "dx", "dh": "dx"}

CHK_RE = re.compile(r";\s*CHK\s+(\S+)\s+cls=(\d+)\s+to=(\S+)\s+live=(\S+)")
CHKT_RE = re.compile(r";\s*CHKT\s+(\d+)\s+live=(\S+)")
LABEL_RE = re.compile(r"^([A-Za-z_$][\w$.]*):")
BPMEM_RE = re.compile(r"^\[bp([+-]\d+)\]$")

unknown_mnemonics = set()


class Sim:
    def __init__(self):
        self.fresh_n = 0
        self.regs = {r: ("entry", r) for r in TRACKED}
        self.stack = []          # list of symbolic values
        self.mem = {}            # "[bp+N]" -> symbolic value

    def fresh(self, why=""):
        self.fresh_n += 1
        return ("val", self.fresh_n, why)

    def value_of(self, operand):
        op = operand.strip()
        if op in TRACKED:
            return self.regs[op]
        if op in SUB8:
            return self.fresh("sub8:" + op)   # half a reg: not the full value
        m = BPMEM_RE.match(op)
        if m and op in self.mem:
            return self.mem[op]
        return self.fresh("src:" + op)

    def write_reg(self, reg, val):
        if reg in TRACKED:
            self.regs[reg] = val
        elif reg in SUB8:
            self.regs[SUB8[reg]] = self.fresh("sub8w:" + reg)

    def clobber(self, *regs):
        for r in regs:
            self.regs[r] = self.fresh("clobber:" + r)

    def mem_invalidate(self):
        self.mem.clear()


def split_operands(rest):
    # strip size keywords and segment overrides for operand identification
    rest = re.sub(r"\b(word|byte|dword|short|near|far)\b", " ", rest)
    parts = [p.strip() for p in rest.split(",")]
    return [re.sub(r"\s+", "", p) for p in parts if p.strip()]


def norm_mem(op):
    # normalize "[bp-22]" / "[bp+6]" forms; None for anything else
    op = op.replace(" ", "")
    if BPMEM_RE.match(op):
        return op
    return None


def step(sim, line):
    """Symbolically execute one asm line."""
    code = line.split(";", 1)[0].strip()
    if not code or code.endswith(":") or code.startswith("/*"):
        return
    toks = code.split(None, 1)
    mn = toks[0].lower()
    rest = toks[1] if len(toks) > 1 else ""

    if mn in ("jmp", "je", "jne", "jl", "jle", "jg", "jge", "jb", "jbe",
              "ja", "jae", "jz", "jnz", "js", "jns", "jc", "jnc", "jo",
              "jno", "jcxz", "loop", "loopz", "loopnz"):
        return  # linear scan: control flow ignored
    if mn in ("test", "cmp", "nop"):
        return
    if mn == "push":
        sim.stack.append(sim.value_of(rest))
        return
    if mn == "pop":
        v = sim.stack.pop() if sim.stack else sim.fresh("pop-underflow")
        sim.write_reg(rest.strip(), v)
        return
    if mn == "xchg":
        ops = split_operands(rest)
        if len(ops) == 2 and ops[0] in TRACKED and ops[1] in TRACKED:
            sim.regs[ops[0]], sim.regs[ops[1]] = sim.regs[ops[1]], sim.regs[ops[0]]
        else:
            for o in ops:
                sim.write_reg(o, sim.fresh("xchg"))
        return
    if mn == "mov":
        ops = split_operands(rest)
        if len(ops) != 2:
            return
        dst, src = ops
        if dst.startswith("["):
            key = norm_mem(dst)
            if key:
                sim.mem[key] = sim.value_of(src)
            else:
                sim.mem_invalidate()   # indirect write may alias bp slots
            return
        sim.write_reg(dst, sim.value_of(src))
        return
    if mn in ("add", "sub"):
        ops = split_operands(rest)
        if ops and ops[0] == "sp":
            # arg cleanup / frame carve: adjust the symbolic stack
            try:
                n = int(ops[1], 0) // 2
            except (ValueError, IndexError):
                n = len(sim.stack)
            if mn == "add":
                del sim.stack[max(0, len(sim.stack) - n):]
            else:
                sim.stack.extend(sim.fresh("subsp") for _ in range(n))
            return
        # fall through to generic two-operand arithmetic
    if mn in ("add", "sub", "adc", "sbb", "and", "or", "xor", "shl", "shr",
              "sar", "sal", "rol", "ror", "rcl", "rcr", "lea"):
        ops = split_operands(rest)
        if not ops:
            return
        dst = ops[0]
        if dst.startswith("["):
            key = norm_mem(dst)
            if key:
                sim.mem[key] = sim.fresh("rmw")
            else:
                sim.mem_invalidate()
            return
        sim.write_reg(dst, sim.fresh(mn))
        return
    if mn in ("inc", "dec", "neg", "not"):
        dst = split_operands(rest)[0] if rest else ""
        if dst.startswith("["):
            key = norm_mem(dst)
            if key:
                sim.mem[key] = sim.fresh(mn)
            else:
                sim.mem_invalidate()
        else:
            sim.write_reg(dst, sim.fresh(mn))
        return
    if mn in ("mul", "imul", "div", "idiv"):
        ops = split_operands(rest)
        if len(ops) >= 2:           # two-operand imul
            sim.write_reg(ops[0], sim.fresh(mn))
        else:
            sim.clobber("ax", "dx")
        return
    if mn == "cwd":
        sim.clobber("dx")
        return
    if mn == "cbw":
        sim.clobber("ax")
        return
    if mn == "call":
        sim.clobber("ax", "cx", "dx")   # caller-save; BX/SI/DI/ES/DS preserved
        return
    if mn in ("ret", "retf", "iret"):
        return
    if mn == "les":
        ops = split_operands(rest)
        if ops:
            sim.write_reg(ops[0], sim.fresh("les"))
        sim.clobber("es")
        return
    if mn == "lds":
        ops = split_operands(rest)
        if ops:
            sim.write_reg(ops[0], sim.fresh("lds"))
        sim.clobber("ds")
        return
    if mn in ("stosb", "stosw"):
        sim.clobber("di")
        return
    if mn in ("lodsb", "lodsw"):
        sim.clobber("si", "ax")
        return
    if mn in ("movsb", "movsw"):
        sim.clobber("si", "di")
        return
    if mn == "rep" or mn.startswith("rep"):
        sim.clobber("cx", "si", "di")
        return
    if mn in ("std", "cld", "sti", "cli", "clc", "stc"):
        return
    unknown_mnemonics.add(mn)


class Region:
    def __init__(self, kind, op, dest, live, fname, lineno, func):
        self.kind = kind          # "ins" | "term"
        self.op = op
        self.dest = dest          # "ax".."di" | "slot" | "-" | "R?"
        self.live = live          # set of reg names
        self.fname = fname
        self.lineno = lineno
        self.func = func
        self.lines = []


def parse_live(s):
    return set() if s == "-" else set(s.split(","))


# ops whose dest may legitimately end up equal to its entry value (identity
# moves the emitter elides or forwards)
DEST_ENTRY_OK = {"copy", "swap", "cast", "addr", "load", "extuw", "extsw",
                 "extub", "extsb", "extuh", "extsh"}


def check_region(reg, report):
    if reg.op == "asm":
        # user inline assembly: clobbers are declared in the C source's
        # clobber list, which the marker doesn't carry — out of scope.
        return
    sim = Sim()
    for ln in reg.lines:
        step(sim, ln)
    if reg.op == "swap":
        # rega's cycle-resolving exchange: the correct outcome is exactly
        # two GPRs holding each other's entry values.  Anything else —
        # including an EMPTY region (a silently dropped slot swap, the
        # §4w Oswap limitation) — is flagged.
        changed = [r for r in GPRS if sim.regs[r] != ("entry", r)]
        ok = (len(changed) == 2
              and sim.regs[changed[0]] == ("entry", changed[1])
              and sim.regs[changed[1]] == ("entry", changed[0]))
        if not ok:
            report[("swap", "exchange" if reg.lines else "DROPPED")].append(reg)
        for r in SEGS:
            if sim.regs[r] != ("entry", r):
                report[(reg.op, r)].append(reg)
        return
    if reg.kind == "ins" and (reg.op.startswith("call") or reg.op == "vacall"):
        allowed = {"ax", "cx", "dx"}
    elif reg.dest in GPRS:
        allowed = {reg.dest}
    else:
        allowed = set()
    for r in sorted(reg.live - allowed):
        if r in GPRS and sim.regs[r] != ("entry", r):
            report[(reg.op, r)].append(reg)
    for r in SEGS:
        if sim.regs[r] != ("entry", r):
            report[(reg.op, r)].append(reg)
    # dest-destroyed rule (the §4x Ocmps CX shape): a non-empty region whose
    # REGISTER dest ends holding its entry value computed nothing — usually a
    # save bracket popping over the just-stored result.
    if (reg.dest in GPRS and reg.lines
            and reg.op not in DEST_ENTRY_OK
            and sim.regs[reg.dest] == ("entry", reg.dest)):
        report[(reg.op, reg.dest + "(dest)")].append(reg)


def run_file(path, report):
    if path == "-":
        lines = sys.stdin.readlines()
    else:
        with open(path, errors="replace") as fh:
            lines = fh.readlines()
    cur = None
    func = "?"
    nregions = 0
    for n, raw in enumerate(lines, 1):
        line = raw.rstrip("\n")
        s = line.strip()
        m = CHK_RE.search(s)
        mt = CHKT_RE.search(s) if not m else None
        lbl = LABEL_RE.match(line) if not line.startswith((" ", "\t")) else None
        if m or mt or lbl:
            if cur:
                check_region(cur, report)
                nregions += 1
                cur = None
            if lbl:
                name = lbl.group(1)
                if not name.startswith((".", "l")) or not re.match(r"^l\d", name):
                    if not name.startswith("."):
                        func = name
                continue
            if m:
                cur = Region("ins", m.group(1), m.group(3),
                             parse_live(m.group(4)), path, n, func)
            else:
                cur = Region("term", "jmp" + mt.group(1), "-",
                             parse_live(mt.group(2)), path, n, func)
            continue
        if cur is not None and s and not s.startswith((";", ".")):
            cur.lines.append(line)
    if cur:
        check_region(cur, report)
        nregions += 1
    return nregions


def main():
    paths = sys.argv[1:]
    if not paths:
        print(__doc__)
        return 2
    report = defaultdict(list)
    total = 0
    for p in paths:
        total += run_file(p, report)
    print(f"checked {total} regions in {len(paths)} file(s)")
    if unknown_mnemonics:
        print("unknown mnemonics (treated as no-write):",
              ", ".join(sorted(unknown_mnemonics)))
    if not report:
        print("OK: no live-register clobbers found")
        return 0
    print(f"\n{sum(len(v) for v in report.values())} violation(s), "
          f"{len(report)} distinct (op, reg) classes:\n")
    for (op, r), regions in sorted(report.items(),
                                   key=lambda kv: -len(kv[1])):
        print(f"  [{len(regions):4d}] op={op:<12} clobbers live {r}")
        for ex in regions[:3]:
            print(f"          {ex.fname}:{ex.lineno}  fn={ex.func} "
                  f"dest={ex.dest} live={{{','.join(sorted(ex.live))}}}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
