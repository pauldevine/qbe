# C-Kermit (~/projects/ckermit, Victor port) under the qbe/minic 8086 toolchain

Method: preprocess the 24 modules of the real `victorow.mak` build with Open
Watcom's own `wcc -p` (same flags, `-fi=ckvictor.h`), strip Watcom-isms, then
minic -m large -> qbe -t i8086 -> asm_to_omf -> nasm.  Compile-only (no link).
Scripts (tools/ckermit/, see README.md): `sweep.sh` (driver), `fixups.pl` (text workarounds, one per gap),
`splitdecl.pl`, `one.sh`/`it.sh`/`m.sh`/`locate.py` (diagnosis), `omfsize.py`.

## Result: 24/24 modules compile end-to-end, but the output cannot link/run yet

### Hard blockers (backend / layout)
- B1  `wart()` (ckcpro, the protocol state machine) compiles to 68,873 bytes
      of code -- over the 64 KB single-segment limit.  Watcom's whole ckcpro
      module is 17 KB.
- B2  DGROUP overflow: `_DATA`+`_BSS` total 78,000 B against the 64 KB cap
      (Watcom fits in 48.9 KB by putting strings in code with -zc and large
      objects in far segments with -zt).
- B3  Code density: large model 557 KB of code vs Watcom -os 160 KB (3.5x).
      Medium on the same three modules: 1.8x.  The difference is far-data
      global access: every global load/store is
      push bx/push es/mov ax,seg X/mov es,ax/mov bx,X/op es:[bx]/pop es/pop bx
      where Watcom (DGROUP-resident scalars, DS=DGROUP) emits `mov [X],r`.
      Smaller contributors: compare not fused into branch (cmp/mov dx,1/je/
      mov dx,0/test/jnz), switch lowered to a linear compare chain, 335
      duplicated epilogues in wart().

### Frontend gaps fixed in minic.y (gated: ckermit_decl_probe, 5 models)
- NGlo 512 -> 8192 ("too many globals")
- block-scope `extern T *g;` of an INITIALIZED global: "double definition"
  (varaddextern tested glo == 1; initialized globals store their slot index)
- comma-operator expression statement `a++, b--;`
- comma operator in `while (...)` / `if (...)` conditions
- block-scope array shadowing a file-scope name: "double definition"
- diagnostic: die() now reports the statement line for emit-time errors

### Frontend gaps worked around in fixups.pl (not fixed)
- G1  non-pointer function typedef `typedef void f(int,int);`
- G2  function returning function pointer `void (*signal(int, void(*)(int)))(int)`
- G3  `extern struct T *fn(...);`
- G4  `extern T *name[N];` / multi `extern T *a[], *b[];` / `extern char **a, **b;`
      (ext_decl `'*' IDENT` steals the star; one star only)
- G5  abstract fn-ptr parameter `int (*)(char)` in a prototype
- G6  abstract array parameters `char *[]`, `char []`, `struct T[]`
- G7  `long double`
- G11 statement-scope function prototypes (`{ ...; int f(int); }`)
- G12 2-D arrays `char a[N][M]` (rewritten to the aoa array-typedef path)
- G13 statement-scope multi-name extern `extern int a, b;`
- G15 block-scope `char *p = 0, *q = f;` / `const char *p, *a[4];`
- G16 statement-scope `extern char x[];`
- G19 unnamed bitfields `unsigned :16;`
- G20 parenthesized parameter declarator `T (name)`
- G21 `void (__far *h)()`
- G23 east-const `void const *`
- G24 struct-typed `?:` as an assignment source ("invalid lvalue")
- G8  file-scope `int a = 1, b = 2;` (first declarator initialized)
Watcom-isms (a real port would use our headers / crt0): `__declspec(__watcall)`,
`__int64`, `__based`, `__interrupt`, `seg :> off`, the XI init-table record.

### Link surface
84 external symbols not defined by C-Kermit itself (undefined-watcom.txt): mostly
POSIX/stdio (open/read/stat/opendir/localtime/signal/setjmp/...), plus Watcom
internals pulled in by Watcom headers (___iob, __IsTable, ___get_errno_ptr).
