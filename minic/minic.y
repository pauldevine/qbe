%{

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	NString = 128,  /* max identifier length; MicroPython has 49-char names
	                 * (e.g. MP_MAP_LOOKUP_ADD_IF_NOT_FOUND_OR_REMOVE_IF_FOUND)
	                 * and longer generated qstr symbols. Host-only memory. */
	NGlo = 512,
	/* varh[] is an open-addressing table holding all globals + enum
	 * constants (never cleared) plus the current function's locals.
	 * MicroPython pulls ~214 MP_QSTR_* enum constants from genhdr alone,
	 * plus hundreds of `extern const mp_obj_type_t` globals, so 512
	 * overflowed ("too many variables") on 62/132 spike files.  Keep the
	 * load factor low for the linear-probe chain repair; host-only memory
	 * (~140 B/slot).  A full REPL build generates still more qstrs. */
	NVar = 4096,
	NStr = 256,
};

enum { /* minic types */
	NIL,
	CHR,
	INT,
	LNG,
	PTR,
	FUN,
	STRUCT_T,  /* struct */
	UNION_T,   /* union */
};

/* DOS memory model — mirrors qbe's enum MemModel.  Selected via -m on
 * the command line.  Default = small (matches Path A behaviour). */
enum {
	MTiny,
	MSmall,
	MMedium,
	MCompact,
	MLarge,
	MHuge,
};
int memmodel = MSmall;
/* NEAR_CODE excludes MCompact: although the original x86 compact model
 * specifies near code, this port's compact uses the far-code ABI
 * (uses_far_code() in i8086/abi.c) so that cross-segment calls work
 * across multiple per-module CODE segments.  Fn-ptrs must therefore be
 * 4 bytes (segment:offset) — otherwise the indirect-call emit assumes a
 * DX:AX target it never gets and retfs to garbage. */
#define NEAR_CODE() (memmodel == MTiny || memmodel == MSmall)
#define NEAR_DATA() (memmodel == MTiny || memmodel == MSmall || memmodel == MMedium)
#define CODEPTR_T() (NEAR_CODE() ? 'w' : 'l')
#define DATAPTR_T() (NEAR_DATA() ? 'w' : 'l')
#define CODEPTR_SZ() (NEAR_CODE() ? 2 : 4)
#define DATAPTR_SZ() (NEAR_DATA() ? 2 : 4)
/* alloc4/8 result class for data slots.  In near-data models the slot
 * address is a 16-bit SS-relative offset (Kw); in far-data models the
 * language-level address-of operator yields a 32-bit far pointer
 * (Kl), so the alloc temp must match DATAPTR_T() to avoid Kw/Kl
 * mismatches in subsequent storel/loadl/add/sub on slot addresses.
 * Backend (i8086 emit.c) is responsible for materializing SS:offset
 * when Oaddr Kl is lowered. */
#define ALLOC_T() DATAPTR_T()

#define SHORT     (1 << 16)  /* Short flag for types */
#define UNSIGNED  (1 << 17)  /* Unsigned flag for types */
#define FLOAT     (1 << 18)  /* Float flag for types (float=INT|FLOAT, double=LNG|FLOAT) */
/* FAR sits at bit 26 (§5c; was 24).  Position 24 is where the FLOAT flag
 * (bit 18) lands after TWO encoding shifts (IDIR(IDIR(float)) = `float **`,
 * IDIR(FUNC(float)) = float-returning fn ptr) — with FAR at 24 the collision
 * made DREF strip the shifted FLOAT bit, silently decoding the inner type
 * as int (the §5b miscompile class; §5b side-tabled only the fn-ptr RETURN
 * case).  At 26 the residual collisions move one level deeper and were
 * surveyed UNCONSUMED 2026-06-11 across MP + stevie + probes:
 *   - UNSIGNED (17) + 9 = 26: `unsigned T ***` / unsigned-returning fn ptr
 *     behind one more level decodes a phantom FAR;
 *   - FLOAT (18) + 9 = 27: `float ***` pollutes QVOLATILE;
 *   - the innermost FAR of a far-data T*** overflows bit 32 — nested far
 *     pointers keep exactly ONE level (`T **` round-trips: inner FAR
 *     26 -> 29 -> 26; a third level 26 -> 29 -> 32 is lost).  The old
 *     layout had two levels (24 -> 27 -> 30) but traded them for the
 *     REAL float ** miscompile above. */
#define FAR       (1 << 26)  /* Far pointer flag (32-bit segment:offset) */
/* QVOLATILE: a `volatile`-qualified type.  (Named QVOLATILE, not VOLATILE,
 * because VOLATILE is already a grammar TOKEN — yacc emits its own
 * `#define VOLATILE <tok>` that would shadow a same-named macro in the
 * actions, silently OR-ing the token number into the type.)  Unlike the
 * named-object volatile (varh[].isvolatile + the QBE `volatile` keyword via
 * symb_isvolatile), this rides INSIDE the type encoding so it survives the
 * IDIR->DREF round-trip: `volatile T *p` builds IDIR(T|QVOLATILE), where the
 * pointee's QVOLATILE bit is shifted up by IDIR and recovered by DREF when
 * `*p` is dereferenced — so the DEREF's load/store gets the keyword while p
 * itself stays non-volatile.  This is the pointer-to-volatile / MMIO case
 * (`volatile uint8_t *reg; *reg = x;`).  Set on a scalar type directly
 * (`volatile int v` -> INT|QVOLATILE) it is the OUTER qualifier, which varadd
 * strips into varh[].isvolatile.  See [[minic-volatile]]. */
#define QVOLATILE (1 << 27)  /* volatile-qualified type (§5c; was 25 — moved
                              * with FAR so SHORT (16) + 9 = 25 no longer
                              * pollutes it on `short ***`; the pointee's own
                              * volatile now rides one level up at bit 30) */
/* IDIR builds a pointer-to-x type.  In far-data memory models
 * (compact/large/huge), default data pointers are 32-bit segment:offset
 * (FAR), so dereferences route through loadfX/storefX.  Code pointers,
 * IDIR(FUN-kind), stay near in compact (encoded via CODEPTR_T(); FAR is
 * left off so storage/loads stay 16-bit).  In large/huge, code is also
 * far but CODEPTR_T() returns 'l', so FAR is not required there either. */
#define IDIR(x) ((((x) << 3) + PTR) | \
	((NEAR_DATA() || KIND(x) == FUN || \
	  (KIND(x) == PTR && KIND(DREF(x)) == FUN)) ? 0 : FAR))
#define IDIR_FAR(x) ((((x) << 3) + PTR) | FAR)  /* Far pointer to type */
#define FUNC(x) (((x) << 3) + FUN)
/* DREF strips the OUTER FAR flag before shifting so it doesn't pollute
 * downstream type unpacking.  Example: under compact, `struct line *l`
 * has ctyp = (struct_type << 3) | PTR | FAR.  After `*l`, downstream
 * sites compute sidx = DREF(struct_type | FAR_metadata).  Without the
 * mask, the FAR bit at position 26 shifts to bit 23, and the next DREF
 * call (e.g. in `case '.'` looking up structh[sidx]) explodes into a
 * wild array index → SIGSEGV.  Inner FAR bits encoded inside a nested
 * pointer type sit at position 29 (one IDIR up) and shift correctly to
 * position 26 after DREF, so ONE level of nested far ptr (e.g. `int **`
 * in compact) round-trips; a second nested level would need bit 32 and
 * is lost (see the FAR definition comment).  Reported via stevie
 * alloc.c crashing minic under --model=compact. */
/* DREF also strips the OUTER QVOLATILE qualifier (bit 27) before the shift,
 * mirroring the FAR mask: when unpacking a pointer type, any volatile on the
 * pointer OBJECT itself (normally absent — varadd consumes it) must not
 * pollute the recovered pointee, while the pointee's OWN volatile (encoded
 * one level up at bit 30) survives the shift down to bit 27.  It also makes
 * DREF-as-struct-index-extraction (structh[DREF(structtype)]) strip a
 * volatile-qualified struct type's bit so the index stays clean. */
#define DREF(x) (((x) & ~FAR & ~QVOLATILE) >> 3)
#define KIND(x) ((x) & 7)
#define ISUNSIGNED(x) ((x) & UNSIGNED)
#define ISFLOAT(x) ((x) & FLOAT)
#define ISFAR(x) ((x) & FAR)
#define ISVOLATILE(x) ((x) & QVOLATILE)
/* FARSTORAGE(s): true when Symb `s` denotes a file-scope/external symbol
 * whose STORAGE must be reached with a far (segment:offset) access under a
 * far-data model.  Unlike ISFAR (a property of the *value* type — "this is
 * a 4-byte far pointer / this lvalue was reached through a far deref"),
 * FARSTORAGE is a property of WHERE the object lives: every global/extern
 * datum under compact/large/huge is addressed far (qbe already emits
 * `mov es, seg sym; es:[bx]` for globals — see [[minic-far-data-segment]]),
 * so DIRECT access (g, g.m, g = x, g++) must route through the far
 * loadfX/storefX path just like array-subscript already does.  Holds for
 * both scalar AND pointer globals (a global pointer variable 4-byte value
 * still lives in a far segment), so it intentionally has NO PTR/FUN
 * exclusion.  Under NEAR_DATA (tiny/small/medium) it is always false, so
 * those models stay byte-identical.  Correct whether the global is left in
 * DGROUP (far access via ES=DGROUP reads the same bytes) or relocated to
 * its own far segment by asm_to_omf --far-static-data. */
#define FARSTORAGE(s) (!NEAR_DATA() && ((s).t == Glo || (s).t == Ext))
#define BASETYPE(x) (KIND(x) & ~UNSIGNED)
/* Storage sizes — must match `irtyp`'s storage class so struct member
 * offsets agree with the layout emit_struct_array_data writes:
 *   w → 2 bytes, l → 4 bytes on i8086 (the only target minic currently
 *   targets via the -m memmodel flag).
 *   Path A narrowed near pointers/int to `w` but left SIZE returning the
 *   pre-Path-A amd64 sizes (4/8), which made struct param look 24 bytes
 *   wide to access sites while the initializer wrote 8-byte entries.
 *   That mismatch had `P(P_MO)` read garbage from past the array.
 * For far pointers (always `l`) the size stays 4 (seg:off). */
#define SIZE(x)                                    \
	(KIND(x) == NIL ? (die("void has no size"), 0) : \
	 ISFLOAT(x) ? 4 :  /* float == double == single-precision, 4 bytes */ \
	 KIND(x) == CHR ? 1 :  \
	 ((x) & SHORT) ? 2 :  \
	 KIND(x) == INT ? 2 : \
	 KIND(x) == LNG ? 4 : \
	 (KIND(x) == STRUCT_T || KIND(x) == UNION_T) ? structh[DREF(x)].size : \
	 (KIND(x) == PTR && KIND(DREF(x)) == FUN) ? CODEPTR_SZ() : \
	 (KIND(x) == PTR && ISFAR(x)) ? 4 : DATAPTR_SZ())

/* Round n up to a multiple of a (a must be a power of two). */
#define ALIGNUP(n, a) (((n) + (a) - 1) & ~((a) - 1))

typedef struct Node Node;
typedef struct Symb Symb;
typedef struct Stmt Stmt;

struct Symb {
	enum {
		Con,
		Tmp,
		Var,
		Glo,
		Ext,  /* External symbol - referenced by name */
	} t;
	union {
		int n;
		char v[NString];
	} u;
	unsigned long ctyp;
};

struct Node {
	char op;
	unsigned char nlong;  /* 'N' nodes: 1 if the integer literal is `long`
	                       * (L/l suffix, or value too wide for i8086's
	                       * 16-bit int) so expr() types it LNG not INT.
	                       * 'F' nodes: 1 if the float literal carries an
	                       * f/F suffix (single-precision Ks), 0 = double. */
	union {
		int n;
		char v[NString];
		Symb s;
	} u;
	Node *l, *r;
};

struct Stmt {
	enum {
		If,
		While,
		DoWhile,
		For,
		Switch,
		Case,
		Default,
		Seq,
		Expr,
		Break,
		Continue,
		Ret,
		Goto,
		Label,
		Asm,
	} t;
	void *p1, *p2, *p3, *p4;
	int val; /* for case values */
	char label[NString]; /* for goto target and label name */
};

/* Inline assembly operand */
struct AsmOperand {
	char constraint[NString];  /* "=m", "m", "r", etc. */
	Node *expr;                /* The C expression for this operand */
};

/* Inline assembly statement data */
struct AsmStmt {
	char code[1024];           /* Assembly code template */
	int noutputs;              /* Number of output operands */
	int ninputs;               /* Number of input operands */
	int nclobbers;             /* Number of clobber specifications */
	struct AsmOperand outputs[4];  /* Up to 4 output operands */
	struct AsmOperand inputs[4];   /* Up to 4 input operands */
	char clobbers[8][NString];     /* Up to 8 clobber specs ("ax", "memory", etc.) */
	int isvolatile;            /* 1 if volatile */
};

int yylex(void), yylex_inner(void), yyerror(char *);
int prevtok = 0;  /* last token yylex returned; tag-namespace disambiguation */
int brace_depth = 0;     /* { } nesting the lexer has returned so far */
int pending_varclr = 0;  /* function body just closed; drop locals before next token */
int pending_static = 0;  /* a top-level `static` storage class is in effect for the
                          * declaration currently being parsed.  C `static` on a
                          * function/object = internal linkage, so its symbol must
                          * NOT be exported (QBE `function`, not `export function`),
                          * else `static inline` helpers in shared headers (e.g.
                          * MicroPython's utf8_get_char in misc.h) collide as
                          * duplicate public symbols across every TU that includes
                          * them.  Set/cleared in the yylex() wrapper (lexer-level,
                          * to dodge grammar conflicts); read at the function-header
                          * emit sites via fn_export_kw(). */
unsigned forinit_basetyp = 0;  /* base type of the current C99 for-init declarator(s) */

/* C `volatile`: set to 1 by the VOLATILE *type* productions (NOT the `asm
 * volatile` productions, which are separate), it carries the qualifier from
 * a type-specifier reduction to the declarator's varadd, which consumes it
 * into varh[].isvolatile and resets it to 0.  A volatile local/param's alloc
 * is then emitted with the QBE `volatile` keyword (emit_local_alloc), so the
 * backend's markvol pass keeps every load/store of it.  Scope: named scalar
 * local/param objects; volatile globals, arrays, struct members and
 * pointer-to-volatile are not yet covered.  See [[minic-volatile]]. */
int g_decl_volatile = 0;

/* Inner-block scope via alpha-renaming.  minic has a single flat local
 * symbol table and emits function bodies lazily (uses are resolved by
 * name at emit time via varget), so a name reused across distinct blocks
 * with *different* types cannot share one symtab slot.  When a local
 * declaration collides with a still-live local of a different type, the
 * new declarator is given a unique mangled name (`name$N`) and a rename
 * binding `name -> name$N` is pushed; the lexer then stamps that mangled
 * name into every subsequent *use* of the source name (uses are lexed
 * after the colliding decl reduces — verified: miniyacc default-reduces
 * the `type IDENT [= expr] ;` rules before reading the next token).  On
 * block exit the binding is popped (at the next yylex, by which point any
 * last-statement decl in the block has already reduced).  Renaming only
 * fires on a different-type collision — same-type re-declaration still
 * folds in varadd (stevie's sibling for-bodies), and the files with no
 * such collision are byte-for-byte unchanged. */
enum { NRename = 256 };
struct {
	char canon[NString];   /* source name */
	char mangled[NString]; /* unique replacement, e.g. t$3 */
	int depth;             /* brace_depth at the colliding decl */
} renamestk[NRename];
int renamestksp = 0;
int rename_serial = 0;   /* monotonic counter for unique mangled names */
Symb expr(Node *), lval(Node *);
void branch(Node *, int, int);
int stmt(Stmt *, int, int);
void emit_struct_copy(Symb, Symb);
char *call_target_name(char *);
Node *mknode(char, Node *, Node *);

FILE *of;
int line;
int lbl, tmp, nglo;
int enumval; /* Current enum value */
int cur_fn_interrupt; /* 1 if current function has __attribute__((interrupt)) */
int cur_fn_weak;      /* 1 if current function has __attribute__((weak)) */
/* §8r: an __attribute__((...)) on a function-POINTER parameter's pointee
 * (e.g. newlibc interrupts.c's `void __far __attribute__((interrupt)) (*isr)(void)`)
 * runs attrlist's actions, which would clobber cur_fn_interrupt/cur_fn_weak of
 * the ENCLOSING function (read by ansi_func_proto AFTER params are parsed).
 * Save before, restore after, so the param attribute is recorded-and-dropped
 * without changing the host function's ISR linkage. */
int fp_saved_interrupt, fp_saved_weak;
/* Struct/union return-by-value: when the current function's return type
 * is an aggregate, it is lowered (System-V style) with a hidden first
 * parameter — a caller-allocated pointer to result storage.  The callee
 * copies the returned value through it and returns the pointer; the
 * caller treats the call result as the address of the filled slot.  The
 * hidden pointer is spilled to a fixed-name local slot (%__sret) so the
 * `ret` statement can reload it regardless of which basic block it sits
 * in.  cur_fn_sret marks that the in-progress function uses this ABI;
 * cur_fn_sret_ctyp carries the aggregate ctyp (for the copy size). */
int lval_storage_far;       /* side-channel from lval(): 1 if the lvalue just
                             * returned lives in FAR storage (its address is a
                             * far seg:off pointer).  Distinct from the FAR bit
                             * on the value type, which for a PTR/FUN means "the
                             * VALUE is a far pointer" (near storage).  The store
                             * site reads this to far-store a pointer member of a
                             * far struct (e.g. mp_state_ctx.vm.last_pool), which
                             * the value-FAR bit alone can't distinguish from a
                             * near far-pointer variable. */
int cur_fn_sret;            /* 1 if current fn returns struct/union by value */
unsigned cur_fn_sret_ctyp;  /* the aggregate ctyp returned by value */
char cur_fn_name[NString];  /* Name of function currently being emitted — used
                             * to mangle function-local statics into file-scope
                             * data globals (`static int x;` in foo() →
                             * `data $_foo_x = ...`). */
int cur_fn_labelid = 0;     /* Bumped once per emitted function body.  C `goto`
                             * labels are function-scoped, but minic emits them
                             * into a flat per-TU asm namespace (`@user_<name>`);
                             * two functions sharing a label name (e.g. two
                             * `too_short:` in py/runtime.c) then collide at the
                             * assembler.  Suffixing every user label with this
                             * id (`@user_<name>_F<id>`) makes them unique. */
char *ini[NGlo];
char gloname[NGlo][NString];  /* Real C name for each global slot — used to
                               * emit `data $foo = ...` instead of $glo1 so
                               * cross-translation-unit linkage uses the
                               * source-level identifier. */
char glosec[NGlo][NString];   /* Optional section override for each global.
                               * Used by huge memory model to route arrays
                               * larger than 64K into per-symbol segments
                               * (`_HUGE_<sym>`) that the linker keeps out
                               * of DGROUP; asm_to_omf.py picks these up
                               * via `.section "_HUGE_<sym>"` markers. */
char glostatic[NGlo];         /* 1 = internal linkage (C `static` file-scope
                               * data, or a mangled function-local static):
                               * emitted as plain `data` (no .globl).  0 =
                               * external linkage: emitted as `export data`
                               * (§6b — asm_to_omf.py no longer auto-promotes
                               * data labels, so this flag is authoritative). */
int glo_decl_start = 1;       /* nglo watermark captured at type_and_ident
                               * (the start of a file-scope declaration); the
                               * STATIC typed_decl variants retro-mark every
                               * slot registered since (the named global plus
                               * nothing else that matters: anonymous string
                               * slots have no gloname and are never public,
                               * and a static FUNCTION body's statics must be
                               * internal anyway). */

void
glo_mark_static_range(int start)
{
	int i;
	for (i = start; i < nglo && i < NGlo; i++)
		glostatic[i] = 1;
}
struct {
	char v[NString];
	unsigned ctyp;
	int glo;
	int enumconst; /* -2 means it's an enum constant, glo stores the value */
	int isarray; /* 1 if this is an array, 0 if it's a regular variable or pointer */
	int isextern; /* 1 if this is an extern declaration */
	int isstaticlocal; /* 1 if this is a function-local static (mangled global,
	                    * but symtab entry should be cleared between functions) */
	int arraybytes;    /* total byte size of an array declarator (isarray==1);
	                    * 0 when unknown.  Lets sizeof(arrayvar) return the real
	                    * array size instead of the pointer-to-element size. */
	int aoa_dim;       /* >0 => this is an array whose ELEMENT is itself an
	                    * array typedef (`jmp_buf bufs[N]`, jmp_buf==int[8]).
	                    * Holds the inner dimension D; the value type is
	                    * IDIR(arrayelem) (e.g. int*) and a one-level subscript
	                    * bufs[i] yields the row ADDRESS bufs + i*D (no load) —
	                    * minic's flat type system can't carry array-of-array
	                    * stride, so mkidx() multiplies the index by D.  See §7e. */
	int istentative;   /* 1 if this global is an uninitialized (tentative) file-
	                    * scope definition: `static const T x;` with no init.  A
	                    * later initialized definition of the same name reuses the
	                    * buffered ini[]/gloname[] slot instead of erroring. */
	int fpid;          /* fn-ptr prototype index into fpproto[] (§2q), or -1.
	                    * Set when this var is a `T (*fp)(PARAMS)' declarator;
	                    * recovered at an indirect call `fp(...)' to coerce args. */
	int isvolatile;    /* 1 if declared `volatile`.  A volatile local/param's
	                    * alloc is emitted with the QBE `volatile` keyword, so
	                    * the backend's markvol pass keeps all its loads/stores
	                    * (no promote/forward/elide/reorder).  See [[minic-volatile]]. */
} varh[NVar];

/* Per-function parameter-type table, for argument coercion at call sites.
 * C11 6.5.2.2p4: when a prototype is in scope, each argument is converted
 * as if by assignment to its corresponding parameter type.  minic otherwise
 * sizes every stack argument by the ARGUMENT'S own type, so a wide (`l`,
 * 4-byte) value handed to a narrow (`w`, 2-byte) parameter is pushed as 4
 * bytes where the callee reads 2 — shifting every later stack argument.
 * (Canonical victim: `mp_parse_num_base(str, top - str, &base)` — the
 * far-pointer difference `top - str` is `l` but `size_t len` is `w`, so
 * `&base` was read from the wrong slot.)  We record each function's fixed
 * parameter types here (keyed by name, open addressing like varh) and
 * coerce arguments to them in the direct-call emit path. */
enum { NFnParam = 16 };  /* fixed params recorded per fn; extras uncoerced */
struct {
	char v[NString];
	unsigned ptyp[NFnParam];
	int nparam;  /* fixed params recorded (>=0); entry absent => -1 at lookup */
	unsigned rett;     /* declared RETURN type, carried UNSHIFTED (§5c).
	                    * The direct-call decode DREF(FUNC(ret)) strips
	                    * any ret bit sitting on the FAR/QVOLATILE
	                    * positions — e.g. a `float **` return's FLOAT
	                    * flag (18 + 9 = 27 = QVOLATILE) — silently
	                    * mistyping the result.  The fnproto mirror of
	                    * fpproto.rett (§5b) keeps the decode exact and
	                    * layout-independent. */
	int has_rett;      /* rett recorded (NIL is a valid void rett) */
} fnproto[NVar];

/* Function-POINTER prototypes (§2q).  An indirect call through a function
 * pointer (a `(*fp)(...)' variable or a method-table member like MicroPython's
 * `emit_method_table->local(emit, qst, id->local_num, kind)') loses the
 * callee's parameter types — the fn-ptr type integer encodes only the return
 * type — so coerce_arg (which fixes a stack-arg-width mismatch) never fired and
 * a narrow arg handed to a wide param shifted every later slot (the uint16_t
 * local_num stayed `w' where the param is mp_uint_t `l', so `kind' was read
 * from the wrong [bp+off]).  We record each fn-ptr declarator's fixed parameter
 * types here and stash the table index (fpid) in the declarator's varh/Member
 * entry, recovering it at the indirect-call site to coerce arguments — the
 * indirect-call analogue of fnproto/fnproto_find. */
enum { NFp = 512, NFpParam = 16 };
struct {
	int nparam;
	unsigned ptyp[NFpParam];
	unsigned rett;     /* declared RETURN type (§5b).  The type integer
	                    * IDIR(FUNC(ret)) shifts `ret' up 6 bits, which
	                    * (pre-§5c, FAR at bit 24) landed a float return's
	                    * FLOAT flag (bit 18) on FAR — and DREF strips
	                    * FAR, so a float-returning fn ptr decoded as
	                    * int-returning (the call result class came out
	                    * w, not s).  §5c moved FAR to 26, but the side
	                    * table stays: it also covers `float *` returns
	                    * (FLOAT three shifts up = 27 = QVOLATILE now)
	                    * and keeps the decode layout-independent. */
} fpproto[NFp];
int nfpproto = 0;
int g_callee_fpid = -1;  /* set by expr() case '.' to a fn-ptr member's fpid
                          * (or -1), read by the case 'I' indirect-call site */

/* Typedef table.  Sized for real preprocessed TUs (MicroPython headers
 * define well over 128 typedefs); minic is host-compiled so this is cheap. */
enum { NTyp = 512 };
struct {
	char v[NString];
	unsigned ctyp;
	int arraydim;        /* >0 => array typedef (`typedef int jmp_buf[8]`) */
	unsigned arrayelem;  /* element ctyp, valid only when arraydim > 0 */
	int fpid;            /* fn-ptr prototype index into fpproto[] (§2s), or -1,
	                      * for a `typedef RET (*F)(PARAMS)' typedef.  Surfaced
	                      * by typhget() into g_td_fpid so a variable/member
	                      * declared with the typedef name inherits the proto
	                      * and its indirect-call args are coerced (§2q gap). */
} typh[NTyp];

/* Set by typhget() (i.e. whenever the lexer resolves a TNAME) to the
 * array dimension/element of an array typedef, so storage-allocating
 * declaration rules (local var, struct member) can size them as arrays
 * rather than as the bare pointer-to-element their ctyp encodes.  Reset
 * to 0 by the lexer on every type keyword and by the `type '*'` pointer
 * rules, so a stale dim never leaks into a following plain declaration. */
int g_td_arraydim = 0;
unsigned g_td_arrayelem = 0;
int g_td_fpid = -1;  /* fn-ptr proto index of the typedef the lexer last
                      * resolved to a TNAME (§2s), or -1.  Consumed by the
                      * var/member declaration rules to inherit the proto;
                      * reset like g_td_arraydim so it never leaks forward. */

/* Struct/union member */
enum { NMember = 256 };
struct Member {
	char name[NString];
	unsigned ctyp;
	int offset;      /* Byte offset within struct */
	int bitwidth;    /* Bit width (0 = not a bitfield) */
	int bitoffset;   /* Bit offset within the storage unit */
	int count;       /* Array element count (0 = not an array OR flexible) */
	int isflex;      /* 1 = flexible array member `T x[];` (count 0 but decays) */
	int fpid;        /* fn-ptr prototype index into fpproto[] (§2q), or -1, for
	                  * a `T (*fn)(PARAMS)' member; recovered at the indirect
	                  * call `obj->fn(...)' to coerce arguments to PARAMS. */
};

/* Struct/union definition table.  minic is a host-compiled tool (runs on
 * the build machine, not on DOS), so this is sized for real translation
 * units: preprocessed MicroPython headers define well over 64 aggregate
 * types in a single TU. */
enum { NStruct = 256 };
struct {
	char name[NString];
	int isunion;  /* 1 for union, 0 for struct */
	int nmembers;
	struct Member members[NMember];  /* Max NMember members per struct */
	int size;
	int align;        /* §4g: struct alignment = max member alignment (1 under
	                   * NEAR_DATA, so medium stays byte-identical/packed). */
	int curbfoffset;  /* Current bit offset for bitfield packing */
	int curbfbase;    /* Byte offset of current bitfield storage unit */
	int forward;      /* 1 = forward/incomplete (tag known, body not yet defined) */
} structh[NStruct];
int nstruct = 0;
int curstruct = -1;  /* Index of struct currently being defined */
int parentstruct = -1;  /* Parent struct for anonymous members (legacy, unused) */
#define NStructNest 32
int structstk[NStructNest];  /* Saved curstruct for nested aggregate members */
int structstksp = 0;         /* Stack pointer into structstk */
int nestedanoncount = 0;     /* Counter for unique nested-aggregate tag names */
int typedefanoncount = 0;  /* Counter for anonymous typedef structs/unions */
int clit = 0;  /* Counter for compound literal temporaries */
unsigned curfntyp = INT;  /* Current function return type (defaults to INT for K&R style) */
unsigned parsed_type = INT;  /* Stores type parsed in typed_decl for later use */
unsigned kr_curtype = INT;  /* Stores type from current K&R param-decl group */
char parsed_ident[NString];  /* Stores identifier parsed in typed_decl */

void
die(char *s)
{
	fprintf(stderr, "error:%d: %s\n", line, s);
	exit(1);
}

void *
alloc(size_t s)
{
	void *p;

	p = malloc(s);
	if (!p)
		die("out of memory");
	return p;
}

unsigned
hash(char *s)
{
	unsigned h;

	h = 42;
	while (*s)
		h += 11 * h + *s++;
	return h % NVar;
}

void
varclr()
{
	unsigned h;

	/* Drop any inner-block renames left over from the previous function
	 * (defensive — a well-formed body pops them all at its closing }). */
	renamestksp = 0;

	for (h=0; h<NVar; h++)
		if ((!varh[h].glo && !varh[h].enumconst) || varh[h].isstaticlocal)
			varh[h].v[0] = 0;

	/* Linear-probe chain repair: a freshly emptied slot can sit between
	 * a hash bucket and entries that probed past it.  Lookup by name
	 * stops at the empty slot and falsely reports "undefined", which
	 * showed up after a function with many locals when a later global
	 * collided with one of those locals.  Walk the table from each
	 * empty slot, sliding live entries back into earlier positions if
	 * their hash bucket lies on or before the gap. */
	for (h = 0; h < NVar; h++) {
		if (varh[h].v[0] == 0) {
			unsigned i = (h + 1) % NVar;
			while (varh[i].v[0] != 0) {
				unsigned hi = hash(varh[i].v);
				int between;
				if (h < i)
					between = (hi <= h || hi > i);
				else
					between = (hi <= h && hi > i);
				if (between) {
					varh[h] = varh[i];
					varh[i].v[0] = 0;
					h = i;
				}
				i = (i + 1) % NVar;
				if (i == h)
					break;
			}
		}
	}
}

void
varadd(char *v, int glo, unsigned ctyp, int isarray)
{
	unsigned h0, h;
	int vol;

	/* Consume the pending `volatile` qualifier exactly once per declarator
	 * (reset so a non-volatile sibling/next declaration can't inherit it).
	 * The OUTER QVOLATILE bit on the type (a directly volatile-qualified
	 * scalar like `volatile int v`) marks the object too; strip it from the
	 * STORED type so the varh[].ctyp `==` redeclaration checks and all
	 * downstream type comparisons stay byte-identical.  A pointer-to-volatile
	 * (`volatile T *p`) carries its qualifier on the INNER pointee bit, which
	 * this mask leaves intact so a later `*p` deref recovers it. */
	vol = g_decl_volatile || (ISVOLATILE(ctyp) ? 1 : 0);
	g_decl_volatile = 0;
	ctyp &= ~QVOLATILE;

	h0 = hash(v);
	h = h0;
	do {
		if (varh[h].v[0] == 0) {
			strcpy(varh[h].v, v);
			varh[h].glo = glo;
			varh[h].ctyp = ctyp;
			varh[h].enumconst = (glo == -2) ? 1 : 0;
			varh[h].isarray = isarray;
			varh[h].isextern = 0;
			varh[h].isstaticlocal = 0;
			varh[h].arraybytes = 0;
			varh[h].aoa_dim = 0;
			varh[h].istentative = 0;
			varh[h].fpid = -1;
			varh[h].isvolatile = vol;
			return;
		}
		if (strcmp(varh[h].v, v) == 0) {
			/* Allow definition after extern declaration */
			if (varh[h].isextern && glo > 0) {
				varh[h].glo = glo;  /* Update to actual glo value */
				varh[h].isextern = 0;  /* Now it's a real definition */
				return;
			}
			/* Allow definition after function prototype with the same type:
			 * `char *foo(int);` followed later by `char *foo(int x) { ... }`. */
			if (KIND(varh[h].ctyp) == FUN && KIND(ctyp) == FUN &&
			    varh[h].ctyp == ctyp)
				return;
			/* Permit re-declaration of a same-typed local in a nested
			 * block.  Stevie does this in distinct for-bodies:
			 *   for (...) { LPTR *pos; ... }
			 *   for (...) { LPTR *pos; ... }
			 * QBE accepts the duplicate alloc (it isn't strict-SSA on
			 * input); the second decl effectively rebinds %name.
			 * Different-typed re-declaration across sibling blocks
			 * (e.g. MicroPython's `{const byte *t;}` then `{size_t
			 * t;}`) is handled *before* reaching varadd, by
			 * block_scope_decl() — it alpha-renames the new declarator
			 * so the two bindings stay distinct.  By the time a
			 * different-typed name reaches varadd unrenamed, it is a
			 * genuine redefinition. */
			if (glo == 0 && varh[h].glo == 0 && !varh[h].isextern &&
			    !varh[h].enumconst && varh[h].ctyp == ctyp) {
				varh[h].isarray = isarray;
				return;
			}
			die("double definition");
		}
		h = (h+1) % NVar;
	} while(h != h0);
	die("too many variables");
}

/* Return the active mangled name for a source identifier, or NULL.  The
 * most recently pushed binding (innermost scope) wins. */
char *
rename_lookup(char *v)
{
	int i;
	for (i = renamestksp - 1; i >= 0; i--)
		if (strcmp(renamestk[i].canon, v) == 0)
			return renamestk[i].mangled;
	return NULL;
}

/* Pop all rename bindings introduced in a block deeper than the current
 * lexer brace_depth (i.e. blocks the lexer has since closed).  Run at the
 * start of every yylex() so a block-scoped rename stops stamping uses the
 * moment its block ends. */
void
rename_pop_closed(void)
{
	while (renamestksp > 0 && renamestk[renamestksp-1].depth > brace_depth)
		renamestksp--;
}

/* Inner-block scope hook for a local declaration.  If `node`'s name
 * collides with a still-live local of a *different* type, give the
 * declarator a unique mangled name, register a rename so subsequent uses
 * resolve to it, and return the mangled name; otherwise return the name
 * unchanged.  Mutates node->u.v in place so an initializer assignment
 * built from the same node targets the renamed slot. */
/* Core renamer: operate on a name buffer `v` (NString-sized, mutated in
 * place when a rename fires) rather than a Node, so callers that hold only
 * a char* (e.g. the static-local path) can shadow too. */
char *
block_scope_rename(char *v, unsigned ctyp, int isarray)
{
	unsigned h0, h;

	h0 = hash(v);
	h = h0;
	do {
		if (varh[h].v[0] == 0)
			break;
		if (strcmp(varh[h].v, v) == 0) {
			/* Rename when the new local would collide with (a) a
			 * same-named LOCAL of a different type (the original
			 * §1k inner-block case), or (b) ANY global, extern,
			 * function, or enum constant — C says a block-scope
			 * declaration shadows the file-scope binding (§6a:
			 * newlibc vfs_open has a local `fat_mount` next to the
			 * file-scope function fat_mount()). */
			if ((varh[h].glo == 0 && !varh[h].isextern &&
			    !varh[h].enumconst &&
			    (varh[h].ctyp != ctyp || varh[h].isarray != isarray))
			    || varh[h].glo != 0 || varh[h].isextern
			    || varh[h].enumconst) {
				if (renamestksp >= NRename)
					die("too many block-scoped renames");
				sprintf(renamestk[renamestksp].mangled,
					"%s$%d", v, ++rename_serial);
				strcpy(renamestk[renamestksp].canon, v);
				renamestk[renamestksp].depth = brace_depth;
				strcpy(v, renamestk[renamestksp].mangled);
				renamestksp++;
				return v;
			}
			break;
		}
		h = (h+1) % NVar;
	} while (h != h0);
	return v;
}

char *
block_scope_decl(Node *node, unsigned ctyp, int isarray)
{
	return block_scope_rename(node->u.v, ctyp, isarray);
}

void
varaddextern(char *v, unsigned ctyp, int isarray)
{
	unsigned h0, h;
	int vol;

	/* Consume the pending `volatile` qualifier (set by the VOLATILE type
	 * productions) just like varadd, so `extern volatile int g;` marks the
	 * symbol and its loads/stores get the QBE keyword via symb_isvolatile.
	 * Like varadd, also honor an outer QVOLATILE type bit and strip it from
	 * the stored type (keeps inner pointee volatile for pointer externs). */
	vol = g_decl_volatile || (ISVOLATILE(ctyp) ? 1 : 0);
	g_decl_volatile = 0;
	ctyp &= ~QVOLATILE;

	h0 = hash(v);
	h = h0;
	do {
		if (varh[h].v[0] == 0) {
			strcpy(varh[h].v, v);
			varh[h].glo = 1;  /* Mark as global */
			varh[h].ctyp = ctyp;
			varh[h].enumconst = 0;
			varh[h].isarray = isarray;
			varh[h].isextern = 1;  /* Mark as extern */
			varh[h].isstaticlocal = 0;
			varh[h].isvolatile = vol;
			return;
		}
		if (strcmp(varh[h].v, v) == 0) {
			/* Allow multiple extern declarations, or extern after definition */
			if (varh[h].isextern || varh[h].glo == 1) {
				varh[h].isvolatile |= vol;  /* upgrade if any decl is volatile */
				return;  /* Already declared/defined */
			}
			die("double definition");
		}
		h = (h+1) % NVar;
	} while(h != h0);
	die("too many variables");
}

int
structfind(char *name)
{
	int i;
	for (i = 0; i < nstruct; i++)
		if (strcmp(structh[i].name, name) == 0)
			return i;
	return -1;
}

/* Extract the actual string content from a string literal (stored in ini[] with QBE format)
 * Format is: { b "actual string", b 0 }
 * We skip 5 chars prefix and 8 chars suffix
 */
void
getstrlit(int idx, char *buf, int bufsz)
{
	char *src = ini[idx];
	char *dst = buf;
	int len;

	/* Skip prefix: { b " */
	src += 5;

	/* Find the closing quote - copy until we hit ", b 0 } */
	len = strlen(src);
	if (len > 8)
		len -= 8;  /* Remove suffix: ", b 0 } */
	else
		len = 0;

	if (len >= bufsz)
		len = bufsz - 1;

	strncpy(dst, src, len);
	dst[len] = '\0';
}

int
structadd(char *name, int isunion)
{
	int idx;

	idx = structfind(name);
	if (idx >= 0) {
		if (!structh[idx].forward)
			die("struct/union already defined");
		/* Reuse the forward-declared slot; fill in the real body so
		 * that pre-existing typedefs/pointers keep pointing at it and
		 * SIZE() becomes correct once members are added. */
		structh[idx].isunion = isunion;
		structh[idx].nmembers = 0;
		structh[idx].size = 0;
		structh[idx].align = 1;
		structh[idx].curbfoffset = 0;
		structh[idx].curbfbase = 0;
		structh[idx].forward = 0;
		return idx;
	}

	if (nstruct >= NStruct)
		die("too many struct/union definitions");

	idx = nstruct++;
	strcpy(structh[idx].name, name);
	structh[idx].isunion = isunion;
	structh[idx].nmembers = 0;
	structh[idx].size = 0;
	structh[idx].align = 1;
	structh[idx].curbfoffset = 0;  /* No bitfield in progress */
	structh[idx].curbfbase = 0;
	structh[idx].forward = 0;
	return idx;
}

/* Create a forward/incomplete struct entry (tag known, body not yet
 * defined).  Used for `typedef struct Foo Foo;` and pointer-to-incomplete
 * references before the struct body appears.  Reuses an existing slot if
 * the tag is already known. */
int
structadd_forward(char *name, int isunion)
{
	int idx;

	idx = structfind(name);
	if (idx >= 0)
		return idx;

	if (nstruct >= NStruct)
		die("too many struct/union definitions");

	idx = nstruct++;
	strcpy(structh[idx].name, name);
	structh[idx].isunion = isunion;
	structh[idx].nmembers = 0;
	structh[idx].size = 0;
	structh[idx].align = 1;
	structh[idx].curbfoffset = 0;
	structh[idx].curbfbase = 0;
	structh[idx].forward = 1;
	return idx;
}

/* §4g: natural alignment of a member type.  Under NEAR_DATA (tiny/small/
 * medium) this is always 1, so those models keep the historical PACKED
 * layout byte-for-byte.  Under far-data a 4-byte member (long / far data
 * pointer / float / 4-byte fn-ptr) aligns to 4 so MicroPython's conservative
 * GC, which scans memory in sizeof(void*)=4 strides, finds every pointer at
 * a stride boundary instead of split across two scan words.  Sub-4-byte
 * scalars (char, short, int, near ptr) stay align-1 (they can't hold a
 * pointer, so their alignment is irrelevant to the collector) — this keeps
 * the padding, and therefore the image growth, to a minimum: a struct with
 * no 4-byte member has alignment 1 and is byte-identical even under far-data. */
int
alignof_ctyp(unsigned ctyp)
{
	if (NEAR_DATA())
		return 1;
	if (KIND(ctyp) == STRUCT_T || KIND(ctyp) == UNION_T)
		return structh[DREF(ctyp)].align;
	if (SIZE(ctyp) >= 4)
		return 4;
	return 1;
}

/* §4g: finalize a struct's layout by tail-padding its size up to its own
 * alignment, so an array of it (or its embedding in a larger struct) keeps
 * every element/member at its natural alignment.  Idempotent (rounding an
 * already-rounded size is a no-op), so it is safe to call at every grammar
 * close site.  A no-op under NEAR_DATA (align stays 1). */
void
structfinish(int idx)
{
	if (idx < 0)
		return;
	structh[idx].size = ALIGNUP(structh[idx].size, structh[idx].align);
}

void
structaddmember(int sidx, char *name, unsigned ctyp)
{
	int i;
	int malign;
	struct Member *m;

	if (structh[sidx].nmembers >= NMember)
		die("too many members in struct/union");

	/* Check for duplicate member names */
	for (i = 0; i < structh[sidx].nmembers; i++)
		if (strcmp(structh[sidx].members[i].name, name) == 0)
			die("duplicate member name");

	/* Non-bitfield member resets bitfield packing state */
	structh[sidx].curbfoffset = 0;
	structh[sidx].curbfbase = 0;

	m = &structh[sidx].members[structh[sidx].nmembers];
	strcpy(m->name, name);
	m->ctyp = ctyp;
	/* A `volatile` member captures its qualifier in m->ctyp's QVOLATILE
	 * bit (from the `VOLATILE T` type production), so the g_decl_volatile
	 * flag has served its purpose here.  Clear it so it does NOT leak into
	 * a following file-scope declaration (`struct{volatile int x;}; int g;`
	 * — g must stay non-volatile); varadd only consumes the flag for plain
	 * variable decls, never for struct members. */
	g_decl_volatile = 0;
	m->bitwidth = 0;    /* Not a bitfield */
	m->bitoffset = 0;
	m->count = 0;       /* Scalar member */
	m->isflex = 0;
	m->fpid = -1;       /* Not a fn-ptr member (set by the fn-ptr member rule) */

	/* §4g: track the struct's alignment and pad to the member's alignment
	 * before placing it (no-op under NEAR_DATA — alignof_ctyp returns 1). */
	malign = alignof_ctyp(ctyp);
	if (malign > structh[sidx].align)
		structh[sidx].align = malign;
	if (structh[sidx].isunion) {
		/* Union: all members at offset 0 */
		m->offset = 0;
		/* Union size is max of member sizes */
		if (SIZE(ctyp) > structh[sidx].size)
			structh[sidx].size = SIZE(ctyp);
	} else {
		/* Struct: members laid out sequentially */
		structh[sidx].size = ALIGNUP(structh[sidx].size, malign);
		m->offset = structh[sidx].size;
		structh[sidx].size += SIZE(ctyp);
	}

	structh[sidx].nmembers++;
}

/* Add an array member: type ident[count]; */
void
structaddarrmember(int sidx, char *name, unsigned ctyp, int count)
{
	int i, total;
	int malign;
	struct Member *m;

	if (structh[sidx].nmembers >= NMember)
		die("too many members in struct/union");
	for (i = 0; i < structh[sidx].nmembers; i++)
		if (strcmp(structh[sidx].members[i].name, name) == 0)
			die("duplicate member name");

	structh[sidx].curbfoffset = 0;
	structh[sidx].curbfbase = 0;

	m = &structh[sidx].members[structh[sidx].nmembers];
	strcpy(m->name, name);
	m->ctyp = ctyp;       /* Element type — accesses through s.arr[i] use this */
	g_decl_volatile = 0;  /* see structaddmember: don't leak to the next decl */
	m->bitwidth = 0;
	m->bitoffset = 0;
	m->count = count;
	m->isflex = (count == 0);  /* `T x[];` flexible array: 0 bytes but decays */
	total = SIZE(ctyp) * count;
	/* §4g: array member aligns to its ELEMENT type (so a `void *arr[]`
	 * after a 2-byte field starts at a 4-aligned offset). */
	malign = alignof_ctyp(ctyp);
	if (malign > structh[sidx].align)
		structh[sidx].align = malign;
	if (structh[sidx].isunion) {
		m->offset = 0;
		if (total > structh[sidx].size)
			structh[sidx].size = total;
	} else {
		structh[sidx].size = ALIGNUP(structh[sidx].size, malign);
		m->offset = structh[sidx].size;
		structh[sidx].size += total;
	}
	structh[sidx].nmembers++;
}

/* Add a bitfield member to a struct */
void
structaddbitfield(int sidx, char *name, unsigned ctyp, int width)
{
	int i;
	struct Member *m;
	int unitsize;      /* Size of storage unit in bits */
	int unitbytes;     /* Size of storage unit in bytes */

	if (structh[sidx].nmembers >= NMember)
		die("too many members in struct/union");

	/* Check for duplicate member names */
	for (i = 0; i < structh[sidx].nmembers; i++)
		if (strcmp(structh[sidx].members[i].name, name) == 0)
			die("duplicate member name");

	/* Calculate storage unit size based on declared type */
	unitbytes = SIZE(ctyp);
	unitsize = unitbytes * 8;

	/* Validate width */
	if (width <= 0)
		die("bitfield width must be positive");
	if (width > unitsize)
		die("bitfield width exceeds type size");

	/* §4g: a bitfield's storage unit aligns to its declared type. */
	if (alignof_ctyp(ctyp) > structh[sidx].align)
		structh[sidx].align = alignof_ctyp(ctyp);

	/* Check if this bitfield fits in current storage unit */
	if (structh[sidx].curbfoffset == 0 ||
	    structh[sidx].curbfoffset + width > unitsize) {
		/* Start a new storage unit */
		structh[sidx].size = ALIGNUP(structh[sidx].size, alignof_ctyp(ctyp));
		structh[sidx].curbfbase = structh[sidx].size;
		structh[sidx].curbfoffset = 0;
		structh[sidx].size += unitbytes;
	}

	m = &structh[sidx].members[structh[sidx].nmembers];
	strcpy(m->name, name);
	m->ctyp = ctyp;
	g_decl_volatile = 0;  /* see structaddmember: don't leak to the next decl */
	m->offset = structh[sidx].curbfbase;  /* Points to storage unit base */
	m->bitwidth = width;
	m->bitoffset = structh[sidx].curbfoffset;
	m->count = 0;
	m->isflex = 0;

	/* Advance bit offset for next bitfield */
	structh[sidx].curbfoffset += width;

	structh[sidx].nmembers++;
}

/* Hoist members from an anonymous struct/union into parent struct */
void
hoistanonymous(int parent_sidx, int anon_sidx)
{
	int i;
	int base_offset;
	int anon_size;

	if (parent_sidx < 0 || anon_sidx < 0)
		die("invalid struct index for anonymous member");

	/* Base offset for anonymous members in parent struct */
	base_offset = structh[parent_sidx].size;
	anon_size = structh[anon_sidx].size;
	/* §4g: the anonymous aggregate aligns to its own alignment, and that
	 * alignment propagates to the parent (no-op under NEAR_DATA: align==1,
	 * so base_offset is unchanged and the layout stays byte-identical). */
	if (structh[anon_sidx].align > structh[parent_sidx].align)
		structh[parent_sidx].align = structh[anon_sidx].align;
	if (!structh[parent_sidx].isunion)
		base_offset = ALIGNUP(base_offset, structh[anon_sidx].align);

	/* Copy all members from anonymous struct to parent */
	for (i = 0; i < structh[anon_sidx].nmembers; i++) {
		struct Member *anon_mem = &structh[anon_sidx].members[i];
		struct Member *parent_mem;
		int j;

		if (structh[parent_sidx].nmembers >= 16)
			die("too many members in struct (from anonymous)");

		/* Check for duplicate names in parent */
		for (j = 0; j < structh[parent_sidx].nmembers; j++)
			if (strcmp(structh[parent_sidx].members[j].name, anon_mem->name) == 0)
				die("anonymous member name conflicts with parent");

		/* Add member to parent */
		parent_mem = &structh[parent_sidx].members[structh[parent_sidx].nmembers];
		strcpy(parent_mem->name, anon_mem->name);
		parent_mem->ctyp = anon_mem->ctyp;
		parent_mem->bitwidth = anon_mem->bitwidth;
		parent_mem->bitoffset = anon_mem->bitoffset;
		parent_mem->count = anon_mem->count;
		parent_mem->isflex = anon_mem->isflex;

		if (structh[parent_sidx].isunion) {
			/* Parent is union - all members at offset 0 */
			parent_mem->offset = anon_mem->offset;
		} else if (structh[anon_sidx].isunion) {
			/* Anonymous union in struct - all at base_offset */
			parent_mem->offset = base_offset + anon_mem->offset;
		} else {
			/* Both are structs - add base offset */
			parent_mem->offset = base_offset + anon_mem->offset;
		}

		structh[parent_sidx].nmembers++;
	}

	/* Update parent size */
	if (structh[parent_sidx].isunion) {
		/* Union: size is max of member sizes */
		if (anon_size > structh[parent_sidx].size)
			structh[parent_sidx].size = anon_size;
	} else {
		/* Struct: place the anonymous body at the (aligned) base. */
		structh[parent_sidx].size = base_offset + anon_size;
	}
}

/* Find a member by name in a struct, returns member index or -1 if not found */
int
structfindmember(int sidx, char *name)
{
	int i;
	for (i = 0; i < structh[sidx].nmembers; i++)
		if (strcmp(structh[sidx].members[i].name, name) == 0)
			return i;
	return -1;
}

void
typhadd(char *v, unsigned ctyp)
{
	unsigned h0, h;

	/* hash() returns hash%NVar but typh has NTyp slots — wrap into NTyp. */
	h0 = hash(v) % NTyp;
	h = h0;
	do {
		if (typh[h].v[0] == 0) {
			strcpy(typh[h].v, v);
			typh[h].ctyp = ctyp;
			typh[h].arraydim = 0;
			typh[h].arrayelem = 0;
			typh[h].fpid = -1;
			return;
		}
		if (strcmp(typh[h].v, v) == 0)
			die("typedef already defined");
		h = (h+1) % NTyp;
	} while(h != h0);
	die("too many typedefs");
}

/* Register an array typedef `typedef ELEM NAME[DIM];`.  The stored ctyp
 * is pointer-to-element so the name decays correctly when used as a
 * function parameter or dereferenced; arraydim/arrayelem let storage
 * sites allocate DIM*sizeof(ELEM) bytes and flag the var as an array. */
void
typhadd_array(char *v, unsigned elemctyp, int dim)
{
	unsigned h0, h;

	h0 = hash(v) % NTyp;
	h = h0;
	do {
		if (typh[h].v[0] == 0) {
			strcpy(typh[h].v, v);
			typh[h].ctyp = IDIR(elemctyp);
			typh[h].arraydim = dim;
			typh[h].arrayelem = elemctyp;
			typh[h].fpid = -1;
			return;
		}
		if (strcmp(typh[h].v, v) == 0)
			die("typedef already defined");
		h = (h+1) % NTyp;
	} while(h != h0);
	die("too many typedefs");
}

int
typhget(char *v, unsigned *ctyp)
{
	unsigned h0, h;

	h0 = hash(v) % NTyp;
	h = h0;
	do {
		if (strcmp(typh[h].v, v) == 0) {
			*ctyp = typh[h].ctyp;
			g_td_arraydim = typh[h].arraydim;
			g_td_arrayelem = typh[h].arrayelem;
			g_td_fpid = typh[h].fpid;
			return 1;
		}
		if (typh[h].v[0] == 0)
			return 0;
		h = (h+1) % NTyp;
	} while(h != h0);
	return 0;
}

/* Record a fn-ptr prototype index on an existing typedef entry (§2s), so a
 * variable/member later declared with the typedef name inherits it and its
 * indirect-call arguments are coerced.  Called by the fn-ptr typedef rules
 * right after typhadd(). */
void
typhset_fpid(char *v, int fpid)
{
	unsigned h0, h;

	if (fpid < 0)
		return;
	h0 = hash(v) % NTyp;
	h = h0;
	do {
		if (strcmp(typh[h].v, v) == 0) {
			typh[h].fpid = fpid;
			return;
		}
		if (typh[h].v[0] == 0)
			return;
		h = (h+1) % NTyp;
	} while(h != h0);
}

/* Probe the symbol table for the array flag of a variable.  varh[]
 * uses linear probing, so the entry for `v` may be at hash(v)+k for
 * some k > 0; looking only at hash(v) misses collisions.  Return 0
 * if not found. */
int
var_isarray(char *v)
{
	unsigned h0, h;
	h0 = hash(v);
	h = h0;
	do {
		if (varh[h].v[0] == 0)
			return 0;
		if (strcmp(varh[h].v, v) == 0)
			return varh[h].isarray;
		h = (h+1) % NVar;
	} while (h != h0);
	return 0;
}

/* Record the total byte size of an array declarator so sizeof(arrayvar)
 * can report it.  Called by the array-declaration rules right after
 * varadd.  Silently no-ops if the name isn't found. */
void
var_set_arraybytes(char *v, int bytes)
{
	unsigned h0, h;
	h0 = hash(v);
	h = h0;
	do {
		if (varh[h].v[0] == 0)
			return;
		if (strcmp(varh[h].v, v) == 0) {
			varh[h].arraybytes = bytes;
			return;
		}
		h = (h+1) % NVar;
	} while (h != h0);
}

/* Total byte size of an array variable, or 0 if unknown / not an array. */
int
var_arraybytes(char *v)
{
	unsigned h0, h;
	h0 = hash(v);
	h = h0;
	do {
		if (varh[h].v[0] == 0)
			return 0;
		if (strcmp(varh[h].v, v) == 0)
			return varh[h].isarray ? varh[h].arraybytes : 0;
		h = (h+1) % NVar;
	} while (h != h0);
	return 0;
}

/* Flag a variable as an array-of-array-typedef element (`jmp_buf bufs[N]`),
 * recording the inner dimension D.  See varh[].aoa_dim / mkidx. */
void
var_set_aoa_dim(char *v, int dim)
{
	unsigned h0, h;
	h0 = hash(v);
	h = h0;
	do {
		if (varh[h].v[0] == 0)
			return;
		if (strcmp(varh[h].v, v) == 0) {
			varh[h].aoa_dim = dim;
			return;
		}
		h = (h+1) % NVar;
	} while (h != h0);
}

/* Inner array dimension D of an array-of-array variable, or 0 if `v` is not
 * one.  Looked up the same way var_isarray does (plain probe by node name),
 * so a block-scope-renamed local resolves identically. */
int
var_aoa_dim(char *v)
{
	unsigned h0, h;
	h0 = hash(v);
	h = h0;
	do {
		if (varh[h].v[0] == 0)
			return 0;
		if (strcmp(varh[h].v, v) == 0)
			return varh[h].aoa_dim;
		h = (h+1) % NVar;
	} while (h != h0);
	return 0;
}

/* Probe the symbol table for a *local* declaration (parameter or
 * function-scope variable, including static locals) named `v`.  Used by
 * the lexer to recognise a name that shadows a same-named file-scope
 * typedef: once such a local is in scope, uses of the name are ordinary
 * identifiers (IDENT), not the typedef (TNAME).  Returns 0 if not found
 * or only a global/enum/typedef entry exists. */
int
var_islocal(char *v)
{
	unsigned h0, h;
	h0 = hash(v);
	h = h0;
	do {
		if (varh[h].v[0] == 0)
			return 0;
		if (strcmp(varh[h].v, v) == 0)
			return (!varh[h].glo && !varh[h].enumconst)
			    || varh[h].isstaticlocal;
		h = (h+1) % NVar;
	} while (h != h0);
	return 0;
}

/* Mark the just-added global `name` as a tentative (uninitialized) file-
 * scope definition.  A later initialized definition of the same name will
 * reuse its buffered ini[]/gloname[] slot instead of erroring. */
void
mark_tentative(char *name)
{
	unsigned h0, h;
	h0 = hash(name);
	h = h0;
	do {
		if (varh[h].v[0] == 0)
			return;
		if (strcmp(varh[h].v, name) == 0) {
			varh[h].istentative = 1;
			return;
		}
		h = (h+1) % NVar;
	} while (h != h0);
}

/* If `name` already exists as a tentative global, return its buffered glo
 * index (and clear the tentative flag so the real definition can overwrite
 * ini[idx]); otherwise return -1.  Lets an initialized file-scope
 * definition supersede an earlier `static const T name;` declaration. */
int
glo_redef_index(char *name)
{
	unsigned h0, h;
	h0 = hash(name);
	h = h0;
	do {
		if (varh[h].v[0] == 0)
			return -1;
		if (strcmp(varh[h].v, name) == 0) {
			if (varh[h].istentative) {
				varh[h].istentative = 0;
				return varh[h].glo;
			}
			return -1;
		}
		h = (h+1) % NVar;
	} while (h != h0);
	return -1;
}

Symb *
varget(char *v)
{
	static Symb s;
	unsigned h0, h;

	h0 = hash(v);
	h = h0;
	do {
		if (strcmp(varh[h].v, v) == 0) {
			if (varh[h].enumconst) {
				/* Enum constant - return as integer constant */
				s.t = Con;
				s.u.n = varh[h].glo;
			} else if (varh[h].isextern) {
				/* External symbol - reference by name */
				s.t = Ext;
				strcpy(s.u.v, v);
			} else if (!varh[h].glo) {
				s.t = Var;
				strcpy(s.u.v, v);
			} else {
				s.t = Glo;
				s.u.n = varh[h].glo;
			}
			s.ctyp = varh[h].ctyp;
			return &s;
		}
		h = (h+1) % NVar;
	} while (h != h0 && varh[h].v[0] != 0);
	return 0;
}

/* Return 1 if the local/param named `v` was declared `volatile`. */
int
var_isvolatile(char *v)
{
	unsigned h0, h;

	h0 = hash(v);
	h = h0;
	do {
		if (strcmp(varh[h].v, v) == 0)
			return varh[h].isvolatile;
		h = (h+1) % NVar;
	} while (h != h0 && varh[h].v[0] != 0);
	return 0;
}

/* Return 1 if the address Symb `s` names a `volatile`-qualified scalar
 * GLOBAL/extern object.  Globals have no `alloc`, so markvol() (which
 * propagates the bit from a volatile alloc to its loads/stores) cannot
 * reach them — instead the scalar load()/loadfar()/store sites call this
 * and emit the QBE `volatile` keyword directly on the access.  Locals
 * (s.t == Var) return 0: their accesses are already marked by markvol via
 * their volatile alloc, so adding the keyword here would be redundant and
 * would change codegen.  Pointer-to-volatile / volatile members / arrays
 * are out of scope (named scalar object subset).  See [[minic-volatile]]. */
int
symb_isvolatile(Symb s)
{
	if (s.t == Glo && s.u.n > 0 && s.u.n < NGlo && gloname[s.u.n][0] != 0)
		return var_isvolatile(gloname[s.u.n]);
	if (s.t == Ext)
		return var_isvolatile(s.u.v);
	return 0;
}

/* Emit a local variable's `alloc`, appending the QBE `volatile` keyword when
 * the variable was declared volatile so markvol() keeps all its accesses.
 * Replaces the inline `fprintf(of, "\t%%%s =%c alloc%d %d\n", ...)` at the
 * scalar local/param decl sites. */
void
emit_local_alloc(char *v, char klass, int align, int size)
{
	/* The `volatile` keyword goes between the opcode and its size operand
	 * (`alloc4 volatile 2`), where the QBE parser peeks for it. */
	fprintf(of, "\t%%%s =%c alloc%d%s %d\n", v, klass, align,
		var_isvolatile(v) ? " volatile" : "", size);
}

/* Evaluate a constant expression - returns the integer value */
/* Used for case labels which require compile-time constants */
int
const_eval(Node *n)
{
	int l, r;
	Symb *sv;

	if (!n) die("null expression in const_eval");

	switch (n->op) {
	case 'N':
		/* Numeric constant (lexer creates 'N' nodes for numbers with u.n) */
		return n->u.n;

	case 'V':
		/* Identifier - could be an enum constant */
		sv = varget(n->u.v);
		if (sv && sv->t == Con)
			return sv->u.n;
		die("non-constant in case label");
		return 0;

	case '+':
		l = const_eval(n->l);
		r = const_eval(n->r);
		return l + r;

	case '-':
		if (n->r == 0) {
			/* Unary minus */
			return -const_eval(n->l);
		}
		l = const_eval(n->l);
		r = const_eval(n->r);
		return l - r;

	case '*':
		l = const_eval(n->l);
		r = const_eval(n->r);
		return l * r;

	case '/':
		l = const_eval(n->l);
		r = const_eval(n->r);
		if (r == 0) die("division by zero in constant expression");
		return l / r;

	case '%':
		l = const_eval(n->l);
		r = const_eval(n->r);
		if (r == 0) die("modulo by zero in constant expression");
		return l % r;

	case '&':
		l = const_eval(n->l);
		r = const_eval(n->r);
		return l & r;

	case '|':
		l = const_eval(n->l);
		r = const_eval(n->r);
		return l | r;

	case '^':
		l = const_eval(n->l);
		r = const_eval(n->r);
		return l ^ r;

	case '~':
		/* Unary bitwise NOT */
		return ~const_eval(n->l);

	case 'L':
		/* Left shift */
		l = const_eval(n->l);
		r = const_eval(n->r);
		return l << r;

	case 'R':
		/* Right shift */
		l = const_eval(n->l);
		r = const_eval(n->r);
		return l >> r;

	case 'K':
		/* Cast `(type)expr`: fold to the inner value.  MicroPython's
		 * small-int / qstr tagging idiom casts integer constants, e.g.
		 * `(mp_obj_t)((qstr << 3) | 2)`. */
		return const_eval(n->l);

	case '!':
		/* Logical NOT. */
		return !const_eval(n->l);

	case 'e':
		/* Equality `==`. */
		return const_eval(n->l) == const_eval(n->r);

	case 'n':
		/* Inequality `!=`. */
		return const_eval(n->l) != const_eval(n->r);

	case '<':
		/* Less-than (the parser also lowers `>` to `<` with swapped
		 * operands). */
		return const_eval(n->l) < const_eval(n->r);

	case 'l':
		/* Less-or-equal (the parser lowers `>=` likewise). */
		return const_eval(n->l) <= const_eval(n->r);

	case 'a':
		/* Logical AND `&&`. */
		return const_eval(n->l) && const_eval(n->r);

	case 'o':
		/* Logical OR `||`. */
		return const_eval(n->l) || const_eval(n->r);

	case '?':
		/* Ternary `cond ? a : b`.  The parser builds the node as
		 * `'?'(cond, ':'(a, b))`. */
		if (const_eval(n->l))
			return const_eval(n->r->l);
		return const_eval(n->r->r);

	default:
		die("unsupported operation in constant expression");
		return 0;
	}
}

/* Non-dying companion to const_eval: returns 1 iff `n` is a pure
 * integer constant expression that const_eval can fold (every leaf a
 * numeric/enum constant and every operator in the supported set).
 * Used to decide whether a `sizeof(type[dim])` dimension is foldable —
 * MicroPython's MP_STATIC_ASSERT idiom puts a non-foldable address
 * comparison in the dimension and voids the whole sizeof. */
int
constfoldable(Node *n)
{
	Symb *sv;

	if (!n)
		return 0;
	switch (n->op) {
	case 'N':
		return 1;
	case 'V':
		sv = varget(n->u.v);
		return sv && sv->t == Con;
	case '~':
	case '!':
	case 'K':
		return constfoldable(n->l);
	case '-':
		if (n->r == 0)
			return constfoldable(n->l);
		return constfoldable(n->l) && constfoldable(n->r);
	case '?':
		return constfoldable(n->l) &&
		       constfoldable(n->r->l) && constfoldable(n->r->r);
	case '+': case '*': case '/': case '%': case '&': case '|':
	case '^': case 'L': case 'R': case 'e': case 'n': case '<':
	case 'l': case 'a': case 'o':
		return constfoldable(n->l) && constfoldable(n->r);
	default:
		return 0;
	}
}

/* Type of an expression, for sizeof(expr).  minic has no pure
 * type-inference pass, so this runs the normal expr() emitter with `of`
 * redirected to the bit bucket and the scratch counters restored
 * afterwards.  sizeof is unevaluated in C, so discarding the emitted
 * code is correct.  (Any string / compound-literal data globals
 * registered during the walk are left in place — harmless extra data
 * that is never referenced.)  Used for the `sizeof(arr)/sizeof(arr[0])`
 * count idiom and `sizeof(*ptr)`. */
unsigned
typeof_expr(Node *n)
{
	FILE *save_of = of, *nullf;
	int save_tmp = tmp, save_lbl = lbl, save_clit = clit;
	Symb s;

	nullf = fopen("/dev/null", "w");
	if (nullf)
		of = nullf;
	s = expr(n);
	of = save_of;
	if (nullf)
		fclose(nullf);
	tmp = save_tmp;
	lbl = save_lbl;
	clit = save_clit;
	return s.ctyp;
}

int
sizeof_member_array_expr(Node *n)
{
	FILE *save_of = of, *nullf;
	int save_tmp = tmp, save_lbl = lbl, save_clit = clit;
	Symb s;
	struct Member *m = 0;
	int sidx, i, bytes = 0;

	if (!n || n->op != '.')
		return 0;

	nullf = fopen("/dev/null", "w");
	if (nullf)
		of = nullf;
	s = lval(n->l);
	of = save_of;
	if (nullf)
		fclose(nullf);
	tmp = save_tmp;
	lbl = save_lbl;
	clit = save_clit;

	if (KIND(s.ctyp) != STRUCT_T && KIND(s.ctyp) != UNION_T)
		return 0;

	sidx = DREF(s.ctyp);
	for (i = 0; i < structh[sidx].nmembers; i++) {
		if (strcmp(structh[sidx].members[i].name, n->r->u.v) == 0) {
			m = &structh[sidx].members[i];
			break;
		}
	}
	if (!m || m->count <= 0)
		return 0;
	bytes = SIZE(m->ctyp) * m->count;
	return bytes;
}

char
irtyp(unsigned ctyp)
{
	int k = KIND(ctyp);

	/* Pointer/function types: far pointers are 32-bit (segment:offset),
	 * encoded as 'l'.  Otherwise, the IL width is selected by memory
	 * model: code pointers honour NEAR_CODE, data pointers NEAR_DATA. */
	if (k == FUN)
		return CODEPTR_T();
	if (k == PTR) {
		if (ISFAR(ctyp))
			return 'l';
		if (KIND(DREF(ctyp)) == FUN)
			return CODEPTR_T();
		return DATAPTR_T();
	}
	if (ISFLOAT(ctyp)) {
		/* No soft-double on i8086: double is aliased to single (Ks), so
		 * every float — including any leftover LNG|FLOAT — emits as 's'.
		 * This keeps a stray Kd from ever reaching the backend (which has
		 * no double support). */
		return 's';
	}
	/* Characters are bytes */
	if (k == CHR) return 'b';
	/* Short ints are halfwords - check KIND to avoid false positives from shifted types */
	if ((ctyp & SHORT) && k == INT) return 'h';
	/* 32-bit longs on i8086 and 8-byte types */
	if (k == LNG || SIZE(ctyp) == 8) return 'l';
	/* Regular ints are words */
	return 'w';
}

/* QBE return type - for function return types which must be w, l, s, d */
char
irtyp_ret(unsigned ctyp)
{
	if (KIND(ctyp) == FUN)
		return CODEPTR_T();
	if (KIND(ctyp) == PTR) {
		if (ISFAR(ctyp))
			return 'l';
		if (KIND(DREF(ctyp)) == FUN)
			return CODEPTR_T();
		return DATAPTR_T();
	}
	if (ISFLOAT(ctyp)) {
		/* double aliases to single (Ks) on i8086 — see irtyp(). */
		return 's';
	}
	if (KIND(ctyp) == LNG) return 'l';  /* 32-bit long on i8086 (SIZE 4) */
	if (SIZE(ctyp) == 8) return 'l';
	return 'w';  /* char, short, int all return as 'w' */
}

/* Return QBE alignment for alloc instruction (4, 8, or 16) */
int
iralign(unsigned ctyp)
{
	int s = SIZE(ctyp);
	if (s <= 4) return 4;
	if (s <= 8) return 8;
	/* For larger types, use 4-byte alignment (struct members are typically 4-byte aligned) */
	return 4;
}

/* Format a `{ ... }` data initializer for a zero-initialized global of
 * type `ctyp`.  Scalars use `{ <kind> 0 }`; struct/union types reserve
 * SIZE(ctyp) bytes via `align N { z S }` so multi-word structs (e.g.
 * `LPTR { LINE *linep; int index; }` = 4 bytes) don't overlap the next
 * global symbol the way a single `{ w 0 }` (2 bytes on i8086) would. */
static void
emit_zero_init(char *buf, unsigned ctyp)
{
	if (KIND(ctyp) == STRUCT_T || KIND(ctyp) == UNION_T)
		sprintf(buf, "align %d { z %d }", iralign(ctyp), SIZE(ctyp));
	else
		sprintf(buf, "{ %c 0 }", irtyp(ctyp));
}

/* Under MHuge, a global array whose total size won't fit in a 64K
 * DGROUP segment is moved into its own per-symbol segment
 * (`_HUGE_<symname>`).  asm_to_omf.py recognises the section override
 * and splits the segment across paragraph-aligned chunks; omf_link.py
 * places the chunks outside DGROUP at consecutive paragraph bases so
 * pointer arithmetic through `_qbe_huge_add` normalises into a
 * contiguous linear region.  Returns 1 if the override was applied. */
static int
maybe_mark_huge_global(int idx, char *symname, int total_bytes)
{
	int n;
	if (memmodel != MHuge) return 0;
	if (total_bytes <= 65536) return 0;
	if (symname == 0 || symname[0] == 0) return 0;
	n = snprintf(glosec[idx], sizeof glosec[idx], "_HUGE_%s", symname);
	if (n < 0 || n >= (int)sizeof glosec[idx])
		die("huge-section name too long");
	return 1;
}

/* Emit a function-local `static` as a file-scope data global with a
 * mangled name (`_<fnname>_<varname>`), and register the source name
 * in the local symbol table as a reference to that global.  varclr
 * clears the entry between functions so two functions can both have
 * `static int pos;` without colliding.
 *
 *   name      — source identifier as written in C
 *   sym_ctyp  — type to register in symtab (for arrays: pointer-to-elem)
 *   isarray   — passed through to varadd (so address-of decays correctly)
 *   init_buf  — full QBE init body (e.g. "{ w 42 }" or "align 4 { z 12 }").
 *
 * Linearly probes the symtab post-varadd to set isstaticlocal=1; that
 * flag makes varclr discard the entry on the next varclr.
 */
static void
emit_static_local(char *name, unsigned sym_ctyp, int isarray, char *init_buf)
{
	char mangled[NString];
	char srcname[NString];
	unsigned h0, h;
	int n;

	if (cur_fn_name[0] == 0)
		die("static local outside function context");
	/* The internal storage symbol is mangled from the ORIGINAL source name
	 * (`_<fn>_<name>`); compute it before any shadow-rename so the emitted
	 * global symbol stays `$`-free for the assembler. */
	n = snprintf(mangled, sizeof mangled, "_%s_%s", cur_fn_name, name);
	if (n < 0 || n >= (int)sizeof mangled)
		die("static-local mangled name too long");
	if (nglo == NGlo)
		die("too many globals");
	ini[nglo] = alloc(strlen(init_buf) + 1);
	strcpy(ini[nglo], init_buf);
	strcpy(gloname[nglo], mangled);
	glostatic[nglo] = 1;  /* function-local static: internal linkage (§6b) */
	/* A static local shadows any file-scope binding of the same name: route
	 * the SOURCE name through the block-scope renamer (§7d) so body uses
	 * resolve to this slot and varadd doesn't die "double definition".  The
	 * storage symbol (above) is unaffected; only the symtab key + lexer
	 * rename change.  No-collision case leaves srcname unchanged. */
	strcpy(srcname, name);
	block_scope_rename(srcname, sym_ctyp, isarray);
	varadd(srcname, nglo, sym_ctyp, isarray);
	h0 = hash(srcname);
	h = h0;
	do {
		if (strcmp(varh[h].v, srcname) == 0) {
			varh[h].isstaticlocal = 1;
			break;
		}
		h = (h + 1) % NVar;
	} while (h != h0);
	nglo++;
}

void
psymb(Symb s)
{
	switch (s.t) {
	case Tmp:
		fprintf(of, "%%t%d", s.u.n);
		break;
	case Var:
		fprintf(of, "%%%s", s.u.v);
		break;
	case Glo:
		/* Reference globals by their source name when available so the
		 * generated symbol matches what other translation units expect
		 * via `extern` declarations. */
		if (s.u.n > 0 && s.u.n < NGlo && gloname[s.u.n][0] != 0)
			fprintf(of, "$%s", gloname[s.u.n]);
		else
			fprintf(of, "$glo%d", s.u.n);
		break;
	case Ext:
		fprintf(of, "$%s", s.u.v);
		break;
	case Con:
		fprintf(of, "%d", s.u.n);
		break;
	}
}

/* Format a Symb's address operand into `buf` (the same textual form psymb
 * prints).  Lets the address-string helpers emit_zero_aggr/emit_clit_aggr
 * target an arbitrary aggregate lvalue (a local %var, a *p deref temp, ...),
 * not just a %_clit compound-literal slot. */
static void
symb_operand(Symb s, char *buf, size_t n)
{
	switch (s.t) {
	case Tmp:
		snprintf(buf, n, "%%t%d", s.u.n);
		break;
	case Var:
		snprintf(buf, n, "%%%s", s.u.v);
		break;
	case Glo:
		if (s.u.n > 0 && s.u.n < NGlo && gloname[s.u.n][0] != 0)
			snprintf(buf, n, "$%s", gloname[s.u.n]);
		else
			snprintf(buf, n, "$glo%d", s.u.n);
		break;
	case Ext:
		snprintf(buf, n, "$%s", s.u.v);
		break;
	case Con:
		snprintf(buf, n, "%d", s.u.n);
		break;
	default:
		buf[0] = 0;
		break;
	}
}

void
sext(Symb *s)
{
	/* Sign-extending a COMPILE-TIME CONSTANT: don't emit `extsw`.  On
	 * i8086 the `w` class is 16-bit, so `=l extsw <const>` makes the
	 * backend sign-extend only the low 16 bits — truncating any constant
	 * that needs more (e.g. `long x = 555666L` became 31250) and
	 * misreading bit 15 of an in-range value (`40000` -> negative).  The
	 * Con already holds the full intended value in u.n (the lexer stored
	 * the host-int value), so just retype it LNG; psymb prints the whole
	 * number and the backend materializes the 32-bit constant. */
	if (s->t == Con) {
		s->ctyp = LNG;
		return;
	}
	fprintf(of, "\t%%t%d =l extsw ", tmp);
	psymb(*s);
	fprintf(of, "\n");
	s->t = Tmp;
	s->ctyp = LNG;
	s->u.n = tmp++;
}

void
widen_int_to_long(Symb *s)
{
	if (s->t == Con) {
		s->ctyp = ISUNSIGNED(s->ctyp) ? (LNG | UNSIGNED) : LNG;
		return;
	}
	fprintf(of, "\t%%t%d =l %s ", tmp, ISUNSIGNED(s->ctyp) ? "extuw" : "extsw");
	psymb(*s);
	fprintf(of, "\n");
	s->t = Tmp;
	s->ctyp = ISUNSIGNED(s->ctyp) ? (LNG | UNSIGNED) : LNG;
	s->u.n = tmp++;
}

unsigned
prom(int op, Symb *l, Symb *r)
{
	Symb *t;
	int sz;

	/* Floating-point promotion: if either operand is float/double, promote both */
	if (ISFLOAT(l->ctyp) || ISFLOAT(r->ctyp)) {
		/* C usual arithmetic conversions: the common floating type is the
		 * widest operand — double only if either operand is itself double;
		 * a float combined with an integer (or another float) stays float
		 * and the integer converts to float, NOT double.  This also keeps
		 * unary minus on a float — which mkneg desugars to `0 - x` — single
		 * precision (Ks), instead of promoting to Kd which the soft-float
		 * backend cannot lower. */
		int l_dbl = ISFLOAT(l->ctyp) && KIND(l->ctyp) == LNG;
		int r_dbl = ISFLOAT(r->ctyp) && KIND(r->ctyp) == LNG;
		unsigned target_type = (l_dbl || r_dbl) ? (LNG | FLOAT)
		                                        : (INT | FLOAT);

		/* Convert integer to floating-point if needed */
		if (!ISFLOAT(l->ctyp)) {
			fprintf(of, "\t%%t%d =%c ", tmp, irtyp(target_type));
			if (KIND(l->ctyp) == LNG)
				fprintf(of, "sltof ");
			else
				fprintf(of, "swtof ");
			psymb(*l);
			fprintf(of, "\n");
			l->t = Tmp;
			l->ctyp = target_type;
			l->u.n = tmp++;
		} else if ((KIND(l->ctyp) == LNG) != (KIND(target_type) == LNG)) {
			/* Convert float to double or vice versa — only on a real
			 * precision change (compare KIND, not the full ctyp, so the
			 * far-data FAR bit on a float value doesn't force a bogus
			 * float->float truncd). */
			fprintf(of, "\t%%t%d =%c ", tmp, irtyp(target_type));
			if (KIND(target_type) == LNG)
				fprintf(of, "exts ");  /* float to double */
			else
				fprintf(of, "truncd ");  /* double to float */
			psymb(*l);
			fprintf(of, "\n");
			l->t = Tmp;
			l->ctyp = target_type;
			l->u.n = tmp++;
		}

		if (!ISFLOAT(r->ctyp)) {
			fprintf(of, "\t%%t%d =%c ", tmp, irtyp(target_type));
			if (KIND(r->ctyp) == LNG)
				fprintf(of, "sltof ");
			else
				fprintf(of, "swtof ");
			psymb(*r);
			fprintf(of, "\n");
			r->t = Tmp;
			r->ctyp = target_type;
			r->u.n = tmp++;
		} else if ((KIND(r->ctyp) == LNG) != (KIND(target_type) == LNG)) {
			/* Convert float to double or vice versa — only on a real
			 * precision change (compare KIND, not the full ctyp; see the
			 * matching note on the left operand above). */
			fprintf(of, "\t%%t%d =%c ", tmp, irtyp(target_type));
			if (KIND(target_type) == LNG)
				fprintf(of, "exts ");  /* float to double */
			else
				fprintf(of, "truncd ");  /* double to float */
			psymb(*r);
			fprintf(of, "\n");
			r->t = Tmp;
			r->ctyp = target_type;
			r->u.n = tmp++;
		}

		return target_type;
	}

	/* Promote char to int for comparisons (both operands must be int or larger).
	 * Honor each operand's signedness: an unsigned char zero-extends (extub),
	 * a signed char sign-extends (extsb) — see the int=char assignment note. */
	if (strchr("ne<l", op) && KIND(l->ctyp) == CHR && KIND(r->ctyp) == CHR) {
		fprintf(of, "\t%%t%d =w %s ", tmp, ISUNSIGNED(l->ctyp) ? "extub" : "extsb");
		psymb(*l);
		fprintf(of, "\n");
		l->t = Tmp;
		l->ctyp = INT;
		l->u.n = tmp++;
		fprintf(of, "\t%%t%d =w %s ", tmp, ISUNSIGNED(r->ctyp) ? "extub" : "extsb");
		psymb(*r);
		fprintf(of, "\n");
		r->t = Tmp;
		r->ctyp = INT;
		r->u.n = tmp++;
		return INT;
	}

	if (l->ctyp == r->ctyp && KIND(l->ctyp) != PTR)
		return l->ctyp;

	/* Promote char to int (zero-extend unsigned char, sign-extend signed) */
	if (KIND(l->ctyp) == CHR && KIND(r->ctyp) != CHR) {
		/* Extend char to int */
		fprintf(of, "\t%%t%d =w %s ", tmp, ISUNSIGNED(l->ctyp) ? "extub" : "extsb");
		psymb(*l);
		fprintf(of, "\n");
		l->t = Tmp;
		l->ctyp = INT;
		l->u.n = tmp++;
	}
	if (KIND(r->ctyp) == CHR && KIND(l->ctyp) != CHR) {
		fprintf(of, "\t%%t%d =w %s ", tmp, ISUNSIGNED(r->ctyp) ? "extub" : "extsb");
		psymb(*r);
		fprintf(of, "\n");
		r->t = Tmp;
		r->ctyp = INT;
		r->u.n = tmp++;
	}

	/* Promote int to long (handles both signed and unsigned) */
	if (KIND(l->ctyp) == LNG && KIND(r->ctyp) == INT) {
		widen_int_to_long(r);
		/* Return unsigned long if l is unsigned, else signed long */
		return ISUNSIGNED(l->ctyp) ? (LNG | UNSIGNED) : LNG;
	}
	if (KIND(l->ctyp) == INT && KIND(r->ctyp) == LNG) {
		widen_int_to_long(l);
		/* Return unsigned long if r is unsigned, else signed long */
		return ISUNSIGNED(r->ctyp) ? (LNG | UNSIGNED) : LNG;
	}

	/* Pointer subtraction yields ptrdiff_t: handle BEFORE the same-kind
	 * early return so the result type is the difference, not pointer.
	 * ptrdiff_t is 16-bit (INT/Kw) for near pointers, 32-bit (LNG/Kl) for
	 * far -- a near char* difference typed LNG produces a class-inconsistent
	 * `l sub` of two `w` operands, which trips QBE gvn assoccon's width
	 * assert (gvn.c:210).  Mirror the far-aware logic at the second '-'
	 * handler below. */
	if (op == '-' && KIND(l->ctyp) == PTR && KIND(r->ctyp) == PTR) {
		if (l->ctyp != r->ctyp)
			die("non-homogeneous pointers in substraction");
		return ISFAR(l->ctyp) ? LNG : INT;
	}

	/* Handle unsigned type promotion */
	if (KIND(l->ctyp) == KIND(r->ctyp)) {
		/* Same base type, possibly different signedness */
		/* Promote to unsigned if either is unsigned */
		if (ISUNSIGNED(l->ctyp) || ISUNSIGNED(r->ctyp))
			return KIND(l->ctyp) | UNSIGNED;
		return l->ctyp;
	}

	/* Comparisons (==, !=, <, <=) do not do pointer arithmetic.  When a
	 * pointer is compared (typically against a null constant, or another
	 * pointer), return the pointer operand's type directly.  The Scale
	 * path below would otherwise compute SIZE(DREF(ptr)) -- fatal for
	 * void* / incomplete pointees -- and would wrongly multiply a
	 * comparison operand by the element size. */
	if (strchr("ne<l", op) && (KIND(l->ctyp) == PTR || KIND(r->ctyp) == PTR))
		return KIND(l->ctyp) == PTR ? l->ctyp : r->ctyp;

	if (op == '+') {
		if (KIND(r->ctyp) == PTR) {
			t = l;
			l = r;
			r = t;
		}
		if (KIND(r->ctyp) == PTR)
			die("pointers added");
		goto Scale;
	}

	if (op == '-') {
		if (KIND(l->ctyp) != PTR)
			die("pointer substracted from integer");
		if (KIND(r->ctyp) != PTR)
			goto Scale;
		if (l->ctyp != r->ctyp)
			die("non-homogeneous pointers in substraction");
		/* ptrdiff_t: 16-bit for near pointers, 32-bit for far. */
		return ISFAR(l->ctyp) ? LNG : INT;
	}

Scale:
	sz = SIZE(DREF(l->ctyp));
	if (r->t == Con)
		r->u.n *= sz;
	else {
		char pt = irtyp(l->ctyp);  /* 'w' near, 'l' far */
		if (pt == 'l' && irtyp(r->ctyp) != 'l') {
			/* Under huge, the FULL 32-bit scaled index reaches
			 * _qbe_huge_add and is added to the 20-bit linear
			 * address, so an UNSIGNED index whose 16-bit value is
			 * >= 0x8000 (e.g. a size_t byte offset into a >32 KB
			 * object) must ZERO-extend: a sign-extend makes it
			 * negative and mis-addresses BELOW the object — the §4i
			 * gap that the compact/large addfo/subfo fix never
			 * reached.  widen_int_to_long picks extuw/extsw by the
			 * source signedness.  Under compact/large the offset-only
			 * addfo/subfo ops read only the low 16 bits, where extsw
			 * and extuw agree, so the uniform sext stays harmless
			 * there — keep it to preserve the byte-identical
			 * MP-compact corpus.  See [[project-far-ptr-unsigned-index-bug]]. */
			if (memmodel == MHuge)
				widen_int_to_long(r);
			else
				sext(r);
		}
		fprintf(of, "\t%%t%d =%c mul %d, ", tmp, pt, sz);
		psymb(*r);
		fprintf(of, "\n");
		r->u.n = tmp++;
		r->ctyp = (pt == 'l') ? LNG : INT;
	}
	return l->ctyp;
}

void loadfar(Symb d, Symb s);  /* fwd decl: load() delegates to it for FARSTORAGE */

void
load(Symb d, Symb s)
{
	char t;

	/* Direct access to a global/extern datum under a far-data model must
	 * go through the far load path — its storage lives in a far segment
	 * (or DGROUP reached via ES).  Delegate to loadfar so every caller of
	 * load() (the `V` variable read, member loads, inc/dec) gets it for
	 * free.  See FARSTORAGE / [[minic-far-data-segment]]. */
	if (FARSTORAGE(s)) {
		loadfar(d, s);
		return;
	}

	fprintf(of, "\t");
	psymb(d);
	t = irtyp(d.ctyp);

	/* QBE doesn't support byte/halfword temporaries, load into words */
	if (t == 'b' || t == 'h') {
		/* Use word temporary for byte/halfword loads */
		if (ISUNSIGNED(d.ctyp)) {
			fprintf(of, " =w loadu%c ", t);
		} else {
			fprintf(of, " =w loads%c ", t);
		}
	} else {
		fprintf(of, " =%c load%c ", t, t);
	}
	/* A volatile access must re-read memory every time: emit the QBE
	 * `volatile` keyword between the load opcode and its address operand
	 * so loadopt won't forward a prior store and gcm won't elide/reorder
	 * it.  Two triggers: a volatile global/extern NAMED symbol (`s`), or a
	 * volatile-qualified VALUE TYPE — the pointee of a `volatile T *`
	 * recovered by DREF, carried on d.ctyp.  No-op (byte-identical) when
	 * neither holds. */
	fprintf(of, "%s", (symb_isvolatile(s) || ISVOLATILE(d.ctyp)) ? "volatile " : "");
	psymb(s);
	fprintf(of, "\n");
}

/*
 * Load through a far pointer (segment:offset)
 * Uses the loadfb/loadfh/loadfw operations for i8086 far memory access
 */
void
loadfar(Symb d, Symb s)
{
	char t;

	fprintf(of, "\t");
	psymb(d);
	t = irtyp(d.ctyp);

	/* Far pointer loads - use loadfb/loadfh/loadfw/loadfl.
	 * `l` (LNG/long, 4 bytes on i8086) gets its own op so the high 16
	 * bits aren't silently truncated — pre-fix the else branch routed
	 * `long` reads through `loadfw` and dropped the high half.  See
	 * [[storefar-lacks-storefl]]. */
	if (t == 'b') {
		fprintf(of, " =w loadfb ");
	} else if (t == 'h') {
		fprintf(of, " =w loadfh ");
	} else if (t == 'l') {
		fprintf(of, " =l loadfl ");  /* 32-bit long through far ptr */
	} else if (t == 's') {
		fprintf(of, " =s loadfs ");  /* 32-bit single-float through far ptr */
	} else {
		fprintf(of, " =w loadfw ");  /* Word (16-bit) load through far ptr */
	}
	/* Volatile through the far path (far-data model): keep the access for
	 * a volatile global/extern OR a volatile-qualified pointee (d.ctyp) —
	 * same as load() (see that note). */
	fprintf(of, "%s", (symb_isvolatile(s) || ISVOLATILE(d.ctyp)) ? "volatile " : "");
	psymb(s);
	fprintf(of, "\n");
}

/*
 * Store through a far pointer (segment:offset)
 * Uses the storefb/storefh/storefw operations for i8086 far memory access
 */
void
storefar(Symb d, Symb s)
{
	char t;

	t = irtyp(d.ctyp);

	fprintf(of, "\t");
	/* Far pointer stores - use storefb/storefh/storefw/storefl.
	 * `l` (LNG/long) gets its own op; pre-fix the else branch routed
	 * `long` writes through `storefw` and silently truncated the high
	 * 16 bits.  See [[storefar-lacks-storefl]]. */
	if (t == 'b') {
		fprintf(of, "storefb ");
	} else if (t == 'h') {
		fprintf(of, "storefh ");
	} else if (t == 'l') {
		fprintf(of, "storefl ");  /* 32-bit long through far ptr */
	} else if (t == 's') {
		fprintf(of, "storefs ");  /* 32-bit single-float through far ptr */
	} else {
		fprintf(of, "storefw ");  /* Word (16-bit) store through far ptr */
	}
	/* Keep the store for a volatile far destination (named global/extern)
	 * OR a volatile-qualified value type (d.ctyp — a volatile pointee). */
	fprintf(of, "%s", (symb_isvolatile(s) || ISVOLATILE(d.ctyp)) ? "volatile " : "");
	psymb(d);  /* value to store */
	fprintf(of, ", ");
	psymb(s);  /* far pointer address */
	fprintf(of, "\n");
}

/*
 * Copy a struct/union value from one address to another.  `dst` and
 * `src` are address-bearing Symbs (their psymb output IS the address);
 * the copy size comes from `dst`'s aggregate type.  The copy is emitted
 * word-by-word with a trailing byte for odd sizes.  Each side
 * independently selects far (loadfw/storefw + `=l add`) or near
 * (loadw/storew + `=w add`) addressing from its own FAR flag, so
 * `*near = *far` and the reverse each pick the right variant per side.
 *
 * The IR is kept strictly Kw/Kb per element so QBE's loadopt won't fuse
 * adjacent stores into a wider Kl op (which on i8086 triggers a chain of
 * shl/xor scratch).  Used by the struct-assignment path, by `return
 * aggr;` (copy into the hidden return pointer), and by `x = f();` where
 * f returns a struct (copy out of the result slot).
 */
void
emit_struct_copy(Symb dst, Symb src)
{
	Symb off_addr, val;
	int sidx = DREF(dst.ctyp);
	int sz = structh[sidx].size;
	int off;
	/* FARSTORAGE: a global/extern aggregate lives in a far segment, so a
	 * copy where either side is a direct global must use the far per-word
	 * load/store path (and 4-byte address arithmetic) even though the
	 * aggregate's own ctyp carries no FAR bit. */
	int src_far = ISFAR(src.ctyp) || FARSTORAGE(src);
	int dst_far = ISFAR(dst.ctyp) || FARSTORAGE(dst);
	/* §3m(b): a volatile struct-to-struct copy `*d = *s`.  If either operand
	 * is volatile-qualified the corresponding word/byte accesses must carry
	 * the QBE `volatile` keyword so loadopt/gcm don't forward, CSE, or elide
	 * them.  The qualifier reaches here two ways: as the QVOLATILE bit on the
	 * aggregate lvalue's value type (the deref of a `volatile struct S *`
	 * shifts the pointee bit down via DREF; lval `case 'V'` re-derives it for
	 * a directly-declared volatile object), or via the NAMED symbol for a
	 * volatile global/extern aggregate.  src governs the LOADS, dst the STORES.
	 * No-op (byte-identical) when neither side is volatile. */
	int src_vol = symb_isvolatile(src) || ISVOLATILE(src.ctyp);
	int dst_vol = symb_isvolatile(dst) || ISVOLATILE(dst.ctyp);
	char src_klass = src_far ? 'l' : 'w';
	char dst_klass = dst_far ? 'l' : 'w';
	unsigned src_ptyp = src_far ? IDIR_FAR(INT) : IDIR(INT);
	unsigned dst_ptyp = dst_far ? IDIR_FAR(INT) : IDIR(INT);

	off = 0;
	while (off + 1 < sz) {
		if (off > 0) {
			off_addr.t = Tmp;
			off_addr.u.n = tmp++;
			off_addr.ctyp = src_ptyp;
			fprintf(of, "\t");
			psymb(off_addr);
			fprintf(of, " =%c add ", src_klass);
			psymb(src);
			fprintf(of, ", %d\n", off);
		} else {
			off_addr = src;
		}
		val.t = Tmp;
		val.u.n = tmp++;
		val.ctyp = INT;
		fprintf(of, "\t");
		psymb(val);
		fprintf(of, src_far ? " =w loadfw " : " =w loadw ");
		fprintf(of, "%s", src_vol ? "volatile " : "");
		psymb(off_addr);
		fprintf(of, "\n");

		if (off > 0) {
			off_addr.t = Tmp;
			off_addr.u.n = tmp++;
			off_addr.ctyp = dst_ptyp;
			fprintf(of, "\t");
			psymb(off_addr);
			fprintf(of, " =%c add ", dst_klass);
			psymb(dst);
			fprintf(of, ", %d\n", off);
		} else {
			off_addr = dst;
		}
		fprintf(of, dst_far ? "\tstorefw " : "\tstorew ");
		fprintf(of, "%s", dst_vol ? "volatile " : "");
		psymb(val);
		fprintf(of, ", ");
		psymb(off_addr);
		fprintf(of, "\n");
		off += 2;
	}
	if (off < sz) {
		unsigned src_bptyp = src_far ? IDIR_FAR(CHR) : IDIR(CHR);
		unsigned dst_bptyp = dst_far ? IDIR_FAR(CHR) : IDIR(CHR);
		if (off > 0) {
			off_addr.t = Tmp;
			off_addr.u.n = tmp++;
			off_addr.ctyp = src_bptyp;
			fprintf(of, "\t");
			psymb(off_addr);
			fprintf(of, " =%c add ", src_klass);
			psymb(src);
			fprintf(of, ", %d\n", off);
		} else {
			off_addr = src;
		}
		val.t = Tmp;
		val.u.n = tmp++;
		val.ctyp = CHR;
		fprintf(of, "\t");
		psymb(val);
		fprintf(of, src_far ? " =w loadfb " : " =w loadub ");
		fprintf(of, "%s", src_vol ? "volatile " : "");
		psymb(off_addr);
		fprintf(of, "\n");

		if (off > 0) {
			off_addr.t = Tmp;
			off_addr.u.n = tmp++;
			off_addr.ctyp = dst_bptyp;
			fprintf(of, "\t");
			psymb(off_addr);
			fprintf(of, " =%c add ", dst_klass);
			psymb(dst);
			fprintf(of, ", %d\n", off);
		} else {
			off_addr = dst;
		}
		fprintf(of, dst_far ? "\tstorefb " : "\tstoreb ");
		fprintf(of, "%s", dst_vol ? "volatile " : "");
		psymb(val);
		fprintf(of, ", ");
		psymb(off_addr);
		fprintf(of, "\n");
	}
}

/* Zero-fill `s` bytes of compound-literal storage %_clit<clitnum>.
 *
 * On i8086 a `storew`/`storefw` writes 2 bytes (T.wordsz == 2), so the fill
 * MUST step by 2.  The old loops here stepped `j += 4` while emitting 2-byte
 * stores, leaving alternating 2-byte GAPS of stack garbage — so every member
 * of a `S s = {0};` local past the first word read non-zero.  Canonical victim:
 * py/compile.c's `compiler_t comp_state = {0};` — the `compile_error` pointer
 * field (well past the first word) read garbage, so mp_compile raised a bogus
 * exception on the correct parse tree of `print(1+2)`.
 *
 * Under a far-data model %_clit is a Kl (SS:offset) address, so offset arith is
 * `=l add` and the stores are `storefw`/`storefb` — a near `=w add` would
 * truncate the segment and scatter the zeros into the wrong segment.  Mirrors
 * emit_struct_copy's per-element stepping (kept strictly Kw/Kb so loadopt won't
 * fuse adjacent stores into a wider Kl op). */
/* Zero-fill `s` bytes of aggregate storage whose address is the SSA operand
 * `addr` (e.g. "%_clit3" for a compound literal or "%foo" for a named local).
 * See the long note above for the bug history.  Used by the compound-literal
 * 'L' paths and the bare struct-local declaration. */
static void
emit_zero_aggr(const char *addr, int s)
{
	int far = !NEAR_DATA();
	char klass = far ? 'l' : 'w';
	const char *sl = far ? "storefl" : "storel";   /* 4-byte store */
	const char *sw = far ? "storefw" : "storew";   /* 2-byte store */
	const char *sb = far ? "storefb" : "storeb";   /* 1-byte store */
	int off = 0;

	/* For all but the smallest aggregates, a `memset(addr, 0, s)` call is
	 * far smaller than unrolled stores — important because zeroing CORRECTLY
	 * (the bug below was a half-fill) roughly doubles the inline store code,
	 * and the Victor image-size budget is razor-thin.  memset() is in
	 * far_stdlib, so call_target_name mangles it to _far_memset under a
	 * far-data model (4-byte far dest ptr); the near _memset reaches a stack
	 * local fine since SS == DGROUP under near-data.  Tiny aggregates stay
	 * inline to avoid the call overhead. */
	if (s > 8) {
		fprintf(of, "\tcall $%s(%c %s, w 0, w %d, ...)\n",
			call_target_name("memset"), DATAPTR_T(), addr, s);
		return;
	}

	/* The old loops stepped `off += 4` but emitted a 2-byte storew, so they
	 * zeroed only half the bytes (T.wordsz == 2 on i8086).  Keep the 4-byte
	 * stride — which keeps the store count identical, important for the
	 * razor-thin Victor image-size budget — but use a real 4-byte store, then
	 * mop up a 2-byte and/or 1-byte tail for sizes not a multiple of 4. */
	while (off + 4 <= s) {
		if (off == 0)
			fprintf(of, "\t%s 0, %s\n", sl, addr);
		else {
			fprintf(of, "\t%%t%d =%c add %s, %d\n", tmp, klass, addr, off);
			fprintf(of, "\t%s 0, %%t%d\n", sl, tmp);
			tmp++;
		}
		off += 4;
	}
	if (off + 2 <= s) {
		if (off == 0)
			fprintf(of, "\t%s 0, %s\n", sw, addr);
		else {
			fprintf(of, "\t%%t%d =%c add %s, %d\n", tmp, klass, addr, off);
			fprintf(of, "\t%s 0, %%t%d\n", sw, tmp);
			tmp++;
		}
		off += 2;
	}
	if (off < s) {
		if (off == 0)
			fprintf(of, "\t%s 0, %s\n", sb, addr);
		else {
			fprintf(of, "\t%%t%d =%c add %s, %d\n", tmp, klass, addr, off);
			fprintf(of, "\t%s 0, %%t%d\n", sb, tmp);
			tmp++;
		}
	}
}

/* Zero-fill `sz` bytes of the named local `v` (the bitfield-support zero-init
 * for a struct/union local declaration). */
static void
emit_zero_local(char *v, int sz)
{
	char zaddr[NString];
	snprintf(zaddr, sizeof zaddr, "%%%s", v);
	emit_zero_aggr(zaddr, sz);
}

/* True when struct/union `sidx` (or a nested struct/union member) contains a
 * bitfield.  minic implicitly zero-initializes a bare `struct S s;` local only
 * so a later bitfield read-modify-write sees defined bits; C otherwise leaves
 * an uninitialized automatic object indeterminate.  Gating the implicit
 * zero-init on this both matches C and saves a lot of code on the size-tight
 * Victor target (most structs have no bitfields).  Note the old implicit
 * zero-init was a buggy HALF-fill anyway, so no correct program could have
 * relied on it. */
static int
struct_has_bitfield(int sidx)
{
	int i;
	for (i = 0; i < structh[sidx].nmembers; i++) {
		struct Member *m = &structh[sidx].members[i];
		if (m->bitwidth > 0)
			return 1;
		if ((KIND(m->ctyp) == STRUCT_T || KIND(m->ctyp) == UNION_T)
		    && struct_has_bitfield(DREF(m->ctyp)))
			return 1;
	}
	return 0;
}

/* True when an initializer list is exactly `{ 0 }` (the all-zero idiom).  Such
 * a local `S s = {0};` can be lowered to a direct zero-fill of the target,
 * skipping the compound-literal temporary AND the struct copy — both a code-size
 * and a speed win (and the dominant initializer shape in MicroPython, e.g.
 * `compiler_t comp_state = {0};`). */
static int
initlist_is_zero(Node *il)
{
	return il && il->r == 0 && il->l && il->l->op == 'N' && il->l->u.n == 0;
}

/*
 * In far-data memory models (compact/large/huge) default `char *` and
 * other data pointers are 32-bit segment:offset. The stock libstub
 * routines (_printf, _strlen, _strcpy, ...) all read 16-bit near
 * pointers off the stack, so calling them with far pointers would
 * silently shift every subsequent vararg by one slot. Re-route calls
 * to a fixed list of well-known stdlib names to `_far_X` variants
 * that consume 4-byte stack pointers and dereference via ES:[BX].
 *
 * Only data-pointer-bearing routines are mangled; routines that take
 * no pointers (_atoi on a literal int, _isalpha, ...) keep their
 * regular names. User-defined functions of the same name override
 * the libstub variant the same way they always did, but in those
 * compact-only models the user takes responsibility for matching the
 * far-ptr ABI on their declarations.
 */
char *
call_target_name(char *f)
{
	static const char *far_stdlib[] = {
		"printf", "fprintf", "sprintf",
		"puts", "fputs", "fputc", "fgets",
		"fopen", "fclose",
		"fread", "fwrite", "fflush",
		"strlen", "strcpy", "strcmp", "strncmp", "strncpy",
		"strchr", "strcat", "strcspn", "strstr", "strrchr",
		"memcpy", "memmove", "memcmp", "memset",
		"intdos", "int86", "segread",
		"setjmp", "longjmp",
		0
	};
	static char mangled[NString];
	int i;

	if (NEAR_DATA())
		return f;
	for (i=0; far_stdlib[i]; i++) {
		if (strcmp(f, far_stdlib[i]) == 0) {
			/* QBE will prefix the asm symbol with `_`, so emit
			 * `far_X` and let it become `_far_X` at link time. */
			snprintf(mangled, sizeof mangled, "far_%s", f);
			return mangled;
		}
	}
	return f;
}

/* Struct/union return-by-value (caller side): allocate storage for the
 * returned aggregate and emit its alloc.  The returned temp number holds
 * the slot address; the caller passes it as the hidden first argument
 * and then treats it as the call's result (the address of the filled
 * aggregate).  Must be called before evaluating the real arguments so
 * the slot temp number is stable. */
static int
alloc_sret_slot(unsigned aggr_ctyp)
{
	int slot = tmp++;
	fprintf(of, "\t%%t%d =%c alloc%d %d\n", slot, ALLOC_T(),
		iralign(aggr_ctyp), SIZE(aggr_ctyp));
	return slot;
}

static int
is_aggr(unsigned ctyp)
{
	return KIND(ctyp) == STRUCT_T || KIND(ctyp) == UNION_T;
}

/* Record the fixed parameter types of function `name' (declared or defined)
 * for later argument coercion.  `params' is the par0 Node chain; each node's
 * u.v is a parameter name still live in the local symtab, so its type is
 * varget(name)->ctyp.  A variadic `...' contributes no node, so nparam counts
 * only the fixed parameters — exactly the ones an argument must be converted
 * to (true varargs keep their own promoted type).  Overwrites any prior entry
 * (a definition supersedes an earlier prototype). */
static void
fnproto_record(char *name, Node *params, unsigned rett)
{
	unsigned h0, h;
	Node *n;
	int i;
	Symb *s;

	h0 = hash(name);
	h = h0;
	do {
		if (fnproto[h].v[0] == 0 || strcmp(fnproto[h].v, name) == 0) {
			strcpy(fnproto[h].v, name);
			i = 0;
			for (n = params; n && i < NFnParam; n = n->r) {
				s = varget(n->u.v);
				fnproto[h].ptyp[i++] = s ? s->ctyp : INT;
			}
			fnproto[h].nparam = i;
			fnproto[h].rett = rett;
			fnproto[h].has_rett = 1;
			return;
		}
		h = (h + 1) % NVar;
	} while (h != h0);
	/* table full: silently skip (coercion just won't fire for this fn) */
}

/* Build a one-link node carrying a fn-ptr parameter TYPE (in u.n), chained via
 * ->r — the fptpar grammar emits these so fpproto_alloc can read the declared
 * parameter types of a function-pointer declarator (§2q). */
static Node *
mkptype(unsigned ty, Node *rest)
{
	Node *n = mknode('t', 0, rest);
	n->u.n = (int)ty;
	return n;
}

/* Record a function-pointer declarator's RETURN type and fixed parameter
 * types (the `chain' of mkptype nodes from fptpar0) into fpproto[] and return
 * its index, or -1 if the table is full or there is nothing worth recording.
 * A `...' contributes no node (the chain ends), so nparam counts only the
 * fixed parameters.  A float return is recorded even with an empty parameter
 * list: the IDIR(FUNC(ret)) encoding destroys its FLOAT flag (see the fpproto
 * struct comment), so the side table is the only place the call site can
 * recover the result class from. */
static int
fpproto_alloc(unsigned rett, Node *chain)
{
	Node *n;
	int i = 0, id;

	if ((chain == 0 && !(rett & (FLOAT | (FLOAT << 3)))) || nfpproto >= NFp)
		return -1;     /* FLOAT<<3 also catches a float* return: its
		                * FLOAT bit sits one IDIR up and still lands on
		                * FAR after FUNC's shift */
	id = nfpproto++;
	fpproto[id].rett = rett;
	for (n = chain; n && i < NFpParam; n = n->r)
		fpproto[id].ptyp[i++] = (unsigned)n->u.n;
	fpproto[id].nparam = i;
	return id;
}

/* Set the fn-ptr prototype index on an existing varh entry (after varadd). */
static void
varsetfpid(char *v, int fpid)
{
	unsigned h0, h;

	h0 = hash(v);
	h = h0;
	do {
		if (strcmp(varh[h].v, v) == 0) {
			varh[h].fpid = fpid;
			return;
		}
		h = (h+1) % NVar;
	} while (h != h0 && varh[h].v[0] != 0);
}

/* Return the fn-ptr prototype index recorded for variable `v', or -1. */
static int
varfpid(char *v)
{
	unsigned h0, h;

	h0 = hash(v);
	h = h0;
	do {
		if (strcmp(varh[h].v, v) == 0)
			return varh[h].fpid;
		h = (h+1) % NVar;
	} while (h != h0 && varh[h].v[0] != 0);
	return -1;
}

/* Stamp the fn-ptr prototype index onto the most-recently-added member of
 * struct/union `sidx' (the one the fn-ptr member rule just created). */
static void
structset_last_fpid(int sidx, int fpid)
{
	int n = structh[sidx].nmembers;
	if (n > 0)
		structh[sidx].members[n-1].fpid = fpid;
}

/* Look up function `name' in the prototype table.  Returns its index, or -1
 * if no prototype was recorded (e.g. an implicitly-declared or K&R-unspecified
 * function — leave its arguments untouched). */
static int
fnproto_find(char *name)
{
	unsigned h0, h;

	h0 = hash(name);
	h = h0;
	do {
		if (fnproto[h].v[0] == 0)
			return -1;
		if (strcmp(fnproto[h].v, name) == 0)
			return (int)h;
		h = (h + 1) % NVar;
	} while (h != h0);
	return -1;
}

/* Coerce a call argument value `s' to the declared parameter type `ptyp'
 * (C11 6.5.2.2p7).  Integer-scalar width mismatches are fixed (the case that
 * shifts the stack-argument layout), and int<->float mismatches get the REAL
 * C argument conversion; pointer and by-value aggregate arguments keep their
 * own type. */
static Symb
coerce_arg(Symb s, unsigned ptyp)
{
	char ac, pc;

	/* By-value aggregates cross by pointer via eval_arg/emit_arg. */
	if (is_aggr(s.ctyp) || is_aggr(ptyp))
		return s;
	/* Integer argument to a prototyped FLOAT parameter: a real conversion,
	 * not a width fix.  Without it the raw integer word lands in the
	 * callee's binary32 slot and reads back as a denormal (~1e-44) — e.g.
	 * parsenum.c's powf(5, -dec_exp) became powf(eps, eps) ~= 1.0, so the
	 * decimal-exponent scaling of every MicroPython float literal was a
	 * silent no-op (§4x). */
	if (ISFLOAT(ptyp) && !ISFLOAT(s.ctyp)) {
		fprintf(of, "\t%%t%d =%c %s ", tmp, irtyp_ret(ptyp),
		    KIND(s.ctyp) == LNG ? "sltof" : "swtof");
		psymb(s);
		fprintf(of, "\n");
		s.t = Tmp;
		s.ctyp = ptyp;
		s.u.n = tmp++;
		return s;
	}
	/* Float argument to a prototyped INTEGER parameter.  The source is
	 * always single-precision (Ks) on this target, so the op is stosi for
	 * any integer dest width — a Kl result takes the full _sf_to_int DX:AX
	 * (the §3z Ostosi-with-Kl-result path); dtosi would fail QBE's
	 * typecheck (it wants a Kd operand, which never exists here). */
	if (!ISFLOAT(ptyp) && ISFLOAT(s.ctyp)) {
		fprintf(of, "\t%%t%d =%c %s ", tmp, irtyp_ret(ptyp),
		    "stosi");
		psymb(s);
		fprintf(of, "\n");
		s.t = Tmp;
		s.ctyp = ptyp;
		s.u.n = tmp++;
		return s;
	}
	/* float -> float: single precision only on this target, nothing to fix. */
	if (ISFLOAT(ptyp))
		return s;
	ac = irtyp_ret(s.ctyp);   /* 'w' or 'l' */
	pc = irtyp_ret(ptyp);
	if (ac == pc)
		return s;
	if (pc == 'l') {
		/* widen w -> l */
		if (s.t == Con) {
			s.ctyp = ISUNSIGNED(ptyp) ? (LNG | UNSIGNED) : LNG;
			return s;
		}
		if (ISUNSIGNED(s.ctyp)) {
			fprintf(of, "\t%%t%d =l extuw ", tmp);
			psymb(s);
			fprintf(of, "\n");
			s.t = Tmp; s.ctyp = LNG | UNSIGNED; s.u.n = tmp++;
		} else {
			sext(&s);  /* Con-aware signed widen to LNG */
		}
	} else {
		/* narrow l -> w */
		unsigned rc = (KIND(ptyp) == CHR) ? CHR : INT;
		if (ISUNSIGNED(ptyp))
			rc |= UNSIGNED;
		if (s.t == Con) {
			s.ctyp = rc;
			return s;
		}
		fprintf(of, "\t%%t%d =w copy ", tmp);
		psymb(s);
		fprintf(of, "\n");
		s.t = Tmp; s.ctyp = rc; s.u.n = tmp++;
	}
	return s;
}

/* Struct/union pass-BY-VALUE ABI.  A by-value aggregate argument crosses the
 * call boundary as a POINTER to its storage: the caller yields the aggregate's
 * address, the callee copies *ptr into its own local (C copy semantics).  The
 * rule is type-driven on both ends (aggregate <-> one pointer slot), so caller
 * and callee agree across separate compilation.  Without this minic emitted a
 * single scalar word for a whole struct argument, truncating every member past
 * the first — e.g. mp_lexer_new(qstr, mp_reader_t) dropped the reader's
 * readbyte far fn-ptr, so next_char's first indirect call went wild. */
static Symb
eval_arg(Node *a)
{
	Symb s = expr(a->l);
	if (is_aggr(s.ctyp)) {
		int op = a->l->op;
		/* expr() already yields the result-slot address for a call /
		 * indirect call / compound literal; for an lvalue (V/@/.) it
		 * loaded a scalar word, so re-derive the address (that dead load
		 * is DCE'd).  Re-running lval on C/I/L would emit it twice. */
		if (op != 'C' && op != 'I' && op != 'L')
			s = lval(a->l);
	}
	return s;
}

static void
emit_arg(Symb s)
{
	if (is_aggr(s.ctyp))
		fprintf(of, "%c ", DATAPTR_T());   /* struct passed by pointer */
	else
		fprintf(of, "%c ", irtyp_ret(s.ctyp));
	psymb(s);
	fprintf(of, ", ");
}

/* Prologue binding for parameter `name' (type *s) delivered in %t<t>: alloc
 * local storage and store the incoming value.  A by-value aggregate arrives as
 * a POINTER to the caller's copy, so copy the pointed-to struct into our local
 * storage (and use far load/store under a far-data model — see eval_arg). */
static void
bind_param(char *name, Symb *s, int t)
{
	fprintf(of, "\t%%%s =%c alloc%d %d\n", name, ALLOC_T(),
		iralign(s->ctyp), SIZE(s->ctyp));
	if (is_aggr(s->ctyp)) {
		Symb dst = *s, src;        /* *s is the Var symbol (its u.v = name) */
		unsigned fbit = NEAR_DATA() ? 0 : FAR;
		dst.ctyp = s->ctyp | fbit;
		src.t = Tmp; src.u.n = t; src.ctyp = s->ctyp | fbit;
		emit_struct_copy(dst, src);
	} else {
		fprintf(of, "\tstore%c %%t%d, %%%s\n", irtyp(s->ctyp), t, name);
	}
}

void
call(Node *n, Symb *sr)
{
	Node *a;
	char *f;
	unsigned ft;
	Symb *sv;

	f = n->l->u.v;

	/* va_start: <stdarg.h> expands `va_start(ap, last)` to
	 * `((ap) = (va_list)__builtin_va_argptr())`.  Emit the i8086 `vargp`
	 * op, which the backend lowers to a pointer (SS:bp+vararg_off) to the
	 * first variadic argument of the enclosing variadic function.  The
	 * subsequent `va_arg(ap, T)` macro is pure pointer arithmetic + a far
	 * load, so no further builtin is needed.  See
	 * [[project-minic-vararg-stub]]. */
	if (strcmp(f, "__builtin_va_argptr") == 0) {
		sr->t = Tmp;
		sr->u.n = tmp++;
		sr->ctyp = IDIR(NIL) | (NEAR_DATA() ? 0 : FAR);  /* void* */
		fprintf(of, "\t");
		psymb(*sr);
		fprintf(of, " =%c vargp\n", DATAPTR_T());
		return;
	}

	sv = varget(f);
	if (sv) {
		ft = sv->ctyp;
		/* Check if this is a function pointer - if so, do indirect call */
		if (KIND(ft) == PTR && KIND(DREF(ft)) == FUN) {
			/* Function pointer: generate indirect call */
			Symb fptr;
			unsigned fptr_type = DREF(ft);  /* FUN(return_type) */
			int sret;
			unsigned aggr;
			int sret_slot = 0;
			int fpid = varfpid(f);
			sr->ctyp = DREF(fptr_type);     /* return_type */
			/* The double-DREF decode can LOSE a float return:
			 * pre-§5c the FLOAT flag shifted onto FAR (now a
			 * `float *` return shifts onto QVOLATILE instead),
			 * and DREF strips both.  Recover the declared type
			 * from the side table when the declarator recorded
			 * one (§5b). */
			if (fpid >= 0)
				sr->ctyp = fpproto[fpid].rett;
			sret = (KIND(sr->ctyp) == STRUCT_T || KIND(sr->ctyp) == UNION_T);
			aggr = sr->ctyp;

			/* Load the function pointer value */
			fptr.t = Tmp;
			fptr.u.n = tmp++;
			fptr.ctyp = ft;
			load(fptr, *sv);

			/* Struct/union return-by-value: alloc result storage and
			 * pass its address as the hidden first argument. */
			if (sret)
				sret_slot = alloc_sret_slot(aggr);

			/* Evaluate all arguments, coercing each to the fn-ptr's
			 * declared parameter type (§2q) — a width mismatch on an
			 * indirect call shifts every later stack slot just like a
			 * direct call (fnproto path), but here the prototype comes
			 * from the fn-ptr variable's recorded fpid, not its name. */
			{
				int argi = 0;
				for (a=n->r; a; a=a->r, argi++) {
					a->u.s = eval_arg(a);
					if (fpid >= 0 && argi < fpproto[fpid].nparam)
						a->u.s = coerce_arg(a->u.s,
						    fpproto[fpid].ptyp[argi]);
				}
			}

			/* Generate indirect call */
			if (sret) {
				fprintf(of, "\tcall ");
				psymb(fptr);
				fprintf(of, "(%c %%t%d, ", DATAPTR_T(), sret_slot);
			} else if (sr->ctyp == NIL) {
				/* Void function pointer - no return value */
				fprintf(of, "\tcall ");
				psymb(fptr);
				fprintf(of, "(");
			} else {
				fprintf(of, "\t");
				psymb(*sr);
				fprintf(of, " =%c call ", irtyp_ret(sr->ctyp));
				psymb(fptr);
				fprintf(of, "(");
			}
			for (a=n->r; a; a=a->r)
				emit_arg(a->u.s);
			fprintf(of, "...)\n");
			if (sret) {
				sr->t = Tmp;
				sr->u.n = sret_slot;
				sr->ctyp = aggr | (NEAR_DATA() ? 0 : FAR);
			}
			return;
		}
		if (KIND(ft) != FUN)
			die("invalid call");
	} else if (strcmp(f, "alloca") == 0 ||
	           strcmp(f, "__builtin_alloca") == 0) {
		/* alloca is a compiler builtin in gcc/clang and MicroPython
		 * calls it without a declaration; treat it as returning a
		 * (data) void pointer so `T *p = alloca(...)` type-checks. */
		ft = FUNC(IDIR(NIL));
	} else
		ft = FUNC(INT);
	sr->ctyp = DREF(ft);
	{
	/* Struct/union return-by-value: alloc the result slot and pass its
	 * address as the hidden first argument.  The slot alloc must precede
	 * argument evaluation so its temp number stays stable. */
	int sret;
	unsigned aggr;
	int sret_slot = 0;
	int proto = fnproto_find(f);
	int argi = 0;
	/* §5c: the DREF(FUNC(ret)) decode strips any ret bit that lands on
	 * the FAR/QVOLATILE flag positions — e.g. a `float **` return's
	 * FLOAT flag, three encoding shifts up.  Prefer the recorded
	 * declared return type (carried unshifted), the direct-call mirror
	 * of the §5b fpproto.rett fix. */
	if (proto >= 0 && fnproto[proto].has_rett)
		sr->ctyp = fnproto[proto].rett;
	sret = (KIND(sr->ctyp) == STRUCT_T || KIND(sr->ctyp) == UNION_T);
	aggr = sr->ctyp;
	if (sret)
		sret_slot = alloc_sret_slot(aggr);
	for (a=n->r; a; a=a->r, argi++) {
		a->u.s = eval_arg(a);
		/* Convert each argument to the declared parameter type (C11
		 * 6.5.2.2p4) so its stack width matches what the callee reads;
		 * leave true-vararg arguments (index >= nparam) untouched. */
		if (proto >= 0 && argi < fnproto[proto].nparam)
			a->u.s = coerce_arg(a->u.s, fnproto[proto].ptyp[argi]);
	}
	{
	char *cf = call_target_name(f);
	if (sret) {
		/* Hidden return pointer first; the returned pointer is
		 * discarded since we already hold the slot address. */
		fprintf(of, "\tcall $%s(%c %%t%d, ", cf, DATAPTR_T(), sret_slot);
	} else if (sr->ctyp == NIL) {
		/* Void function - no return value */
		fprintf(of, "\tcall $%s(", cf);
	} else {
		fprintf(of, "\t");
		psymb(*sr);
		fprintf(of, " =%c call $%s(", irtyp_ret(sr->ctyp), cf);
	}
	}
	for (a=n->r; a; a=a->r)
		emit_arg(a->u.s);
	fprintf(of, "...)\n");
	if (sret) {
		/* The call result IS the result slot's address (an aggregate
		 * lvalue).  Mark it far under far-data so downstream copies
		 * use the far load/store variants and 4-byte address arith. */
		sr->t = Tmp;
		sr->u.n = sret_slot;
		sr->ctyp = aggr | (NEAR_DATA() ? 0 : FAR);
	}
	}
}

/*
 * Huge memory model: pointer +/- offset must be normalised so the
 * resulting (seg, off) pair represents the same 20-bit linear address
 * after any carry into the segment.  The flat 32-bit `add ax, lo; adc
 * dx, hi` we use in compact/large model does NOT compute a normalised
 * pointer (it carries between bit 15 and bit 16, but a real-mode
 * segment carry happens at bit 4) and therefore mis-addresses any
 * array > 64K under huge.  Route the arithmetic through the libstub
 * helpers _qbe_huge_add / _qbe_huge_sub instead.  See
 * [[huge-mode-plan]] / [[huge-phase-a]].
 *
 * Returns 1 if a call was emitted (caller skips its usual add/sub
 * format-string emission), 0 otherwise.  Float operands, non-pointer
 * results, and non-FAR pointer types fall through to the regular path.
 */
static int
huge_ptr_binop(int op, Symb dst, Symb lhs, Symb rhs)
{
	Symb sptr, soff;

	if (memmodel != MHuge) return 0;
	if (op != '+' && op != '-') return 0;
	if (KIND(dst.ctyp) != PTR) return 0;
	/* Function pointers live in CS, not DS — their arithmetic is not
	 * subject to segment normalisation.  Exclude direct fn-ptr type. */
	if (KIND(DREF(dst.ctyp)) == FUN) return 0;
	if (ISFLOAT(lhs.ctyp) || ISFLOAT(rhs.ctyp)) return 0;
	/* Stack-pointer operands (Symb.t == Var) used to be gated out here
	 * because the i8086 backend's Ostorel handler wrote value→[bp+slot]
	 * directly, regardless of whether the slot held an alloca-slot
	 * destination or a spilled pointer VALUE.  Phase B' (i8086/emit.c
	 * Ostorel/Oload Kl via fn->arg_slot_top) closed that gap: spilled
	 * Kl-ptr slots now deref through ES:BX, so a normalised stack
	 * pointer returned by _qbe_huge_add behaves the same as a
	 * normalised global pointer.  See [[phase-bprime]] /
	 * [[huge-phase-b-storel-gap]]. */
	/* Under MHuge, default data pointers are 32-bit (l) regardless of
	 * the FAR flag's presence — prefix ++/-- strips FAR before calling
	 * prom() ([[minic.y:2353]]), so we can't rely on ISFAR here. */

	/* The pointer side is whichever operand has PTR kind; the other is
	 * the (already-scaled) byte offset.  For '+' either order is legal;
	 * for '-' the LHS must be the pointer (N - ptr is invalid C). */
	if (KIND(lhs.ctyp) == PTR) {
		sptr = lhs;
		soff = rhs;
	} else {
		sptr = rhs;
		soff = lhs;
	}

	/* Helper signature: unsigned long _qbe_huge_add(unsigned long ptr,
	 * long offset).  The offset arg must be Kl, so widen narrower tmps
	 * with the right signedness.  Constants take their class from the
	 * call signature (we emit `l <N>`), so they need no extension. */
	if (soff.t == Tmp && irtyp(soff.ctyp) != 'l') {
		const char *ext = ISUNSIGNED(soff.ctyp) ? "extuw" : "extsw";
		fprintf(of, "\t%%t%d =l %s ", tmp, ext);
		psymb(soff);
		fprintf(of, "\n");
		soff.t = Tmp;
		soff.ctyp = ISUNSIGNED(soff.ctyp) ? (LNG | UNSIGNED) : LNG;
		soff.u.n = tmp++;
	}

	fprintf(of, "\t");
	psymb(dst);
	fprintf(of, " =l call $%s(l ",
		op == '+' ? "qbe_huge_add" : "qbe_huge_sub");
	psymb(sptr);
	fprintf(of, ", l ");
	psymb(soff);
	fprintf(of, ")\n");
	return 1;
}

/*
 * Far-pointer index arithmetic for compact/large (and explicit __far in any
 * non-huge model): emit the dedicated `addfo`/`subfo` ops instead of a flat
 * `=l add`/`=l sub`.
 *
 * A far pointer's segment is fixed per object (objects <= 64 KB) and the
 * 16-bit offset wraps within the segment, so `far_ptr ± idx` must add/sub the
 * index to the OFFSET word only, preserving the segment.  A flat 32-bit add of
 * a SIGN-extended index (what the Scale path produces) goes wrong when the
 * in-segment byte offset is >= 0x8000: extsw makes it negative, so the sum
 * lands BELOW the object (e.g. MicroPython gc_alloc's pool_start + start_block*16
 * on a >32 KB heap).  A zero-extended index instead breaks a 16-bit-wrapped
 * "negative" size_t delta (off + 0xFFFF should give off-1).  Only offset-only
 * modular arithmetic on the 16-bit offset is correct for BOTH — that's what
 * the addfo/subfo backend ops do (they read just arg1's low 16 bits, so the
 * Scale path's `=l mul`/sext is left unchanged: its low word already equals
 * (idx*sz) mod 0x10000).  See NEXT_SESSION §4i / [[project-far-ptr-unsigned-index-bug]].
 *
 * MHuge is excluded: there an object can exceed 64 KB, so a genuine segment
 * carry is required — handled by huge_ptr_binop (_qbe_huge_add/sub), which the
 * callers run first.  Function pointers (pointee FUN, living in CS) and near
 * pointers (16-bit, irtyp 'w') keep the regular add/sub.
 *
 * Returns 1 if it emitted addfo/subfo (caller skips its add/sub), else 0.
 */
static int
far_ptr_offset_binop(int op, Symb dst, Symb lhs, Symb rhs)
{
	Symb sptr, soff;

	if (op != '+' && op != '-') return 0;
	if (memmodel == MHuge) return 0;
	if (KIND(dst.ctyp) != PTR) return 0;
	if (KIND(DREF(dst.ctyp)) == FUN) return 0;   /* fn ptr lives in CS */
	if (irtyp_ret(dst.ctyp) != 'l') return 0;     /* near (16-bit) ptr */
	if (ISFLOAT(lhs.ctyp) || ISFLOAT(rhs.ctyp)) return 0;

	/* The pointer side is the PTR operand; the other is the (already
	 * element-size-scaled) byte offset.  '+' is commutative; for '-' the
	 * pointer must be the LHS (prom() has already swapped a `idx + ptr`
	 * so the pointer is `lhs`). */
	if (KIND(lhs.ctyp) == PTR) {
		sptr = lhs;
		soff = rhs;
	} else {
		sptr = rhs;
		soff = lhs;
	}

	/* VARIABLE index only.  A CONSTANT offset keeps the flat `=l add`: the
	 * bug needs a runtime index whose 16-bit value is >= 0x8000 (gc_alloc's
	 * start_block*16) or a runtime-wrapped negative delta — a compile-time
	 * constant scaled offset is folded by QBE into a single relocated CAddr
	 * (`$sym+off`, no runtime arith), and routing it through the opaque
	 * addfo/subfo would defeat that fold and bloat every `arr[const]` /
	 * `&arr[const]` site (measured +2304 B across MicroPython, near the load
	 * ceiling).  A constant index >= 0x8000 could in theory carry into the
	 * segment too, but it is rare and the linker resolves the CAddr addend;
	 * handle it only if a real case appears (NEXT_SESSION §4h scope note). */
	if (soff.t == Con)
		return 0;

	fprintf(of, "\t");
	psymb(dst);
	fprintf(of, " =l %s ", op == '+' ? "addfo" : "subfo");
	psymb(sptr);
	fprintf(of, ", ");
	psymb(soff);
	fprintf(of, "\n");
	return 1;
}

/* Fill a struct/union aggregate's members from an initlist into the storage
 * whose address is the SSA operand `dst` (e.g. "%_clit3" for a compound
 * literal, or "%foo"/"%t9" for a named local / deref target — see the
 * local-aggregate-init rules which fill the destination directly, skipping
 * the compound-literal temp AND the per-init struct copy), at byte offset
 * `base_off`.  Sequential and `.field=` designated items are both handled; a
 * nested-brace item (`{ … }`, op '{') recurses into a sub-struct/union
 * member, so `(T){{a}, b, c}` (e.g. py/objtype.c's mp_obj_super_t) works.
 * The caller has already zero-initialised the storage.
 *
 * Far-correct: under a far-data model the destination address is a Kl
 * (seg:offset) far pointer, so offset arithmetic is `=l add` and member
 * stores are `storef%c` — a near `=w add` + `store%c` would TRUNCATE the
 * segment and scatter members into the wrong segment (the latent member-init
 * truncation the old %_clit-only near path carried). */
static void
emit_clit_aggr(const char *dst, int base_off, int sidx, Node *init)
{
	int far = !NEAR_DATA();
	char klass = far ? 'l' : 'w';
	int i = 0;

	while (init) {
		Node *item = init->l;
		int midx, off;
		struct Member *m;
		Symb val;
		char tc;
		const char *spfx;

		if (item->op == 'D') {
			midx = structfindmember(sidx, item->r->u.v);
			if (midx < 0)
				die("unknown member in designated initializer");
			item = item->l;  /* the value (expr or nested brace) */
		} else {
			if (i >= structh[sidx].nmembers)
				die("too many initializers for struct");
			midx = i;
		}
		m = &structh[sidx].members[midx];
		off = base_off + m->offset;

		if (item->op == '{') {
			/* Nested aggregate initializer fills a sub-struct/union. */
			if (KIND(m->ctyp) != STRUCT_T && KIND(m->ctyp) != UNION_T)
				die("braced initializer for non-aggregate member");
			emit_clit_aggr(dst, off, DREF(m->ctyp), item->l);
		} else {
			val = expr(item);
			tc = irtyp(m->ctyp);
			/* storef has no float/double form; far float members
			 * (no in-tree consumer) keep the plain store, as before. */
			spfx = (far && tc != 's' && tc != 'd') ? "storef" : "store";
			if (off > 0) {
				fprintf(of, "\t%%t%d =%c add %s, %d\n", tmp, klass, dst, off);
				fprintf(of, "\t%s%c ", spfx, tc);
				psymb(val);
				fprintf(of, ", %%t%d\n", tmp);
				tmp++;
			} else {
				fprintf(of, "\t%s%c ", spfx, tc);
				psymb(val);
				fprintf(of, ", %s\n", dst);
			}
		}

		i = midx + 1;
		init = init->r;
	}
}

Symb
expr(Node *n)
{
	static char *otoa[] = {
		['+'] = "add",
		['-'] = "sub",
		['*'] = "mul",
		['/'] = "div",
		['%'] = "rem",
		['&'] = "and",
		['|'] = "or",
		['^'] = "xor",
		['L'] = "shl",
		['R'] = "shr",
		['<'] = "cslt",  /* signed default; ptr/unsigned rewritten to cult/cule at the emit site */
		['l'] = "csle",
		['e'] = "ceq",
		['n'] = "cne",
	};
	Symb sr, s0, s1, sl;
	int o, l;
	int s1_far_storage = 0;
	char ty[2];

	sr.t = Tmp;
	sr.u.n = tmp++;

	switch (n->op) {

	case 0:
		abort();

	case ',':
		/* Comma operator: evaluate left, discard, return right */
		expr(n->l);  /* Evaluate but don't use result */
		sr = expr(n->r);  /* Evaluate and return */
		break;

	case '?':
		/* Ternary operator: cond ? true_expr : false_expr.
		 *
		 * Each branch may itself emit basic blocks (e.g. nested ternaries
		 * or short-circuit && / ||).  The phi at the merge therefore can
		 * NOT use the original branch-entry labels as predecessors — by
		 * the time control reaches the merge it's via whatever block the
		 * sub-expression last fell through to.  We emit a dedicated
		 * "trailer" label after each branch so the predecessor seen by
		 * the phi is unambiguous: @ltrue → @merge and @lfalse → @merge.
		 */
		l = lbl;
		lbl += 5;  /* l = entry-true, l+1 = entry-false, l+2 = trailer-true,
		            * l+3 = trailer-false, l+4 = merge */
		s0 = expr(n->l);
		/* QBE's jnz is typed `w` — truncate Kl conditions to a Kw
		 * boolean first.  Same shape as the fix in branch(). */
		if (irtyp(s0.ctyp) == 'l') {
			Symb cmp;
			cmp.t = Tmp;
			cmp.u.n = tmp++;
			cmp.ctyp = INT;
			fprintf(of, "\t");
			psymb(cmp);
			fprintf(of, " =w cnel ");
			psymb(s0);
			fprintf(of, ", 0\n");
			s0 = cmp;
		}
		fprintf(of, "\tjnz ");
		psymb(s0);
		fprintf(of, ", @l%d, @l%d\n", l, l+1);
		/* True branch */
		fprintf(of, "@l%d\n", l);
		s0 = expr(n->r->l);
		fprintf(of, "\tjmp @l%d\n", l+2);
		/* False branch */
		fprintf(of, "@l%d\n", l+1);
		s1 = expr(n->r->r);
		fprintf(of, "\tjmp @l%d\n", l+3);
		/* Usual arithmetic conversions of the two arms.  The phi (and
		 * therefore BOTH arms) must have the IL width of the WIDER arm,
		 * else the wider value is truncated through the phi.  The old
		 * code only recognised the exact signed INT/LNG pair, so
		 * `cond ? 0 : (U32)x` (an `unsigned long` = LNG|UNSIGNED arm,
		 * which is not == LNG) defaulted to the narrow INT arm and emitted
		 * a `=w` phi that dropped the high word of the 32-bit arm.  Now
		 * any 4-byte ('l') arm forces an 'l' phi and the 2-byte ('w') arm
		 * is sign/zero-extended (per its own signedness) in its trailer
		 * block, where referencing the arm value is still SSA-dominated. */
		{
			char w0 = irtyp_ret(s0.ctyp);
			char w1 = irtyp_ret(s1.ctyp);
			int widen0 = 0, widen1 = 0;
			if (s0.ctyp == s1.ctyp)
				sr.ctyp = s0.ctyp;
			else if (w0 == 'l' && w1 == 'w') {
				sr.ctyp = s0.ctyp; widen1 = 1;
			} else if (w1 == 'l' && w0 == 'w') {
				sr.ctyp = s1.ctyp; widen0 = 1;
			} else if (s0.ctyp == LNG && s1.ctyp == INT)
				sr.ctyp = LNG;
			else if (s0.ctyp == INT && s1.ctyp == LNG)
				sr.ctyp = LNG;
			else
				sr.ctyp = s0.ctyp;
			/* Trailer-true: predecessor of the merge for the true arm. */
			fprintf(of, "@l%d\n", l+2);
			if (widen0) {
				Symb e;
				e.t = Tmp; e.u.n = tmp++; e.ctyp = sr.ctyp;
				fprintf(of, "\t");
				psymb(e);
				fprintf(of, " =l %s ", ISUNSIGNED(s0.ctyp) ? "extuw" : "extsw");
				psymb(s0);
				fprintf(of, "\n");
				s0 = e;
			}
			fprintf(of, "\tjmp @l%d\n", l+4);
			/* Trailer-false. */
			fprintf(of, "@l%d\n", l+3);
			if (widen1) {
				Symb e;
				e.t = Tmp; e.u.n = tmp++; e.ctyp = sr.ctyp;
				fprintf(of, "\t");
				psymb(e);
				fprintf(of, " =l %s ", ISUNSIGNED(s1.ctyp) ? "extuw" : "extsw");
				psymb(s1);
				fprintf(of, "\n");
				s1 = e;
			}
			fprintf(of, "\tjmp @l%d\n", l+4);
		}
		/* Merge */
		fprintf(of, "@l%d\n", l+4);
		fprintf(of, "\t");
		psymb(sr);
		fprintf(of, " =%c phi @l%d ", irtyp_ret(sr.ctyp), l+2);
		psymb(s0);
		fprintf(of, ", @l%d ", l+3);
		psymb(s1);
		fprintf(of, "\n");
		break;

	case 'o':
	case 'a':
		l = lbl;
		lbl += 3;
		branch(n, l, l+1);
		fprintf(of, "@l%d\n", l);
		fprintf(of, "\tjmp @l%d\n", l+2);
		fprintf(of, "@l%d\n", l+1);
		fprintf(of, "\tjmp @l%d\n", l+2);
		fprintf(of, "@l%d\n", l+2);
		fprintf(of, "\t");
		sr.ctyp = INT;
		psymb(sr);
		fprintf(of, " =w phi @l%d 1, @l%d 0\n", l, l+1);
		break;

	case 'V':
		s0 = lval(n);
		sr.ctyp = s0.ctyp;
		if (s0.t == Con) {
			/* Enum constant or other constant - use directly */
			sr = s0;
		} else if (KIND(s0.ctyp) == FUN) {
			/* Function name - return its address as function pointer */
			sr.t = Tmp;
			sr.u.n = tmp++;
			sr.ctyp = IDIR(s0.ctyp);  /* Pointer to function */
			/* Copy function address to temporary */
			fprintf(of, "\t");
			psymb(sr);
			fprintf(of, " =%c copy $%s\n", CODEPTR_T(), n->u.v);
		} else if (var_isarray(n->u.v)) {
			/* Arrays - don't load, the lvalue IS the pointer */
			sr = s0;
		} else {
			/* Regular variables and pointer variables - load value */
			sr.t = Tmp;
			sr.u.n = tmp++;
			load(sr, s0);
			/* Bytes and shorts are extended to words during load */
			if (KIND(sr.ctyp) == CHR || ((sr.ctyp & SHORT) && KIND(sr.ctyp) == INT)) {
				if (ISUNSIGNED(sr.ctyp))
					sr.ctyp = INT | UNSIGNED;
				else
					sr.ctyp = INT;
			}
		}
		break;

	case 'N':
		sr.t = Con;
		sr.u.n = n->u.n;
		sr.ctyp = n->nlong ? LNG : INT;
		break;

	case 'F':
		/* Floating-point literal.  On this FPU-less i8086 target `double`
		 * is single-precision (see TDOUBLE below): there is no soft-double
		 * and no 64-bit int to build one, so EVERY float literal — suffixed
		 * or not — types as single (Ks) and lowers through the _sf_* helpers.
		 * QBE truncates the `s_` constant to binary32. */
		sr.t = Tmp;
		sr.u.n = tmp++;
		sr.ctyp = INT | FLOAT;  /* float (single); double aliases to this */
		fprintf(of, "\t");
		psymb(sr);
		fprintf(of, " =s copy s_%s\n", n->u.v);
		break;

	case 'S':
		sr.t = Glo;
		sr.u.n = n->u.n;
		sr.ctyp = IDIR(INT);
		break;

	case 'L':
		/* Op 'L' is overloaded: compound literal AND left shift (SHL).
		 * Compound literal nodes have r == 0 and u.n holding the type.
		 * Shift nodes have non-NULL r; route them through the default
		 * binary-op handler. */
		if (n->r != 0) {
			s0 = expr(n->l);
			s1 = expr(n->r);
			o = n->op;
			goto Binop;
		}
		/* Compound literal: (type){ initializer }
		 * Allocate temporary storage, initialize it, return value
		 */
		{
			unsigned ctyp = (unsigned)n->u.n;
			int s = SIZE(ctyp);
			int clitnum = clit++;
			Node *init;
			int i;

			/* Allocate temporary storage */
			fprintf(of, "\t%%_clit%d =%c alloc%d %d\n", clitnum, ALLOC_T(), iralign(ctyp), s);

			if (KIND(ctyp) == STRUCT_T || KIND(ctyp) == UNION_T) {
				/* Struct/union initialization */
				int sidx = DREF(ctyp);
				init = n->l;
				i = 0;

				/* Zero-initialize first (C11 6.7.9p21: members with no
				 * explicit initializer are zeroed). */
				{
					char caddr[24];
					snprintf(caddr, sizeof caddr, "%%_clit%d", clitnum);
					emit_zero_aggr(caddr, s);

					/* Initialize members from initlist with designator
					 * and nested-brace support (see emit_clit_aggr). */
					(void)i;
					emit_clit_aggr(caddr, 0, sidx, init);
				}

				/* For structs, load the struct value (like struct variables) */
				sr.t = Tmp;
				sr.u.n = tmp++;
				sr.ctyp = ctyp;
				fprintf(of, "\t");
				psymb(sr);
				fprintf(of, " =%c load%c %%_clit%d\n", irtyp(ctyp), irtyp(ctyp), clitnum);
			} else if (KIND(ctyp) == PTR) {
				/* Array compound literal - initialize elements */
				unsigned elemtyp = DREF(ctyp);
				int elems = SIZE(elemtyp);
				init = n->l;
				i = 0;

				while (init) {
					Symb val = expr(init->l);
					if (i == 0) {
						fprintf(of, "\tstore%c ", irtyp(elemtyp));
						psymb(val);
						fprintf(of, ", %%_clit%d\n", clitnum);
					} else {
						fprintf(of, "\t%%t%d =w add %%_clit%d, %d\n", tmp, clitnum, i * elems);
						fprintf(of, "\tstore%c ", irtyp(elemtyp));
						psymb(val);
						fprintf(of, ", %%t%d\n", tmp);
						tmp++;
					}
					init = init->r;
					i++;
				}

				/* For arrays, return the address as a pointer */
				sr.t = Tmp;
				sr.u.n = tmp++;
				sr.ctyp = ctyp;
				fprintf(of, "\t");
				psymb(sr);
				fprintf(of, " =w copy %%_clit%d\n", clitnum);
			} else {
				/* Scalar compound literal - store and load value */
				char t = irtyp(ctyp);
				init = n->l;
				if (init) {
					Symb val = expr(init->l);
					fprintf(of, "\tstore%c ", t);
					psymb(val);
					fprintf(of, ", %%_clit%d\n", clitnum);
				}

				/* Load the scalar value (QBE doesn't support byte/halfword temps) */
				sr.t = Tmp;
				sr.u.n = tmp++;
				sr.ctyp = ctyp;
				fprintf(of, "\t");
				psymb(sr);
				if (t == 'b' || t == 'h') {
					/* Byte/halfword loads go into words */
					if (ISUNSIGNED(ctyp))
						fprintf(of, " =w loadu%c %%_clit%d\n", t, clitnum);
					else
						fprintf(of, " =w loads%c %%_clit%d\n", t, clitnum);
				} else {
					fprintf(of, " =%c load%c %%_clit%d\n", t, t, clitnum);
				}
			}
		}
		break;

	case 'C':
		call(n, &sr);
		break;

	case 'I':
		/* Indirect function call: (*fptr)(args) */
		{
			Node *a;
			Symb fptr;
			unsigned fptr_type;
			int sret;
			unsigned aggr;
			int sret_slot = 0;
			int fpid;

			/* Evaluate function pointer expression.  Reset the member
			 * fn-ptr stash first: expr() sets g_callee_fpid when n->l is
			 * a member access `obj->fn' (case '.'); leave it -1 otherwise
			 * so a stale id from an earlier statement can't leak (§2q). */
			g_callee_fpid = -1;
			fptr = expr(n->l);
			fpid = g_callee_fpid;
			/* `(*fp)(...)' / `fp(...)' through a plain fn-ptr variable:
			 * recover the prototype from the variable's recorded fpid. */
			if (fpid < 0) {
				if (n->l->op == '@' && n->l->l->op == 'V')
					fpid = varfpid(n->l->l->u.v);
				else if (n->l->op == 'V')
					fpid = varfpid(n->l->u.v);
			}

			/* Check it's a function pointer */
			if (KIND(fptr.ctyp) != PTR || KIND(DREF(fptr.ctyp)) != FUN)
				die("invalid indirect call - not a function pointer");

			/* Get return type */
			fptr_type = DREF(fptr.ctyp);  /* FUN(return_type) */
			sr.ctyp = DREF(fptr_type);     /* return_type */
			/* A float return does not survive the double-DREF decode
			 * (FLOAT flag shifted onto FAR, which DREF strips) —
			 * recover it from the recorded prototype (§5b). */
			if (fpid >= 0)
				sr.ctyp = fpproto[fpid].rett;
			sret = (KIND(sr.ctyp) == STRUCT_T || KIND(sr.ctyp) == UNION_T);
			aggr = sr.ctyp;

			/* Struct/union return-by-value: alloc result storage and
			 * pass its address as the hidden first argument. */
			if (sret)
				sret_slot = alloc_sret_slot(aggr);

			/* Evaluate all arguments, coercing each to the fn-ptr's
			 * declared parameter type (§2q): a width mismatch shifts the
			 * stack-arg layout exactly as on a direct call. */
			{
				int argi = 0;
				for (a=n->r; a; a=a->r, argi++) {
					a->u.s = eval_arg(a);
					if (fpid >= 0 && argi < fpproto[fpid].nparam)
						a->u.s = coerce_arg(a->u.s,
						    fpproto[fpid].ptyp[argi]);
				}
			}

			/* Generate indirect call */
			if (sret) {
				fprintf(of, "\tcall ");
				psymb(fptr);
				fprintf(of, "(%c %%t%d, ", DATAPTR_T(), sret_slot);
			} else if (sr.ctyp == NIL) {
				/* Void-returning function pointer - no result. */
				fprintf(of, "\tcall ");
				psymb(fptr);
				fprintf(of, "(");
			} else {
				fprintf(of, "\t");
				psymb(sr);
				fprintf(of, " =%c call ", irtyp_ret(sr.ctyp));
				psymb(fptr);
				fprintf(of, "(");
			}
			for (a=n->r; a; a=a->r)
				emit_arg(a->u.s);
			fprintf(of, "...)\n");
			if (sret) {
				sr.t = Tmp;
				sr.u.n = sret_slot;
				sr.ctyp = aggr | (NEAR_DATA() ? 0 : FAR);
			}
		}
		break;

	case 'G':
		/* _Generic: compile-time type selection
		 * The controlling expression's type is determined without
		 * integer promotion, so we need to check the underlying type.
		 */
		{
			Node *assoc;
			Node *default_assoc = 0;
			Node *matched = 0;
			unsigned ctrl_type;

			/* Get the original type of the controlling expression
			 * For variables, use the declared type (not promoted type)
			 */
			if (n->l->op == 'V' || (n->l->op == 0 && n->l->l && n->l->l->u.v[0])) {
				/* Variable reference - get declared type */
				Symb *sv = varget(n->l->u.v);
				if (sv) {
					ctrl_type = sv->ctyp;
				} else {
					/* Unknown variable - evaluate to get type */
					Symb ctrl = expr(n->l);
					ctrl_type = ctrl.ctyp;
				}
			} else {
				/* Expression - evaluate to get type */
				Symb ctrl = expr(n->l);
				ctrl_type = ctrl.ctyp;
			}

			/* Search through associations for matching type */
			for (assoc = n->r; assoc; assoc = assoc->r) {
				int assoc_type = assoc->u.n;
				if (assoc_type == -1) {
					/* Default association */
					default_assoc = assoc;
				} else if ((unsigned)assoc_type == ctrl_type) {
					/* Exact type match */
					matched = assoc;
					break;
				}
			}

			/* Use matched type, or default, or error */
			if (!matched) {
				if (default_assoc)
					matched = default_assoc;
				else
					die("_Generic: no matching type and no default");
			}

			/* Evaluate the selected expression */
			sr = expr(matched->l);
		}
		break;

	case '@':
		s0 = expr(n->l);
		if (KIND(s0.ctyp) != PTR)
			die("dereference of a non-pointer");
		sr.ctyp = DREF(s0.ctyp);
		/* `*fp` on a function pointer is a function designator, not a
		 * memory load — the value of fp IS the call target. */
		if (KIND(sr.ctyp) == FUN) {
			sr = s0;
			break;
		}
		/* Check if dereferencing a far pointer.  sr.ctyp carries the
		 * pointee's QVOLATILE qualifier (if any) so load()/loadfar() emit
		 * the `volatile` keyword for this access. */
		if (ISFAR(s0.ctyp)) {
			loadfar(sr, s0);
		} else {
			load(sr, s0);
		}
		/* The RESULT of a volatile lvalue read is an ordinary unqualified
		 * rvalue (C11 6.3.2.1): strip QVOLATILE so it never propagates into
		 * the expression/type-comparison machinery (raw `ctyp == LNG/NIL`
		 * sites that mask only FAR). */
		sr.ctyp &= ~QVOLATILE;
		break;

	case 'A':
		sr = lval(n->l);
		/* &volatile_struct: the address-of result is a plain (non-volatile-
		 * pointee) pointer — strip the QVOLATILE the lval may now carry on a
		 * direct volatile aggregate before IDIR, so pointer types stay
		 * byte-identical and no raw pointer `ctyp ==` site sees the bit.
		 * Code needing volatile through the pointer declares it
		 * `volatile T *p` (whose pointee bit is set independently by the
		 * VOLATILE type rules).  Mirrors §3l's `&named_volatile` behavior. */
		sr.ctyp = IDIR(sr.ctyp & ~QVOLATILE);
		break;

	case '.':
		/* Member access: struct.member */
		s0 = lval(n->l);  /* Get struct lvalue */
		if (KIND(s0.ctyp) != STRUCT_T && KIND(s0.ctyp) != UNION_T)
			die("member access on non-struct/union");
		{
			int sidx = DREF(s0.ctyp);
			char *mname = n->r->u.v;
			int i, found = 0;
			struct Member *m;
			Symb addr;

			/* Find member */
			for (i = 0; i < structh[sidx].nmembers; i++) {
				if (strcmp(structh[sidx].members[i].name, mname) == 0) {
					found = 1;
					m = &structh[sidx].members[i];
					break;
				}
			}
			if (!found)
				die("struct member not found");

			/* Stash this member's fn-ptr prototype id (or -1) for an
			 * immediately-following indirect call `obj->fn(...)' (§2q). */
			g_callee_fpid = m->fpid;

			/* Compute member address: struct_addr + offset.  Under far-
			 * data models (compact/large/huge), when s0 came through a
			 * far-pointer deref it carries the FAR bit and the address
			 * is Kl (4-byte seg:off).  The add must then be `=l add` so
			 * we don't truncate to 16 bits.  Mirror s0's FAR-ness onto
			 * addr so downstream load/store picks the right width.
			 * FARSTORAGE: a global struct accessed directly (`g.m`) lives
			 * in a far segment too, so its member address is also Kl.
			 * !NEAR_DATA(): a LOCAL aggregate's address is likewise a
			 * far (Kl) pointer under compact/large/huge (ALLOC_T() emits
			 * the slot temp as `=l`), so `&local.m` must add as `=l` too
			 * — otherwise a `=w add %localKl, off` truncates the Kl base
			 * and feeds an invalid narrow temp into the far loadfX (and
			 * trips gvn's assoccon KWIDE assert when const-folded). */
			{
				int base_far = ISFAR(s0.ctyp) || FARSTORAGE(s0) || !NEAR_DATA();
				char klass = base_far ? 'l' : 'w';
				unsigned ptyp = base_far ? IDIR_FAR(m->ctyp)
				                              : (IDIR(m->ctyp) & ~FAR);
				if (m->offset > 0) {
					addr.t = Tmp;
					addr.u.n = tmp++;
					addr.ctyp = ptyp;
					fprintf(of, "\t");
					psymb(addr);
					fprintf(of, " =%c add ", klass);
					psymb(s0);
					fprintf(of, ", %d\n", m->offset);
				} else {
					/* Offset 0, just use struct address */
					addr = s0;
					addr.ctyp = ptyp;
				}
			}

			/* Array members decay to a pointer to their first element —
			 * don't load through the address, return it as the value.
			 * Flexible array members (`T x[];`, count 0) decay too. */
			if (m->count > 0 || m->isflex) {
				sr = addr;
				break;
			}

			/* Load value from member address.  The access is volatile
			 * if EITHER the member is itself volatile-qualified (QVOLATILE
			 * already in m->ctyp) OR the containing aggregate is volatile
			 * (`volatile struct S *p` — QVOLATILE on s0.ctyp, recovered
			 * through the deref); OR them onto the value type so
			 * load()/loadfar() emit the QBE `volatile` keyword. */
			sr.t = Tmp;
			sr.u.n = tmp++;
			sr.ctyp = m->ctyp | ISVOLATILE(s0.ctyp);
			if (ISFAR(addr.ctyp))
				loadfar(sr, addr);
			else
				load(sr, addr);

			/* Handle bitfield extraction */
			if (m->bitwidth > 0) {
				Symb shifted, masked;
				unsigned long bitmask;

				/* Shift right to bring bits to position 0 */
				if (m->bitoffset > 0) {
					shifted.t = Tmp;
					shifted.u.n = tmp++;
					shifted.ctyp = m->ctyp;
					fprintf(of, "\t");
					psymb(shifted);
					fprintf(of, " =%c shr ", irtyp(m->ctyp));
					psymb(sr);
					fprintf(of, ", %d\n", m->bitoffset);
					sr = shifted;
				}

				/* Mask to extract only the bitfield bits */
				bitmask = (1UL << m->bitwidth) - 1;
				masked.t = Tmp;
				masked.u.n = tmp++;
				masked.ctyp = m->ctyp;
				fprintf(of, "\t");
				psymb(masked);
				fprintf(of, " =%c and ", irtyp(m->ctyp));
				psymb(sr);
				fprintf(of, ", %lu\n", bitmask);
				sr = masked;
			}
			/* The result of reading a volatile lvalue is an ordinary
			 * unqualified rvalue (C11 6.3.2.1): strip QVOLATILE (from a
			 * volatile member m->ctyp or a volatile aggregate s0) so it
			 * never propagates into downstream raw ctyp comparisons. */
			sr.ctyp &= ~QVOLATILE;
		}
		break;

	case '~':
		s0 = expr(n->l);
		if (ISFLOAT(s0.ctyp))
			die("bitwise NOT not supported on floating-point types");
		sr.ctyp = s0.ctyp;
		fprintf(of, "\t");
		psymb(sr);
		fprintf(of, " =%c xor ", irtyp(sr.ctyp));
		psymb(s0);
		fprintf(of, ", -1\n");
		break;

	case '!':
		s0 = expr(n->l);
		sr.ctyp = INT;
		fprintf(of, "\t");
		psymb(sr);
		{
			/* QBE compare instructions only exist in w/l/s/d
			 * forms — widen byte/halfword to word. */
			char ct = irtyp(s0.ctyp);
			if (ct == 'b' || ct == 'h') ct = 'w';
			fprintf(of, " =w ceq%c ", ct);
		}
		psymb(s0);
		fprintf(of, ", 0\n");
		break;

	case 'K':
		/* Cast expression: n->u.n is target type, n->l is expression */
		s0 = expr(n->l);
		sr.ctyp = n->u.n;
		/* (void) cast: evaluate for side effects, discard value */
		if (sr.ctyp == NIL) {
			break;
		}
		/* For most casts, just copy the value with new type */
		if (ISFLOAT(s0.ctyp) && !ISFLOAT(sr.ctyp)) {
			/* Float to int conversion */
			fprintf(of, "\t");
			psymb(sr);
			fprintf(of, " =%c %s ", irtyp_ret(sr.ctyp),
				KIND(s0.ctyp) == LNG ? "dtosi" : "stosi");
			psymb(s0);
			fprintf(of, "\n");
		} else if (!ISFLOAT(s0.ctyp) && ISFLOAT(sr.ctyp)) {
			/* Int to float conversion */
			fprintf(of, "\t");
			psymb(sr);
			fprintf(of, " =%c %s ", irtyp(sr.ctyp),
				KIND(sr.ctyp) == LNG ? "sltof" : "swtof");
			psymb(s0);
			fprintf(of, "\n");
		} else {
			/* Integer/pointer casts.  QBE requires source width to match
			 * destination class.  When narrowing long -> int the value is
			 * truncated by `=w copy`; when widening int -> long the C
			 * integer-conversion rule preserves the SOURCE value according to
			 * the SOURCE signedness: an unsigned source zero-extends (`extuw`),
			 * a signed source sign-extends (`extsw`).  Using extsw for an
			 * unsigned source (e.g. `(uint32_t)(size_t)`) corrupts the high
			 * word — bug-loud in MP_OBJ_FUN_MAKE_SIG's `(uint32_t)max << 17`
			 * packing, which made mp_arg_check_num spuriously fail. */
			char dst = irtyp_ret(sr.ctyp);
			char src = irtyp_ret(s0.ctyp);
			fprintf(of, "\t");
			psymb(sr);
			if (dst == 'l' && src == 'w') {
				fprintf(of, " =l %s ",
					ISUNSIGNED(s0.ctyp) ? "extuw" : "extsw");
			} else {
				fprintf(of, " =%c copy ", dst);
			}
			psymb(s0);
			fprintf(of, "\n");
		}
		break;

	case '=':
		/* Direct compound-literal initialization of an aggregate lvalue:
		 * `dst = (T){...};` (the desugaring the local-aggregate-init rules
		 * emit, and any user-written struct-literal assignment).  Fill the
		 * destination IN PLACE — zero it, then place the members — skipping
		 * the compound-literal temp slot AND the whole-struct copy.  A
		 * code-size win (the dominant lever for fitting MicroPython under
		 * the Victor ceiling) and a far-data correctness win (emit_clit_aggr
		 * is far-address-correct; the old temp+copy path's near member
		 * stores truncated the segment under far-data).  Op 'L' with r==0 is
		 * a compound literal (r!=0 is a left-shift). */
		if (n->r->op == 'L' && n->r->r == 0
		    && (KIND((unsigned)n->r->u.n) == STRUCT_T
			|| KIND((unsigned)n->r->u.n) == UNION_T)) {
			unsigned ltyp = (unsigned)n->r->u.n;
			char daddr[NString];

			s1 = lval(n->l);
			symb_operand(s1, daddr, sizeof daddr);
			emit_zero_aggr(daddr, SIZE(ltyp));
			emit_clit_aggr(daddr, 0, DREF(ltyp), n->r->l);
			sr = s1;
			break;
		}

		/* Check for bitfield assignment */
		if (n->l->op == '.') {
			/* Get the struct and member info */
			Symb s_struct = lval(n->l->l);
			if (KIND(s_struct.ctyp) == STRUCT_T || KIND(s_struct.ctyp) == UNION_T) {
				int sidx = DREF(s_struct.ctyp);
				char *mname = n->l->r->u.v;
				int i;
				struct Member *m = NULL;

				/* Find member */
				for (i = 0; i < structh[sidx].nmembers; i++) {
					if (strcmp(structh[sidx].members[i].name, mname) == 0) {
						m = &structh[sidx].members[i];
						break;
					}
				}

				if (m && m->bitwidth > 0) {
					/* Bitfield assignment - read-modify-write */
					Symb addr, oldval, newval, clearmask, shifted, merged;
					unsigned long mask, invmask;
					/* !NEAR_DATA(): local aggregate addresses are far Kl too. */
					int base_far = ISFAR(s_struct.ctyp) || FARSTORAGE(s_struct) || !NEAR_DATA();
					char klass = base_far ? 'l' : 'w';
					unsigned ptyp = base_far
					    ? IDIR_FAR(m->ctyp)
					    : (IDIR(m->ctyp) & ~FAR);

					/* Get the storage unit address.  Mirror s_struct's
					 * FAR-ness onto addr so the add width matches and
					 * downstream load/store picks the right op. */
					if (m->offset > 0) {
						addr.t = Tmp;
						addr.u.n = tmp++;
						addr.ctyp = ptyp;
						fprintf(of, "\t");
						psymb(addr);
						fprintf(of, " =%c add ", klass);
						psymb(s_struct);
						fprintf(of, ", %d\n", m->offset);
					} else {
						addr = s_struct;
						addr.ctyp = ptyp;
					}

					/* Evaluate RHS */
					s0 = expr(n->r);

					/* Load current storage unit value.  addr is far
					 * (ptyp = IDIR_FAR) under any far-data model, so the
					 * RMW must go through loadfar/storefar — a near
					 * load/store would hit only the offset against DS and
					 * silently read/write the wrong segment.  Mirror the
					 * bitfield READ path above. */
					oldval.t = Tmp;
					oldval.u.n = tmp++;
					oldval.ctyp = m->ctyp;
					if (base_far)
						loadfar(oldval, addr);
					else
						load(oldval, addr);

					/* Create masks */
					mask = (1UL << m->bitwidth) - 1;
					invmask = ~(mask << m->bitoffset);
					/* Truncate invmask to type size */
					if (SIZE(m->ctyp) == 1)
						invmask &= 0xFF;
					else if (SIZE(m->ctyp) == 2)
						invmask &= 0xFFFF;
					else if (SIZE(m->ctyp) == 4)
						invmask &= 0xFFFFFFFF;

					/* Clear old bitfield bits: oldval & ~(mask << offset) */
					clearmask.t = Tmp;
					clearmask.u.n = tmp++;
					clearmask.ctyp = m->ctyp;
					fprintf(of, "\t");
					psymb(clearmask);
					fprintf(of, " =%c and ", irtyp(m->ctyp));
					psymb(oldval);
					fprintf(of, ", %lu\n", invmask);

					/* Mask the new value: newval & mask */
					newval.t = Tmp;
					newval.u.n = tmp++;
					newval.ctyp = m->ctyp;
					fprintf(of, "\t");
					psymb(newval);
					fprintf(of, " =%c and ", irtyp(m->ctyp));
					psymb(s0);
					fprintf(of, ", %lu\n", mask);

					/* Shift new value to position: newval << offset */
					if (m->bitoffset > 0) {
						shifted.t = Tmp;
						shifted.u.n = tmp++;
						shifted.ctyp = m->ctyp;
						fprintf(of, "\t");
						psymb(shifted);
						fprintf(of, " =%c shl ", irtyp(m->ctyp));
						psymb(newval);
						fprintf(of, ", %d\n", m->bitoffset);
					} else {
						shifted = newval;
					}

					/* Merge: cleared | shifted */
					merged.t = Tmp;
					merged.u.n = tmp++;
					merged.ctyp = m->ctyp;
					fprintf(of, "\t");
					psymb(merged);
					fprintf(of, " =%c or ", irtyp(m->ctyp));
					psymb(clearmask);
					fprintf(of, ", ");
					psymb(shifted);
					fprintf(of, "\n");

					/* Store back (far when the base address is far). */
					if (base_far) {
						storefar(merged, addr);
					} else {
						fprintf(of, "\tstore%c ", irtyp(m->ctyp));
						psymb(merged);
						fprintf(of, ", ");
						psymb(addr);
						fprintf(of, "\n");
					}

					sr = s0;  /* Assignment returns the assigned value */
					break;
				}
			}
		}

		s0 = expr(n->r);
		s1 = lval(n->l);
		s1_far_storage = lval_storage_far;  /* capture before any further expr/lval */
		sr = s0;

		/* Struct/union assignment: emit a multi-step copy.  The
		 * normal single-store path uses irtyp(STRUCT_T) which gives
		 * 'w' and only copies the first 16-bit word, leaving later
		 * fields uninitialised.  Stevie hits this constantly:
		 * `*Curschar = *Filemem;` and `save = memp = *Topchar;`
		 * leave the index field as malloc-garbage. */
		if (KIND(s1.ctyp) == STRUCT_T || KIND(s1.ctyp) == UNION_T) {
			Symb src_addr;

			/* Get RHS address.  For most cases the RHS is `*ptr` or
			 * an identifier — lval is idempotent there.  For chained
			 * assignment `a = b = c`, the inner `b = c` already
			 * ran during expr(n->r); use b's address as source.  For
			 * a struct-returning call (`x = f();`), expr(n->r) above
			 * already allocated the result slot and left its address
			 * in s0 — re-running lval would emit the call a second
			 * time, so reuse s0 directly. */
			if (n->r->op == '=')
				src_addr = lval(n->r->l);
			else if (n->r->op == 'C' || n->r->op == 'I')
				src_addr = s0;
			else
				src_addr = lval(n->r);

			emit_struct_copy(s1, src_addr);
			sr = s1;
			break;
		}
		/* Type conversions for assignment */

		/* Float/int conversions */
		if (ISFLOAT(s1.ctyp) && !ISFLOAT(s0.ctyp)) {
			/* Convert int to float/double */
			fprintf(of, "\t%%t%d =%c ", tmp, irtyp(s1.ctyp));
			if (KIND(s0.ctyp) == LNG)
				fprintf(of, "sltof ");
			else
				fprintf(of, "swtof ");
			psymb(s0);
			fprintf(of, "\n");
			s0.t = Tmp;
			s0.ctyp = s1.ctyp;
			s0.u.n = tmp++;
		} else if (!ISFLOAT(s1.ctyp) && ISFLOAT(s0.ctyp)) {
			/* Convert float/double to int */
			fprintf(of, "\t%%t%d =%c ", tmp, irtyp(s1.ctyp));
			if (KIND(s1.ctyp) == LNG)
				fprintf(of, "dtosi ");
			else
				fprintf(of, "stosi ");
			psymb(s0);
			fprintf(of, "\n");
			s0.t = Tmp;
			s0.ctyp = s1.ctyp;
			s0.u.n = tmp++;
		} else if (ISFLOAT(s1.ctyp) && ISFLOAT(s0.ctyp)
		           && (KIND(s1.ctyp) == LNG) != (KIND(s0.ctyp) == LNG)) {
			/* Convert between float and double.  Compare only the
			 * floating PRECISION (double = KIND LNG, float = KIND INT),
			 * not the full ctyp — under a far-data model the RHS value
			 * carries an extra FAR bit, so `float = float` must NOT be
			 * mistaken for a precision change and emit a bogus truncd. */
			fprintf(of, "\t%%t%d =%c ", tmp, irtyp(s1.ctyp));
			if (KIND(s1.ctyp) == LNG)
				fprintf(of, "exts ");  /* float to double */
			else
				fprintf(of, "truncd ");  /* double to float */
			psymb(s0);
			fprintf(of, "\n");
			s0.t = Tmp;
			s0.ctyp = s1.ctyp;
			s0.u.n = tmp++;
		} else if (KIND(s1.ctyp) == LNG &&
		           (KIND(s0.ctyp) == INT || KIND(s0.ctyp) == CHR) &&
		           !ISFLOAT(s1.ctyp)) {
			/* Widen int/short/char RHS to long.  The narrow value
			 * sits in a `w` temp (loaded zero/sign-extended to 16
			 * bits); extsw widens it to the 32-bit long. */
			sext(&s0);
		} else if (KIND(s1.ctyp) != LNG && KIND(s0.ctyp) == LNG && !ISFLOAT(s0.ctyp)) {
			/* Implicit narrowing from long to int/short/char.
			 * QBE requires the value width to match the store, so emit a
			 * truncating copy.  Result type matches the LHS. */
			fprintf(of, "\t%%t%d =w copy ", tmp);
			psymb(s0);
			fprintf(of, "\n");
			s0.t = Tmp;
			s0.ctyp = (KIND(s1.ctyp) == CHR) ? CHR : INT;
			s0.u.n = tmp++;
		} else if (KIND(s1.ctyp) == CHR && KIND(s0.ctyp) == INT) {
			/* Truncate int to char - no explicit conversion needed */
			/* QBE will handle truncation in storeb */
		} else if (KIND(s1.ctyp) == INT && KIND(s0.ctyp) == CHR) {
			/* Extend char to int.  An UNSIGNED char (uint8_t) must
			 * zero-extend (extub) — the byte already sits zero-extended
			 * in a `w` temp (loadub/loadfb both clear the high byte), so
			 * sign-extending its low byte would corrupt any value with
			 * bit 7 set (e.g. a 0x8D table index → 0xFF8D).  Signed char
			 * keeps extsb. */
			fprintf(of, "\t%%t%d =w %s ", tmp, ISUNSIGNED(s0.ctyp) ? "extub" : "extsb");
			psymb(s0);
			fprintf(of, "\n");
			s0.t = Tmp;
			s0.ctyp = INT;
			s0.u.n = tmp++;
		}
		if (s0.ctyp != IDIR(NIL) || KIND(s1.ctyp) != PTR)
		if (s1.ctyp != IDIR(NIL) || KIND(s0.ctyp) != PTR)
		/* Null pointer constant: integer constant 0 may be assigned to any pointer
		 * (C standard §6.3.2.3). NULL typically expands to ((void*)0), but plain 0
		 * suffices. Detect: RHS is Con with value 0 and LHS is a pointer. */
		if (KIND(s1.ctyp) == PTR && s0.t == Con && s0.u.n == 0 && !ISFLOAT(s0.ctyp)) {
			s0.ctyp = s1.ctyp;  /* coerce so the type-equality test below passes */
		}
		/* Allow assignment between signed/unsigned variants and float types.
		 * Mask QVOLATILE too: a `volatile T *` lvalue (s1) carries the
		 * pointee qualifier, but it must not make this exact-type test
		 * spuriously differ from an unqualified RHS of the same T. */
		if ((s1.ctyp & ~FAR & ~QVOLATILE) != (s0.ctyp & ~FAR & ~QVOLATILE)
		    && !(KIND(s1.ctyp) == CHR && KIND(s0.ctyp) == INT)
		    && !((KIND(s1.ctyp) == KIND(s0.ctyp)) ||
		         ((KIND(s1.ctyp) & ~UNSIGNED) == (KIND(s0.ctyp) & ~UNSIGNED))
		         || ((KIND(s1.ctyp) & ~FLOAT) == (KIND(s0.ctyp) & ~FLOAT))))
			die("invalid assignment");
		/* Storing through a dereferenced far pointer uses storef*.
		 * A far flag on a PTR-kind lvalue (e.g. `vga = expr;` where
		 * `vga` is itself a far pointer variable) is regular storage —
		 * the slot lives in normal memory and holds the 4-byte far
		 * pointer value, so use storel/storew per irtyp. */
		if ((ISFAR(s1.ctyp) && KIND(s1.ctyp) != PTR && KIND(s1.ctyp) != FUN) || FARSTORAGE(s1) || s1_far_storage) {
			char t = irtyp(s1.ctyp);
			if (t == 'b')
				fprintf(of, "\tstorefb ");
			else if (t == 'h')
				fprintf(of, "\tstorefh ");
			else if (t == 'l')
				fprintf(of, "\tstorefl ");
			else if (t == 's')
				fprintf(of, "\tstorefs ");
			else
				fprintf(of, "\tstorefw ");
		} else {
			fprintf(of, "\tstore%c ", irtyp(s1.ctyp));
		}
		/* Volatile lvalue: keep the store (no dead-store kill / forward /
		 * reorder).  s1 is the lvalue: a volatile global/extern NAMED
		 * symbol, or a volatile-qualified deref (`*p` with p a
		 * `volatile T *`, whose pointee QVOLATILE rides on s1.ctyp). */
		fprintf(of, "%s", (symb_isvolatile(s1) || ISVOLATILE(s1.ctyp)) ? "volatile " : "");
		goto Args;

	case 'p':
	case 'm':
		/* Prefix increment/decrement: ++i, --i */
		o = n->op == 'p' ? '+' : '-';
		sl = lval(n->l);
		s0.t = Tmp;
		s0.u.n = tmp++;
		/* Strip the lvalue-deref FAR marker (set when sl reached us
		 * through a far-pointer dereference of a non-pointer scalar)
		 * but KEEP FAR on PTR/FUN value types — there it's the
		 * value-type bit "this is a 4-byte far pointer," and stripping
		 * it makes a downstream `*p++` deref pick near load instead of
		 * loadfar.  See [[minic-far-postinc-strips-far]]. */
		s0.ctyp = (KIND(sl.ctyp) == PTR || KIND(sl.ctyp) == FUN)
		        ? sl.ctyp
		        : (sl.ctyp & ~FAR);
		/* Load current value (handle far pointer).  Far load/store only
		 * applies when the lvalue is itself a far scalar (i.e. reached
		 * through a far-pointer dereference).  A far-pointer-typed
		 * *variable* lives in normal memory and its slot holds the
		 * 4-byte pointer value -- use plain load/store, matching the
		 * assignment case below.  See [[minic-far-var-assign-storefw]]. */
		if ((ISFAR(sl.ctyp) && KIND(sl.ctyp) != PTR && KIND(sl.ctyp) != FUN) || FARSTORAGE(sl)) {
			loadfar(s0, sl);
		} else {
			load(s0, sl);
		}
		/* The loaded value is a plain rvalue for the +/- arithmetic; drop
		 * the pointee QVOLATILE (the store-back below keys off sl.ctyp). */
		s0.ctyp &= ~QVOLATILE;
		s1.t = Con;
		s1.u.n = 1;
		s1.ctyp = INT;
		/* Compute new value */
		sr.ctyp = prom(o, &s0, &s1);
		if (!huge_ptr_binop(o, sr, s0, s1)
		 && !far_ptr_offset_binop(o, sr, s0, s1)) {
			fprintf(of, "\t");
			psymb(sr);
			fprintf(of, " =%c %s ", irtyp_ret(sr.ctyp), o == '+' ? "add" : "sub");
			psymb(s0);
			fprintf(of, ", ");
			psymb(s1);
			fprintf(of, "\n");
		}
		/* Store new value (handle far pointer) */
		if ((ISFAR(sl.ctyp) && KIND(sl.ctyp) != PTR && KIND(sl.ctyp) != FUN) || FARSTORAGE(sl)) {
			char t = irtyp(sl.ctyp);
			if (t == 'b')
				fprintf(of, "\tstorefb ");
			else if (t == 'h')
				fprintf(of, "\tstorefh ");
			else if (t == 'l')
				fprintf(of, "\tstorefl ");
			else if (t == 's')
				fprintf(of, "\tstorefs ");
			else
				fprintf(of, "\tstorefw ");
		} else {
			fprintf(of, "\tstore%c ", irtyp(sl.ctyp));
		}
		/* Volatile lvalue: keep the store (named global/extern or a
		 * volatile-qualified deref carried on sl.ctyp). */
		fprintf(of, "%s", (symb_isvolatile(sl) || ISVOLATILE(sl.ctyp)) ? "volatile " : "");
		psymb(sr);
		fprintf(of, ", ");
		psymb(sl);
		fprintf(of, "\n");
		/* Return new value (sr is already the result) */
		break;

	case 'P':
	case 'M':
		/* Postfix increment/decrement: i++, i-- */
		o = n->op == 'P' ? '+' : '-';
		sl = lval(n->l);
		s0.t = Tmp;
		s0.u.n = tmp++;
		/* Strip FAR on non-PTR scalars but KEEP it on PTR/FUN values
		 * — same reason as the prefix `case 'p'` above. */
		s0.ctyp = (KIND(sl.ctyp) == PTR || KIND(sl.ctyp) == FUN)
		        ? sl.ctyp
		        : (sl.ctyp & ~FAR);
		/* Load current value (see note above on PTR/FUN exclusion). */
		if ((ISFAR(sl.ctyp) && KIND(sl.ctyp) != PTR && KIND(sl.ctyp) != FUN) || FARSTORAGE(sl)) {
			loadfar(s0, sl);
		} else {
			load(s0, sl);
		}
		/* The loaded value is a plain rvalue for the +/- arithmetic; drop
		 * the pointee QVOLATILE (the store-back below keys off sl.ctyp). */
		s0.ctyp &= ~QVOLATILE;
		s1.t = Con;
		s1.u.n = 1;
		s1.ctyp = INT;
		goto Binop;

	default:
		s0 = expr(n->l);
		s1 = expr(n->r);
		o = n->op;
	Binop:
		sr.ctyp = prom(o, &s0, &s1);

		/* Under MHuge, pointer +/- offset must normalise; route the
		 * arithmetic through libstub's _qbe_huge_add / _qbe_huge_sub
		 * instead of emitting flat 32-bit add/sub.  See
		 * [[huge-mode-plan]] / [[huge-phase-a]]. */
		if (huge_ptr_binop(o, sr, s0, s1))
			break;

		/* Under MHuge, ptr - ptr (a byte/element COUNT, not a pointer) must
		 * be computed on the LINEAR addresses: two normalised far pointers
		 * into the same object can sit in different segments, so a flat
		 * 32-bit `sub` of their seg:off words gives (Δseg<<16)+Δoff instead
		 * of the true Δseg*16+Δoff.  `_qbe_huge_cmp(p,q)` already returns the
		 * signed linear difference linear(p)-linear(q); reuse it.  (Flat sub
		 * stays correct under compact/large, where the segment is shared and
		 * cancels — and under near-data.  Comparison `p<q` also stays flat:
		 * normalisation makes the seg:off word monotonic in linear address.)
		 * The element-size `div` post-step below the switch still runs after
		 * this break, so wider pointees scale correctly too (verified with
		 * int and long element types: q-p divides the linear byte delta by
		 * the element SIZE). */
		if (memmodel == MHuge && o == '-'
		&& KIND(s0.ctyp) == PTR && KIND(s1.ctyp) == PTR
		&& KIND(DREF(s0.ctyp)) != FUN) {
			fprintf(of, "\t");
			psymb(sr);
			fprintf(of, " =l call $qbe_huge_cmp(l ");
			psymb(s0);
			fprintf(of, ", l ");
			psymb(s1);
			fprintf(of, ")\n");
			break;
		}

		/* Under MHuge, a pointer RELATIONAL compare (p < q, p <= q; the
		 * parser lowers p > q / p >= q to swapped-operand < / <=) must be
		 * decided on the LINEAR addresses, exactly like the ptr-ptr
		 * subtraction above.  The flat `cultl` further down assumes BOTH
		 * operands are NORMALISED huge pointers, so the seg:off word is
		 * monotonic in linear address — but a bare symbol address ($sym,
		 * e.g. _sbrk's __heap_end) is UNNORMALISED (raw DGROUP:offset, the
		 * offset can exceed 0xF), so comparing it flat against a normalised
		 * pointer (one that went through _qbe_huge_add) gives the wrong
		 * order.  Route through _qbe_huge_cmp(p,q) = signed linear(p)-linear(q)
		 * and test its sign: p<q ⟺ cmp<0, p<=q ⟺ cmp<=0.  huge_cmp recomputes
		 * seg*16+off from the raw words, so it is normalisation-invariant and
		 * correct for both forms.  MHuge-gated, so the compact/large/near
		 * corpora — including the MP byte-compare — are untouched.  Found in
		 * §7u: malloc returned NULL because _sbrk's `next > __heap_end`
		 * mis-ordered the unnormalised __heap_end.  See [[huge-mode-plan]]. */
		if (memmodel == MHuge && (o == '<' || o == 'l')
		&& KIND(s0.ctyp) == PTR && KIND(s1.ctyp) == PTR
		&& KIND(DREF(s0.ctyp)) != FUN) {
			int ct = tmp++;
			fprintf(of, "\t%%t%d =l call $qbe_huge_cmp(l ", ct);
			psymb(s0);
			fprintf(of, ", l ");
			psymb(s1);
			fprintf(of, ")\n");
			sr.ctyp = INT;
			fprintf(of, "\t");
			psymb(sr);
			fprintf(of, " =%c %s %%t%d, 0\n", irtyp_ret(sr.ctyp),
				o == '<' ? "csltl" : "cslel", ct);
			break;
		}

		/* Under MHuge, a pointer EQUALITY compare (p == q, p != q) has the
		 * SAME latent flat-compare gap as the relational compare above: the
		 * `ceql`/`cnel` further down compares the raw 32-bit seg:off words
		 * bit-for-bit, so two pointers that denote the SAME linear address
		 * through DIFFERENT normalisations — e.g. an unnormalised symbol
		 * address (offset > 0xF) vs the normalised pointer _qbe_huge_add
		 * returns for the same byte — wrongly compare UNEQUAL (and a genuinely
		 * different address can never alias, so == has no false-positive risk;
		 * the bug is purely false-negative).  Route through _qbe_huge_cmp(p,q)
		 * = signed linear(p)-linear(q) and test == 0 / != 0: linear equality is
		 * exactly C pointer equality (C11 6.5.9) on the flat 8086 huge model,
		 * and it stays correct for NULL too (0:0 → linear 0, so p == NULL ⟺
		 * linear(p) == 0).  §7u closed the relational sibling (the live _sbrk
		 * `> __heap_end` consumer); this closes the equality gap noted there.
		 * MHuge-gated, so compact/large/near — including the MP byte-compare —
		 * are untouched.  See [[huge-mode-plan]]. */
		if (memmodel == MHuge && (o == 'e' || o == 'n')
		&& KIND(s0.ctyp) == PTR && KIND(s1.ctyp) == PTR
		&& KIND(DREF(s0.ctyp)) != FUN) {
			int ct = tmp++;
			fprintf(of, "\t%%t%d =l call $qbe_huge_cmp(l ", ct);
			psymb(s0);
			fprintf(of, ", l ");
			psymb(s1);
			fprintf(of, ")\n");
			sr.ctyp = INT;
			fprintf(of, "\t");
			psymb(sr);
			fprintf(of, " =%c %s %%t%d, 0\n", irtyp_ret(sr.ctyp),
				o == 'e' ? "ceql" : "cnel", ct);
			break;
		}

		/* Compact/large (or explicit __far): `far_ptr ± idx` must use the
		 * offset-only addfo/subfo ops (segment preserved, 16-bit offset
		 * wraparound) — a flat `=l add` of the Scale path's sign-extended
		 * index corrupts any in-segment offset >= 0x8000.  Covers a[i],
		 * p+i, p-i, and the postfix p++/p-- that reach Binop.  The post-switch
		 * ptr-ptr `div` and the P/M store-back still run after this break.
		 * See far_ptr_offset_binop / [[project-far-ptr-unsigned-index-bug]]. */
		if (far_ptr_offset_binop(o, sr, s0, s1))
			break;

		/* Validate operations on floating-point types */
		if (ISFLOAT(sr.ctyp)) {
			/* Disallow modulo on floats */
			if (o == '%')
				die("modulo operation not supported on floating-point types");
			/* Disallow bitwise operations on floats */
			if (strchr("&|^LR", o))
				die("bitwise operations not supported on floating-point types");
		}

		if (strchr("ne<l", n->op)) {
			char ct = irtyp(sr.ctyp);
			/* QBE compare ops are only typed w/l/s/d.  Widen
			 * byte/halfword operand types to word. */
			if (ct == 'b' || ct == 'h') ct = 'w';
			sprintf(ty, "%c", ct);
			sr.ctyp = INT;
		} else
			strcpy(ty, "");
		fprintf(of, "\t");
		psymb(sr);
		/* The result temp's class must be a valid QBE temp class
		   (w/l/s/d) — never the b/h memory suffixes that irtyp() yields
		   for char/short.  irtyp_ret() widens char/short to 'w', which is
		   also C-correct: integer arithmetic promotes sub-int operands to
		   int width and only truncates on store.  (e.g. uint16_t+uint16_t
		   and uint8_t+uint8_t, where prom() returns the operand type for
		   same-typed operands — see py/ringbuf.c, gc.c, emitbc.c.) */
		fprintf(of, " =%c", irtyp_ret(sr.ctyp));
		/* Handle comparisons based on type */
		if (ISFLOAT(s0.ctyp)) {
			/* Floating-point comparison: cXXt where XX is comparison and t is type */
			if (o == '<')
				fprintf(of, " clt%s ", ty);
			else if (o == 'l')  /* <= */
				fprintf(of, " cle%s ", ty);
			else if (o == 'e')  /* == */
				fprintf(of, " ceq%s ", ty);
			else if (o == 'n')  /* != */
				fprintf(of, " cne%s ", ty);
			else
				fprintf(of, " %s%s ", otoa[o], ty);
		} else if (strchr("<l", o) && (ISUNSIGNED(s0.ctyp) || ISUNSIGNED(s1.ctyp)
		           || KIND(s0.ctyp) == PTR || KIND(s1.ctyp) == PTR)) {
			/* Unsigned integer comparison.  POINTER relational compares
			 * (C11 6.5.8) are address comparisons and must be UNSIGNED
			 * too: pointer types never carry the UNSIGNED flag, so
			 * without the KIND==PTR check they fell through to the
			 * signed cslt/csle below — wrong whenever the operands
			 * straddle the sign bit (near offset >= 0x8000, or a far
			 * pointer's segment word >= 0x8000 vs one below).  Harmless
			 * by luck in images where every segment is >= 0x8000 (all
			 * "negative", so signed ordering matches), but real; found
			 * in §4o's VERIFY_PTR SSA audit. */
			fprintf(of, " %s%s ", o == '<' ? "cult" : "cule", ty);
		} else if ((o == '/' || o == '%')
		           && (ISUNSIGNED(s0.ctyp) || ISUNSIGNED(s1.ctyp))) {
			/* Unsigned integer divide / modulo. */
			fprintf(of, " %s%s ", o == '/' ? "udiv" : "urem", ty);
		} else if (o == 'R' && !ISUNSIGNED(s0.ctyp)) {
			/* C11 6.5.7: a >> b takes the (promoted) type of a;
			 * for signed a, the shift is arithmetic (sar), not logical.
			 * otoa['R'] = "shr" handles the unsigned case in the else. */
			fprintf(of, " sar%s ", ty);
		} else {
			/* Signed integer comparison or other operations */
			fprintf(of, " %s%s ", otoa[o], ty);
		}
	Args:
		psymb(s0);
		fprintf(of, ", ");
		psymb(s1);
		fprintf(of, "\n");
		break;

	}
	if (n->op == '-'
	&&  KIND(s0.ctyp) == PTR
	&&  KIND(s1.ctyp) == PTR) {
		fprintf(of, "\t%%t%d =%c div ", tmp, irtyp(sr.ctyp));
		psymb(sr);
		fprintf(of, ", %d\n", SIZE(DREF(s0.ctyp)));
		sr.u.n = tmp++;
	}
	if (n->op == 'P' || n->op == 'M') {
		/* Store new value (see note on the p/m prefix case above). */
		if ((ISFAR(sl.ctyp) && KIND(sl.ctyp) != PTR && KIND(sl.ctyp) != FUN) || FARSTORAGE(sl)) {
			char t = irtyp(sl.ctyp);
			if (t == 'b')
				fprintf(of, "\tstorefb ");
			else if (t == 'h')
				fprintf(of, "\tstorefh ");
			else if (t == 'l')
				fprintf(of, "\tstorefl ");
			else if (t == 's')
				fprintf(of, "\tstorefs ");
			else
				fprintf(of, "\tstorefw ");
		} else {
			fprintf(of, "\tstore%c ", irtyp(sl.ctyp));
		}
		/* Volatile lvalue: keep the store (named global/extern or a
		 * volatile-qualified deref carried on sl.ctyp). */
		fprintf(of, "%s", (symb_isvolatile(sl) || ISVOLATILE(sl.ctyp)) ? "volatile " : "");
		psymb(sr);
		fprintf(of, ", ");
		psymb(sl);
		fprintf(of, "\n");
		sr = s0;
	}
	return sr;
}

Symb
lval(Node *n)
{
	Symb sr;

	lval_storage_far = 0;  /* default: lvalue lives in near storage (slot/DGROUP) */

	switch (n->op) {
	default:
		die("invalid lvalue");
	case 'C':
	case 'I':
		/* A call is an lvalue only when it returns a struct/union by
		 * value: expr() allocates the result slot, emits the call, and
		 * yields the slot's address (so `f().field` derefs it).  This
		 * path runs only where expr() wasn't already called on the
		 * node — the struct-assignment path reuses its own s0 instead
		 * to avoid emitting the call twice. */
		sr = expr(n);
		if (KIND(sr.ctyp) != STRUCT_T && KIND(sr.ctyp) != UNION_T)
			die("invalid lvalue");
		break;
	case 'V':
		if (!varget(n->u.v)) {
			static char ediag[NString + 32];
			sprintf(ediag, "undefined variable %s", n->u.v);
			die(ediag);
		}
		sr = *varget(n->u.v);
		/* A far global variable lives in far storage; a local/near one
		 * does not (even if its value type is a far pointer). */
		lval_storage_far = FARSTORAGE(sr) ? 1 : 0;
		/* A directly-declared `volatile` struct/union OBJECT: re-derive its
		 * QVOLATILE qualifier (varadd stripped it from the stored type into
		 * varh[].isvolatile) and carry it on the aggregate lvalue, so EVERY
		 * member access — offset 0 AND offset>0 — sees ISVOLATILE(s0.ctyp)
		 * and emits a volatile load/store (the member-access paths OR it onto
		 * the member value type; the lval `.` path propagates it so nested
		 * `s.inner.x` composes).  Restricted to STRUCT_T/UNION_T so scalar
		 * volatile locals/globals stay byte-identical — those are handled by
		 * markvol (volatile alloc) / symb_isvolatile (named global). */
		if ((KIND(sr.ctyp) == STRUCT_T || KIND(sr.ctyp) == UNION_T) &&
		    var_isvolatile(n->u.v))
			sr.ctyp |= QVOLATILE;
		break;
	case 'L':
		/* Compound literal as lvalue - allocate and initialize, return address */
		{
			unsigned ctyp = (unsigned)n->u.n;
			int s = SIZE(ctyp);
			int clitnum = clit++;
			Node *init;
			int i;

			/* Allocate temporary storage */
			fprintf(of, "\t%%_clit%d =%c alloc%d %d\n", clitnum, ALLOC_T(), iralign(ctyp), s);

			if (KIND(ctyp) == STRUCT_T || KIND(ctyp) == UNION_T) {
				/* Struct/union initialization */
				int sidx = DREF(ctyp);
				init = n->l;
				i = 0;

				/* Zero-initialize first (C11 6.7.9p21: members with no
				 * explicit initializer are zeroed). */
				{
					char caddr[24];
					snprintf(caddr, sizeof caddr, "%%_clit%d", clitnum);
					emit_zero_aggr(caddr, s);

					/* Initialize members from initlist with designator
					 * and nested-brace support (see emit_clit_aggr). */
					(void)i;
					emit_clit_aggr(caddr, 0, sidx, init);
				}
			} else {
				/* Scalar initialization */
				init = n->l;
				if (init) {
					Symb val = expr(init->l);
					fprintf(of, "\tstore%c ", irtyp(ctyp));
					psymb(val);
					fprintf(of, ", %%_clit%d\n", clitnum);
				}
			}

			/* Return address as lvalue */
			sr.t = Tmp;
			sr.u.n = tmp++;
			sr.ctyp = ctyp;
			fprintf(of, "\t");
			psymb(sr);
			fprintf(of, " =w copy %%_clit%d\n", clitnum);
		}
		break;
	case '@':
		sr = expr(n->l);
		if (KIND(sr.ctyp) != PTR)
			die("dereference of a non-pointer");
		/* Preserve FAR flag to indicate this address came from far pointer dereference */
		{
			unsigned far_flag = ISFAR(sr.ctyp) ? FAR : 0;
			/* Storage is far iff the dereferenced POINTER was far (i.e. the
			 * pointee lives at a far seg:off address). */
			lval_storage_far = far_flag ? 1 : 0;
			sr.ctyp = DREF(sr.ctyp) | far_flag;
		}
		break;
	case '.':
		/* Member access is also an lvalue - compute address */
		{
			Symb s0 = lval(n->l);  /* Get struct lvalue */
			if (KIND(s0.ctyp) != STRUCT_T && KIND(s0.ctyp) != UNION_T)
				die("member access on non-struct/union");

			int sidx = DREF(s0.ctyp);
			char *mname = n->r->u.v;
			int i, found = 0;
			struct Member *m;
			char klass;
			unsigned far_flag;

			/* Find member */
			for (i = 0; i < structh[sidx].nmembers; i++) {
				if (strcmp(structh[sidx].members[i].name, mname) == 0) {
					found = 1;
					m = &structh[sidx].members[i];
					break;
				}
			}
			if (!found)
				die("struct member not found");

			/* Stash this member's fn-ptr prototype id (or -1) for an
			 * immediately-following indirect call `obj->fn(...)' (§2q). */
			g_callee_fpid = m->fpid;

			/* Compute member address: struct_addr + offset.  Under far-
			 * data, when s0 came through a far-ptr deref it carries the
			 * FAR bit and the address is Kl.  Propagate FAR onto sr so
			 * the assignment site (which checks ISFAR(sl.ctyp)) routes
			 * through storefar instead of plain store.  FARSTORAGE: a
			 * global struct lives in a far segment, so `g.m = x` also
			 * needs the Kl address + FAR propagation.  !NEAR_DATA(): a
			 * local aggregate's address is far Kl as well. */
			{
			int base_far = ISFAR(s0.ctyp) || FARSTORAGE(s0) || !NEAR_DATA();
			klass = base_far ? 'l' : 'w';
			far_flag = base_far ? FAR : 0;
			/* The member lives in far storage iff its containing struct
			 * does — independent of whether the member's own value type
			 * is a far pointer.  The store site needs this to far-store a
			 * pointer member of a far struct. */
			lval_storage_far = base_far ? 1 : 0;
			}
			/* Volatile lvalue if the member is volatile-qualified
			 * (QVOLATILE in m->ctyp) OR the aggregate is volatile
			 * (`volatile struct S *p`, QVOLATILE on s0.ctyp); carry it on
			 * the lvalue value-type so the assignment / inc-dec store
			 * sites (which check ISVOLATILE(sl.ctyp)) keep the store. */
			if (m->offset > 0) {
				sr.t = Tmp;
				sr.u.n = tmp++;
				sr.ctyp = m->ctyp | far_flag | ISVOLATILE(s0.ctyp);
				fprintf(of, "\t");
				psymb(sr);
				fprintf(of, " =%c add ", klass);
				psymb(s0);
				fprintf(of, ", %d\n", m->offset);
			} else {
				/* Offset 0, just use struct address */
				sr = s0;
				sr.ctyp = m->ctyp | far_flag | ISVOLATILE(s0.ctyp);
			}
		}
		break;
	}
	return sr;
}

void
branch(Node *n, int lt, int lf)
{
	Symb s;
	int l;

	switch (n->op) {
	default:
		s = expr(n);
		/* QBE's `jnz` is typed `w` — it tests only 16 bits.  For a
		 * Kl value (far pointer, `long`, `long long`) we MUST first
		 * compare against 0 with the right width, otherwise a far
		 * pointer like (segment=0x1234, offset=0x0000) reads as
		 * NULL and we take the wrong branch.  Surfaced as stevie's
		 * inc() returning -1 on the first byte under compact — the
		 * `if (lp && lp->linep)` test truncated the far pointer to
		 * its offset and called it zero when the offset happened
		 * to land on a clean paragraph. */
		if (irtyp(s.ctyp) == 'l') {
			Symb cmp;
			cmp.t = Tmp;
			cmp.u.n = tmp++;
			cmp.ctyp = INT;
			fprintf(of, "\t");
			psymb(cmp);
			fprintf(of, " =w cnel ");
			psymb(s);
			fprintf(of, ", 0\n");
			s = cmp;
		}
		fprintf(of, "\tjnz ");
		psymb(s);
		fprintf(of, ", @l%d, @l%d\n", lt, lf);
		break;
	case 'o':
		l = lbl;
		lbl += 1;
		branch(n->l, lt, l);
		fprintf(of, "@l%d\n", l);
		branch(n->r, lt, lf);
		break;
	case 'a':
		l = lbl;
		lbl += 1;
		branch(n->l, l, lf);
		fprintf(of, "@l%d\n", l);
		branch(n->r, lt, lf);
		break;
	}
}

void
collectcases(Stmt *s, Stmt **cases, int *ncase, int *defidx)
{
	if (!s)
		return;
	if (s->t == Seq) {
		collectcases((Stmt*)s->p1, cases, ncase, defidx);
		collectcases((Stmt*)s->p2, cases, ncase, defidx);
	} else if (s->t == Case || s->t == Default) {
		if (s->t == Default)
			*defidx = *ncase;
		cases[(*ncase)++] = s;
		/* Fallthrough labels parse as nested Case nodes, e.g.
		 *   case A: case B: stmt;  →  Case(A, p2=Case(B, p2=stmt))
		 * Recurse into p2 so the inner label is also collected. */
		collectcases((Stmt*)s->p2, cases, ncase, defidx);
	}
}

int genswitchbody(Stmt *s, int brk, int cont, Stmt **cases, int *caselbl, int ncase);

int
genswitch(Symb val, Stmt *body, int brk, int cont)
{
	Stmt *cases[128];
	int ncase, defidx, i;
	int caselbl[128];

	ncase = 0;
	defidx = -1;
	collectcases(body, cases, &ncase, &defidx);

	/* Allocate labels for all cases */
	for (i = 0; i < ncase; i++) {
		caselbl[i] = lbl++;
	}

	/* Generate case comparisons */
	for (i = 0; i < ncase; i++) {
		if (cases[i]->t == Case) {
			fprintf(of, "\t%%t%d =w ceqw ", tmp);
			psymb(val);
			fprintf(of, ", %d\n", cases[i]->val);
			fprintf(of, "\tjnz %%t%d, @l%d, @l%d\n", tmp++, caselbl[i], lbl);
			fprintf(of, "@l%d\n", lbl++);
		}
	}

	/* Jump to default or break */
	if (defidx >= 0)
		fprintf(of, "\tjmp @l%d\n", caselbl[defidx]);
	else
		fprintf(of, "\tjmp @l%d\n", brk);

	/* Generate switch body linearly */
	genswitchbody(body, brk, cont, cases, caselbl, ncase);

	return 0;
}

int
contains_case_label(Stmt *s)
{
	if (!s) return 0;
	if (s->t == Case || s->t == Default) return 1;
	if (s->t == Seq)
		return contains_case_label((Stmt*)s->p1)
		    || contains_case_label((Stmt*)s->p2);
	return 0;
}

/* Does this statement subtree contain a goto target (Label)?  A label is a
   fresh re-entry point reachable by `goto`, so a Seq whose tail contains one
   must report its termination as the tail's alone — an earlier statement's
   terminator must not mask the labeled block falling through (the parallel of
   contains_case_label for switch bodies). */
int
contains_label(Stmt *s)
{
	if (!s) return 0;
	if (s->t == Label) return 1;
	if (s->t == Seq)
		return contains_label((Stmt*)s->p1)
		    || contains_label((Stmt*)s->p2);
	return 0;
}

int
genswitchbody(Stmt *s, int brk, int cont, Stmt **cases, int *caselbl, int ncase)
{
	int i;

	if (!s)
		return 0;

	if (s->t == Seq) {
		int r1 = genswitchbody((Stmt*)s->p1, brk, cont, cases, caselbl, ncase);
		/* Mirror stmt(Seq)'s short-circuit: if p1 terminates the basic
		 * block (ret/break/continue), skip p2 unless p2 contains a case
		 * label — case labels in a switch body must be reachable even
		 * if a previous case fell through to a terminator. */
		/* A goto Label between cases (e.g. `break; power_overflow: …;
		 * case …:` in py/runtime.c's mp_binary_op) is also a fresh
		 * re-entry point, so it must not be skipped either. */
		if (r1 && !contains_case_label((Stmt*)s->p2)
		       && !contains_label((Stmt*)s->p2))
			return r1;
		int r2 = genswitchbody((Stmt*)s->p2, brk, cont, cases, caselbl, ncase);
		/* When p2 contains a case label (or a goto label) it provides a
		 * fresh re-entry point: even if p1 terminated the prior basic
		 * block, control can resume at the label and fall through past
		 * p2.  The combined termination state of this Seq is therefore r2
		 * alone — propagating r1 would let an earlier case body's `break`
		 * poison enclosing Seqs and suppress sibling stmts that
		 * legitimately follow the label. */
		if (contains_case_label((Stmt*)s->p2)
		 || contains_label((Stmt*)s->p2))
			return r2;
		return r1 || r2;
	} else if (s->t == Case || s->t == Default) {
		/* Find this case in the cases array and emit its label */
		for (i = 0; i < ncase; i++) {
			if (cases[i] == s) {
				fprintf(of, "@l%d\n", caselbl[i]);
				break;
			}
		}
		/* Process the statement after the case label */
		if (s->p2)
			return genswitchbody((Stmt*)s->p2, brk, cont, cases, caselbl, ncase);
		return 0;
	} else {
		/* Regular statement - process normally */
		return stmt(s, brk, cont);
	}
}

/* Map an inline-asm clobber name to the i8086 register bit the QBE
 * backend allocates (i8086/all.h: RAX=1,RCX=2,RDX=3,RBX=4,RSI=5,RDI=6;
 * a MAKESURE there pins those values so a reorder breaks the build).
 * "memory" and the non-GP clobbers (cc/flags/es/ds/sp/bp) map to 0 —
 * "memory" is handled by QBE's asmvol() pass and the segment/frame regs
 * are not GP-allocatable.  The OR of these masks is emitted as the
 * `asm "code", <mask>` operand so spill.c/rega.c keep no live value in a
 * clobbered reg across the asm. */
static unsigned long
asm_clobber_bit(const char *c)
{
	if (!strcmp(c,"ax")||!strcmp(c,"eax")||!strcmp(c,"al")||!strcmp(c,"ah"))
		return 1UL<<1;  /* RAX */
	if (!strcmp(c,"cx")||!strcmp(c,"ecx")||!strcmp(c,"cl")||!strcmp(c,"ch"))
		return 1UL<<2;  /* RCX */
	if (!strcmp(c,"dx")||!strcmp(c,"edx")||!strcmp(c,"dl")||!strcmp(c,"dh"))
		return 1UL<<3;  /* RDX */
	if (!strcmp(c,"bx")||!strcmp(c,"ebx")||!strcmp(c,"bl")||!strcmp(c,"bh"))
		return 1UL<<4;  /* RBX */
	if (!strcmp(c,"si")||!strcmp(c,"esi"))
		return 1UL<<5;  /* RSI */
	if (!strcmp(c,"di")||!strcmp(c,"edi"))
		return 1UL<<6;  /* RDI */
	return 0;
}

static unsigned long
asm_clobber_mask(struct AsmStmt *a)
{
	unsigned long m = 0;
	int i;
	for (i = 0; i < a->nclobbers; i++)
		m |= asm_clobber_bit(a->clobbers[i]);
	return m;
}

int
stmt(Stmt *s, int b, int c)
{
	int l, r;
	Symb x;

	if (!s)
		return 0;

	switch (s->t) {
	case Ret:
		if (cur_fn_sret) {
			/* Struct/union return-by-value: copy the returned
			 * aggregate into the caller's storage (the hidden
			 * pointer, reloaded from %__sret) and return that
			 * pointer.  The source is an aggregate lvalue, so take
			 * its address — except a struct-returning call, whose
			 * expr() already yields the result-slot address. */
			Symb src, dst;
			Node *rv = (Node *)s->p1;
			if (!rv)
				die("return; in struct-returning function");
			if (rv->op == 'C' || rv->op == 'I')
				src = expr(rv);
			else
				src = lval(rv);
			/* The hidden pointer is far under far-data models (it
			 * addresses caller storage as a 4-byte seg:off), near
			 * under medium.  Reload it and copy the aggregate. */
			dst.t = Tmp;
			dst.u.n = tmp++;
			dst.ctyp = cur_fn_sret_ctyp | (NEAR_DATA() ? 0 : FAR);
			fprintf(of, "\t");
			psymb(dst);
			fprintf(of, " =%c load%c %%__sret\n", DATAPTR_T(), DATAPTR_T());
			/* Mirror the dst's far-ness onto the source aggregate
			 * address: under far-data every data address is a far
			 * (seg:off) pointer, so the copy must use the far load/
			 * store variants on both sides. */
			if (!NEAR_DATA())
				src.ctyp |= FAR;
			emit_struct_copy(dst, src);
			fprintf(of, "\tret %%t%d\n", dst.u.n);
			return 1;
		}
		if (s->p1) {
			x = expr(s->p1);
			/* Coerce the return value to the function's declared return
			 * type, mirroring the assignment converter.  Without this a
			 * narrow (INT/CHR) value returned from an `l` function reached
			 * `ret %tN` as a `w` temp; selret never widened it to DX:AX, so
			 * the function returned stale AX:DX (the MicroPython lexer's
			 * `mp_uint_t readbyte(){ return *cur; }` returned garbage, which
			 * the parser saw as MP_LEXER_INVALID_BYTE and hung).  Float and
			 * narrowing conversions are handled too for completeness. */
			if (ISFLOAT(curfntyp) && !ISFLOAT(x.ctyp)) {
				fprintf(of, "\t%%t%d =%c ", tmp, irtyp(curfntyp));
				fprintf(of, KIND(x.ctyp) == LNG ? "sltof " : "swtof ");
				psymb(x);
				fprintf(of, "\n");
				x.t = Tmp; x.ctyp = curfntyp; x.u.n = tmp++;
			} else if (!ISFLOAT(curfntyp) && ISFLOAT(x.ctyp)) {
				fprintf(of, "\t%%t%d =%c ", tmp, irtyp(curfntyp));
				fprintf(of, KIND(curfntyp) == LNG ? "dtosi " : "stosi ");
				psymb(x);
				fprintf(of, "\n");
				x.t = Tmp; x.ctyp = curfntyp; x.u.n = tmp++;
			} else if (ISFLOAT(curfntyp) && ISFLOAT(x.ctyp)
			           && (KIND(curfntyp) == LNG) != (KIND(x.ctyp) == LNG)) {
				/* Convert between float and double only on a real
				 * precision change (compare KIND, not the full ctyp —
				 * the far-data FAR bit must not force a bogus truncd). */
				fprintf(of, "\t%%t%d =%c ", tmp, irtyp(curfntyp));
				fprintf(of, KIND(curfntyp) == LNG ? "exts " : "truncd ");
				psymb(x);
				fprintf(of, "\n");
				x.t = Tmp; x.ctyp = curfntyp; x.u.n = tmp++;
			} else if (KIND(curfntyp) == LNG &&
			           (KIND(x.ctyp) == INT || KIND(x.ctyp) == CHR) &&
			           !ISFLOAT(curfntyp)) {
				/* Widen a narrow integer value to the long return. */
				sext(&x);
			} else if (KIND(curfntyp) != LNG && KIND(curfntyp) != PTR &&
			           KIND(curfntyp) != FUN &&
			           KIND(x.ctyp) == LNG && !ISFLOAT(x.ctyp)) {
				/* Narrow a long value to an int/char return. */
				fprintf(of, "\t%%t%d =w copy ", tmp);
				psymb(x);
				fprintf(of, "\n");
				x.t = Tmp;
				x.ctyp = (KIND(curfntyp) == CHR) ? CHR : INT;
				x.u.n = tmp++;
			}
			fprintf(of, "\tret ");
			psymb(x);
			fprintf(of, "\n");
		} else {
			fprintf(of, "\tret\n");
		}
		return 1;
	case Break:
		if (b < 0)
			die("break not in loop");
		fprintf(of, "\tjmp @l%d\n", b);
		return 1;
	case Continue:
		if (c < 0)
			die("continue not in loop");
		fprintf(of, "\tjmp @l%d\n", c);
		return 1;
	case Expr:
		expr(s->p1);
		return 0;
	case Seq: {
		/* Always evaluate both sub-statements — if the first
		 * terminated control flow, we still need to emit any
		 * labels (and following code) from the second so that
		 * `goto` jumps from earlier blocks land somewhere. */
		int r1 = stmt(s->p1, b, c);
		int r2 = stmt(s->p2, b, c);
		/* Fall-through termination of a sequence is its LAST statement's.
		   When the tail (p2) contains a label it is a goto re-entry point
		   that may fall through, so an earlier statement's terminator must
		   not mask it — report r2 alone (mirrors genswitchbody's case-label
		   handling).  This makes a function whose textual tail is a
		   goto-reached non-returning block (e.g. `value_error:
		   mp_raise_ValueError(...)` in py/parsenum.c, py/compile.c,
		   py/objstr.c) still get its synthetic trailing `ret`, so the last
		   block carries a terminator. */
		if (contains_label((Stmt*)s->p2))
			return r2;
		return r1 || r2;
	}
	case If:
		l = lbl;
		lbl += 3;
		branch(s->p1, l, l+1);
		fprintf(of, "@l%d\n", l);
		if (!(r=stmt(s->p2, b, c)))
		if (s->p3)
			fprintf(of, "\tjmp @l%d\n", l+2);
		fprintf(of, "@l%d\n", l+1);
		if (s->p3)
		if (!(r &= stmt(s->p3, b, c)))
			fprintf(of, "@l%d\n", l+2);
		return s->p3 && r;
	case While:
		l = lbl;
		lbl += 3;
		fprintf(of, "@l%d\n", l);              /* cond / continue target */
		branch(s->p1, l+1, l+2);
		fprintf(of, "@l%d\n", l+1);
		/* break = l+2, continue = l (cond re-check) */
		if (!stmt(s->p2, l+2, l))
			fprintf(of, "\tjmp @l%d\n", l);
		fprintf(of, "@l%d\n", l+2);
		return 0;
	case DoWhile:
		l = lbl;
		lbl += 3;
		fprintf(of, "@l%d\n", l);
		/* break = l+2, continue = l+1 (test label) */
		if (!stmt(s->p1, l+2, l+1))
			fprintf(of, "\tjmp @l%d\n", l+1);
		fprintf(of, "@l%d\n", l+1);
		branch(s->p2, l, l+2);
		fprintf(of, "@l%d\n", l+2);
		return 0;
	case For:
		/* For loop with explicit continue target on the inc step.
		 * Layout:
		 *   @l   : cond check
		 *   @l+1 : body
		 *   @l+2 : continue target (runs inc, jmps to @l)
		 *   @l+3 : break target (end)
		 * p1 = cond (Node), p2 = body (Stmt), p4 = inc (Node or 0).
		 */
		l = lbl;
		lbl += 4;
		fprintf(of, "@l%d\n", l);
		branch(s->p1, l+1, l+3);
		fprintf(of, "@l%d\n", l+1);
		if (!stmt(s->p2, l+3, l+2))
			fprintf(of, "\tjmp @l%d\n", l+2);
		fprintf(of, "@l%d\n", l+2);
		if (s->p4)
			expr(s->p4);
		fprintf(of, "\tjmp @l%d\n", l);
		fprintf(of, "@l%d\n", l+3);
		return 0;
	case Switch:
		x = expr(s->p1);
		l = lbl++;
		genswitch(x, (Stmt*)s->p2, l, c);
		fprintf(of, "@l%d\n", l);
		return 0;
	case Case:
	case Default:
		/* These are handled within genswitch */
		if (s->p2)
			stmt((Stmt*)s->p2, b, c);
		return 0;
	case Goto:
		fprintf(of, "\tjmp @user_%s_F%d\n", s->label, cur_fn_labelid);
		return 1;
	case Label:
		fprintf(of, "@user_%s_F%d\n", s->label, cur_fn_labelid);
		return stmt(s->p1, b, c);
	case Asm: {
		struct AsmStmt *a = (struct AsmStmt *)s->p1;
		char processed[2048];
		char *src = a->code;
		char *dst = processed;
		int opnum;

		/*
		 * Process inline assembly:
		 * 1. For basic asm (no operands), emit directly
		 * 2. For extended asm with operands, substitute %0, %1, etc.
		 * 3. Emit clobbers as a comment for documentation
		 */
		unsigned long clobmask = asm_clobber_mask(a);
		if (a->noutputs == 0 && a->ninputs == 0) {
			/* Simple inline assembly - emit directly */
			fprintf(of, "\tasm \"%s\"", a->code);
		} else {
			/*
			 * Extended inline assembly with operands
			 * Process the template and substitute operand references
			 */
			while (*src && (dst - processed) < (int)sizeof(processed) - 100) {
				if (*src == '%' && isdigit(*(src+1))) {
					/* Operand reference %N */
					opnum = *(src+1) - '0';
					src += 2;

					if (opnum < a->noutputs) {
						/* Output operand */
						Symb s = lval(a->outputs[opnum].expr);
						if (s.t == Var) {
							/* Local: emit the bare temp ref `%name`.
							 * The i8086 backend resolves it to the
							 * slot's [bp±N] at emit time, and QBE's
							 * asmvol() keeps the slot in memory (an asm
							 * output the dataflow can't see).  Emitting
							 * `[bp-%name]` here was a never-resolved
							 * guess (the §6a "=r"/"=m" gap). */
							dst += sprintf(dst, "%%%s", s.u.v);
						} else if (s.t == Glo) {
							/* Global variable - use NASM syntax with underscore prefix */
							dst += sprintf(dst, "[_glo%d]", s.u.n);
						} else {
							dst += sprintf(dst, "[unknown]");
						}
					} else {
						/* Input operand (indexed after outputs) */
						int inpidx = opnum - a->noutputs;
						if (inpidx < a->ninputs) {
							Symb s = lval(a->inputs[inpidx].expr);
							if (s.t == Var) {
								/* Local: bare temp ref `%name`,
								 * resolved to [bp±N] by the i8086
								 * backend (see the output branch). */
								dst += sprintf(dst, "%%%s", s.u.v);
							} else if (s.t == Glo) {
								/* Global variable - use NASM syntax with underscore prefix */
								dst += sprintf(dst, "[_glo%d]", s.u.n);
							} else {
								dst += sprintf(dst, "[unknown]");
							}
						}
					}
				} else {
					*dst++ = *src++;
				}
			}
			*dst = '\0';
			fprintf(of, "\tasm \"%s\"", processed);
		}
		/* Register-clobber mask operand (only when nonzero, so a
		 * memory-only clobber leaves the .ssa byte-stable).  QBE's
		 * spill.c/rega.c use it to keep no live value in a clobbered
		 * reg across the asm. */
		if (clobmask)
			fprintf(of, ", %lu", clobmask);
		fprintf(of, "\n");
		/* Emit clobbers as a comment for documentation/debugging */
		if (a->nclobbers > 0) {
			fprintf(of, "# clobbers:");
			for (int i = 0; i < a->nclobbers; i++) {
				fprintf(of, " %s", a->clobbers[i]);
			}
			fprintf(of, "\n");
		}
		return 0;
	}
	default:
		die("unknown statement type");
		return 0;
	}
}

Node *
mknode(char op, Node *l, Node *r)
{
	Node *n;

	n = alloc(sizeof *n);
	n->op = op;
	n->nlong = 0;
	n->l = l;
	n->r = r;
	return n;
}

Node *
mkidx(Node *a, Node *i)
{
	Node *n;
	int d;

	/* Array-of-array-typedef subscript (`jmp_buf bufs[i]`): minic's flat
	 * type system can't represent `int (*)[8]`, so bufs is registered as a
	 * plain int* array (value type IDIR(arrayelem)).  A one-level subscript
	 * must yield the ROW ADDRESS bufs + i*D (D = inner dim), NOT load a
	 * scalar.  Multiply the index by D and emit a bare pointer add (no
	 * deref): the '+' Scale path then scales by sizeof(arrayelem), giving the
	 * byte offset i*D*sizeof(elem), and the result is the int* row pointer.
	 * This composes — bufs[i][j] takes the normal `@(+ . j)` path below on
	 * that int*, and setjmp(bufs[i]) gets the row address it wants.  Only
	 * fires for aoa variables (flag-gated), so all other code is unchanged. */
	if (a->op == 'V' && (d = var_aoa_dim(a->u.v)) > 0) {
		Node *dim = mknode('N', 0, 0);
		dim->u.n = d;
		return mknode('+', a, mknode('*', i, dim));
	}

	n = mknode('+', a, i);
	n = mknode('@', n, 0);
	return n;
}

/* Build a deferred initializer for a block-scoped local array
 * `T a[] = { … }` / `T a[N] = { … }`: a comma-chain of `a[idx] = val`
 * assignments (via mkidx, so expr() scales each index by the element
 * size at emit time).  Returned as one Node to wrap in mkstmt(Expr,…)
 * so the stores run in lexical/control-flow order rather than at
 * function entry.  `*out_n` receives the inferred element count
 * (max designated index + 1 / sequential count).  If `zerofill` is
 * nonzero, `*out_n` elements are zeroed first (sized partial-init
 * semantics). */
Node *mkidx(Node *, Node *);
Node *
mk_local_array_init(char *v, Node *initlist, int zerofill, int known_n, int *out_n)
{
	Node *chain = 0, *it;
	int i = 0, n = 0;

	for (it = initlist; it; it = it->r) {
		Node *item = it->l, *av, *iv, *lhs, *asgn;
		int idx;

		if (item->op == 'd') {
			idx = item->r->u.n;
			i = idx + 1;
		} else {
			idx = i++;
		}
		if (idx + 1 > n)
			n = idx + 1;
		av = mknode('V', 0, 0);
		strcpy(av->u.v, v);
		iv = mknode('N', 0, 0);
		iv->u.n = idx;
		lhs = mkidx(av, iv);
		asgn = mknode('=', lhs, item->op == 'd' ? item->l : item);
		chain = chain ? mknode(',', chain, asgn) : asgn;
	}
	if (known_n > n)
		n = known_n;
	*out_n = n;

	if (zerofill) {
		Node *zhead = 0;
		int j;
		for (j = 0; j < n; j++) {
			Node *av = mknode('V', 0, 0), *iv = mknode('N', 0, 0);
			Node *lhs, *zasgn, *zero;
			strcpy(av->u.v, v);
			iv->u.n = j;
			lhs = mkidx(av, iv);
			zero = mknode('N', 0, 0);
			zero->u.n = 0;
			zasgn = mknode('=', lhs, zero);
			zhead = zhead ? mknode(',', zhead, zasgn) : zasgn;
		}
		if (zhead)
			chain = chain ? mknode(',', zhead, chain) : zhead;
	}
	return chain;
}

/* Decode the string literal stored at global index `idx` into raw bytes
 * (NOT including the terminating NUL) written to out[0..max).  Returns the
 * number of content bytes decoded.  The escape handling mirrors
 * strlit_bytelen() exactly so the decoded count agrees with sizeof. */
static int
strlit_decode(int idx, unsigned char *out, int max)
{
	char *s = ini[idx];
	int contentlen, n = 0;

	s += 5;                 /* skip the `{ b "` prefix */
	contentlen = (int)strlen(s);
	contentlen -= 8;        /* drop the `", b 0 }` suffix */
	if (contentlen < 0)
		contentlen = 0;
	while (contentlen > 0 && n < max) {
		unsigned char b;
		if (*s == '\\' && contentlen > 1) {
			s++; contentlen--;          /* the escape char */
			if (*s == 'x') {
				int v = 0;
				s++; contentlen--;
				while (contentlen > 0 &&
				    ((*s >= '0' && *s <= '9') ||
				     (*s >= 'a' && *s <= 'f') ||
				     (*s >= 'A' && *s <= 'F'))) {
					int d = (*s <= '9') ? *s - '0' :
					    (*s <= 'F') ? *s - 'A' + 10 :
					    *s - 'a' + 10;
					v = v * 16 + d;
					s++; contentlen--;
				}
				b = (unsigned char)v;
			} else if (*s >= '0' && *s <= '7') {
				int v = 0, k = 0;
				while (contentlen > 0 && k < 3 &&
				    *s >= '0' && *s <= '7') {
					v = v * 8 + (*s - '0');
					s++; contentlen--; k++;
				}
				b = (unsigned char)v;
			} else {
				switch (*s) {
				case 'n': b = '\n'; break;
				case 't': b = '\t'; break;
				case 'r': b = '\r'; break;
				case 'a': b = '\a'; break;
				case 'b': b = '\b'; break;
				case 'f': b = '\f'; break;
				case 'v': b = '\v'; break;
				case '\\': b = '\\'; break;
				case '"': b = '"'; break;
				case '\'': b = '\''; break;
				case '?': b = '?'; break;
				default: b = (unsigned char)*s; break;
				}
				s++; contentlen--;
			}
		} else {
			b = (unsigned char)*s;
			s++; contentlen--;
		}
		out[n++] = b;
	}
	return n;
}

/* Build a deferred initializer for a block-scoped (non-static) local char
 * array `char v[total] = "string";`.  The literal's decoded bytes occupy
 * v[0..natural) and the slack v[natural..total) is zero-filled (C
 * char-array string-init semantics).  Each byte is a deferred `v[i] = B`
 * assignment (mkidx scales by sizeof(elem)==1 for char), chained into one
 * comma node so the stores run in lexical/control-flow order and re-run on
 * each block re-entry.  `total` must be >= natural (the caller checks). */
static Node *
mk_local_string_init(char *v, int str_idx, long total)
{
	static unsigned char bytes[65536];
	int natural;
	long i;
	Node *chain = 0;

	natural = strlit_decode(str_idx, bytes, sizeof bytes);
	for (i = 0; i < total; i++) {
		Node *av = mknode('V', 0, 0);
		Node *iv = mknode('N', 0, 0);
		Node *bv = mknode('N', 0, 0);
		Node *lhs, *asgn;
		strcpy(av->u.v, v);
		iv->u.n = (int)i;
		lhs = mkidx(av, iv);
		bv->u.n = (i < natural) ? bytes[i] : 0;
		asgn = mknode('=', lhs, bv);
		chain = chain ? mknode(',', chain, asgn) : asgn;
	}
	return chain;
}

/* Build a deferred initializer for a block-scoped local 2-D table
 * `ELEM v[N] = {{…},{…}}` whose element ELEM is an array typedef of inner
 * dimension `dim` (`typedef int row3_t[3]`).  minic has no true `int[N][3]`,
 * so `v` is registered as a plain IDIR(elem) array with aoa_dim=dim (§7e) and
 * a one-level subscript `v[r]` yields the ROW address.  Brace-init therefore
 * cannot reuse mkidx (it would re-multiply the index by dim); instead each
 * `{…}` row is flattened into per-element scalar stores `*(v + (r*dim + c))`.
 * The bare `'+'` Scale path scales the linear index by sizeof(elem) (v decays
 * to IDIR(elem)), so the store lands at byte offset (r*dim+c)*sizeof(elem).
 * Returns the comma-chain (or 0); `rows` is the declared row count N (0 =>
 * infer), `*out_rows` receives the row count used, and `zerofill` zeroes all
 * N*dim elements first (sized partial-init semantics).  Brace-elided flat
 * scalars fill linearly as a fallback (the canonical form is fully braced). */
static Node *
mk_aoa_array_init(char *v, Node *initlist, int dim, int zerofill,
                  int rows, int *out_rows)
{
	Node *chain = 0, *it;
	int lin = 0, nrows = 0;

	for (it = initlist; it; it = it->r) {
		Node *item = it->l, *av, *off, *lhs, *asgn;
		if (item && item->op == '{') {
			/* a braced row: store its scalars at r*dim + col */
			Node *in;
			int c = 0;
			lin = nrows * dim;
			for (in = item->l; in; in = in->r) {
				Node *e = in->l, *val;
				int col;
				if (e && e->op == 'd') {        /* [col] = val */
					col = e->r->u.n;
					val = e->l;
				} else {
					col = c;
					val = e;
				}
				av = mknode('V', 0, 0);
				strcpy(av->u.v, v);
				off = mknode('N', 0, 0);
				off->u.n = nrows * dim + col;
				lhs = mknode('@', mknode('+', av, off), 0);
				asgn = mknode('=', lhs, val);
				chain = chain ? mknode(',', chain, asgn) : asgn;
				c = col + 1;
			}
			nrows++;
			lin = nrows * dim;
		} else {
			/* brace-elided flat scalar: store at the next linear slot */
			av = mknode('V', 0, 0);
			strcpy(av->u.v, v);
			off = mknode('N', 0, 0);
			off->u.n = lin;
			lhs = mknode('@', mknode('+', av, off), 0);
			asgn = mknode('=', lhs, item);
			chain = chain ? mknode(',', chain, asgn) : asgn;
			lin++;
			if (lin > nrows * dim)
				nrows = (lin + dim - 1) / dim;
		}
	}
	if (rows > nrows)
		nrows = rows;
	*out_rows = nrows;

	if (zerofill) {
		Node *zhead = 0;
		int k, tot = nrows * dim;
		for (k = 0; k < tot; k++) {
			Node *av = mknode('V', 0, 0), *off = mknode('N', 0, 0);
			Node *lhs, *zero, *zasgn;
			strcpy(av->u.v, v);
			off->u.n = k;
			lhs = mknode('@', mknode('+', av, off), 0);
			zero = mknode('N', 0, 0);
			zero->u.n = 0;
			zasgn = mknode('=', lhs, zero);
			zhead = zhead ? mknode(',', zhead, zasgn) : zasgn;
		}
		if (zhead)
			chain = chain ? mknode(',', zhead, chain) : zhead;
	}
	return chain;
}

Node *
mkneg(Node *n)
{
	static Node *z;

	if (!z) {
		z = mknode('N', 0, 0);
		z->u.n = 0;
	}
	return mknode('-', z, n);
}

Stmt *
mkstmt(int t, void *p1, void *p2, void *p3)
{
	Stmt *s;

	s = alloc(sizeof *s);
	s->t = t;
	s->p1 = p1;
	s->p2 = p2;
	s->p3 = p3;
	return s;
}

Node *
param(char *v, unsigned ctyp, Node *pl)
{
	Node *n;

	if (ctyp == NIL)
		die("invalid void declaration");
	n = mknode(0, 0, pl);
	strcpy(n->u.v, v);
	/* A parameter shadows any file-scope binding of the same name (and a
	 * different-typed prior local); route through block_scope_decl so it is
	 * alpha-renamed rather than dying "double definition" in varadd (the
	 * §6a/§7b/§7d block-scope-shadow family).  Mutates n->u.v to the mangled
	 * name so the later varget/bind_param in ansi_func_proto resolve the
	 * renamed slot, and registers the rename so body uses of the source name
	 * resolve here too.  No-collision case returns the name unchanged. */
	block_scope_decl(n, ctyp, 0);
	varadd(n->u.v, 0, ctyp, 0);
	return n;
}

/* Abstract (unnamed) parameter in a prototype: `int f(const char *, int)`.
 * Synthesize a unique dummy name so the existing proto path (which
 * iterates n->u.v and calls varget) keeps working. */
Node *
abstract_param(unsigned ctyp, Node *pl)
{
	static int n;
	char buf[NString];

	/* `(void)` flows here as a single abstract param of type void;
	 * treat it as an empty parameter list rather than a real param. */
	if (ctyp == NIL)
		return pl;
	sprintf(buf, "__arg%d", n++);
	return param(buf, ctyp, pl);
}

/* `typedef struct tag alias;` — register the alias as a typedef of
 * the existing struct tag (looked up by name). */
void
typedef_struct_tag(char *tag, char *alias)
{
	int idx = structfind(tag);
	if (idx < 0)
		idx = structadd_forward(tag, 0);  /* forward typedef: body comes later */
	typhadd(alias, (idx << 3) + STRUCT_T);
}

/* Emit a file-scope integer initializer using parsed_type and
 * parsed_ident set by type_and_ident. */
void
emit_global_int_init(int value)
{
	char buf[64];
	if (parsed_type == NIL)
		die("invalid void declaration");
	if (nglo == NGlo)
		die("too many globals");
	sprintf(buf, "{ %c %d }", irtyp(parsed_type), value);
	ini[nglo] = alloc(strlen(buf) + 1);
	strcpy(ini[nglo], buf);
	strcpy(gloname[nglo], parsed_ident);
	varadd(parsed_ident, nglo++, parsed_type, 0);
}

/* `struct TAG { ... } NAME[N];` — emit a zero-filled global array of
 * the just-defined struct (curstruct).  Both the static and non-static
 * forms reduce to this. */
void
emit_struct_global_array(char *name, int count)
{
	int idx = curstruct;
	int total;
	char buf[64];
	unsigned styp;

	if (idx < 0)
		die("missing struct context");
	structfinish(idx);    /* §4g: tail-pad before the array stride is read */
	styp = (idx << 3) + STRUCT_T;
	curstruct = -1;
	total = SIZE(styp) * count;
	if (nglo == NGlo)
		die("too many globals");
	sprintf(buf, "align %d { z %d }", iralign(styp), total);
	ini[nglo] = alloc(strlen(buf) + 1);
	strcpy(ini[nglo], buf);
	strcpy(gloname[nglo], name);
	varadd(name, nglo++, IDIR(styp), 1);
	var_set_arraybytes(name, total);
}

/* Struct-array initializer collection.
 *
 *   struct TAG NAME[] = { 1, "a", 2, "b", ... };
 *   struct TAG NAME[] = { { 1, "a" }, { 2, "b" }, ... };
 *
 * Items arrive in source order; we cycle through the struct's members
 * to assign per-item QBE types when emitting the data block. */
struct CIVal {
	int  issym;          /* 1 = symbol-relative (address), 0 = integer */
	char sym[NString];   /* symbol name (no leading $) when issym */
	long off;            /* integer value, or addend when issym */
};

void cival_eval(Node *, struct CIVal *);

/* File-scope scalar initializer that folded to a symbol address
 * (+addend): `char **environ = __env;`, `int *p = &x;` — the scalar
 * sibling of the aggregate path's `$sym+off` items (§6a). */
void
emit_global_sym_init(char *sym, long off)
{
	char buf[NString + 32];
	if (parsed_type == NIL)
		die("invalid void declaration");
	if (nglo == NGlo)
		die("too many globals");
	if (off)
		sprintf(buf, "{ %c $%s+%ld }", irtyp(parsed_type), sym, off);
	else
		sprintf(buf, "{ %c $%s }", irtyp(parsed_type), sym);
	ini[nglo] = alloc(strlen(buf) + 1);
	strcpy(ini[nglo], buf);
	strcpy(gloname[nglo], parsed_ident);
	varadd(parsed_ident, nglo++, parsed_type, 0);
}

/* §9a: file-scope function-pointer VARIABLE definition.
 *   void (*v)(void);            is_static=0, init=0
 *   static int (*cmp)(int,int); is_static=1, init=0
 *   void (*v)(void) = foo;      init = the initializer expr
 * Mirrors the dcls / statement-scope fn-ptr-variable rules (which alloc a
 * stack slot) but emits a zero-initialized — or symbol-initialized — DATA
 * global, like the typed_decl_rest scalar globals, and records the fn-ptr
 * prototype id (varsetfpid) so an indirect call coerces its arguments.
 * Until now only the EXTERN and TYPEDEF file-scope fn-ptr forms existed, so
 * a plain file-scope fn-ptr definition was a parse error. */
void
emit_global_fnptr(char *name, unsigned base, Node *fptpar, Node *init, int is_static)
{
	unsigned fptr_type = IDIR(FUNC(base));
	char buf[NString + 32];
	int start = nglo;

	if (nglo == NGlo)
		die("too many globals");
	if (init) {
		struct CIVal v;
		cival_eval(init, &v);
		if (v.issym) {
			if (v.off)
				sprintf(buf, "{ %c $%s+%ld }", irtyp(fptr_type), v.sym, v.off);
			else
				sprintf(buf, "{ %c $%s }", irtyp(fptr_type), v.sym);
		} else {
			sprintf(buf, "{ %c %ld }", irtyp(fptr_type), v.off);
		}
	} else {
		emit_zero_init(buf, fptr_type);
	}
	ini[nglo] = alloc(strlen(buf) + 1);
	strcpy(ini[nglo], buf);
	strcpy(gloname[nglo], name);
	varadd(name, nglo++, fptr_type, 0);
	varsetfpid(name, fpproto_alloc(base, fptpar));
	if (is_static)
		glo_mark_static_range(start);
}

/* §9b: one declarator in a file-scope fn-ptr declaration.  These list nodes
 * are consumed ONLY by emit_global_fnptr_list and never reach codegen, so the
 * 'Q'/'Z' op tags are private bookkeeping:
 *   'Q': u.v = name, l = holder, r = next declarator in the comma chain
 *   'Z' holder: l = fptpar (param list), r = init expr (or 0) */
Node *
mk_fnptr_decl(char *name, Node *fptpar, Node *init)
{
	Node *holder = mknode('Z', fptpar, init);
	Node *n = mknode('Q', holder, 0);
	strcpy(n->u.v, name);
	return n;
}

/* Emit every declarator in a file-scope fn-ptr declaration, all sharing the
 * single return type `base` (C: the declaration's type applies to each
 * declarator — `int (*a)(int), (*b)(int);`). */
void
emit_global_fnptr_list(unsigned base, Node *list, int is_static)
{
	Node *it;

	for (it = list; it; it = it->r)
		emit_global_fnptr(it->u.v, base, it->l->l, it->l->r, is_static);
}

#define NSAI 4096
int  nsai = 0;
char sai_kind[NSAI];   /* 'N' = literal number, 'S' = string global idx,
                        * 'A' = address symbol (sai_sym[i] + sai_val[i]) */
long sai_val[NSAI];
char sai_sym[NSAI][NString];  /* symbol name for kind 'A' (no leading $) */

void
sai_clear(void)
{
	nsai = 0;
}

/* Designated array initializer `[index] = value`: zero-fill the scalar
 * data list up to `idx` so the next sai_add_* lands at position idx.
 * Supports the in-order contiguous form MicroPython uses for
 * `static const uint8_t t[] = { [SCOPE_MODULE] = …, [SCOPE_LAMBDA] = …, }`. */
void
sai_designate(int idx)
{
	if (idx < 0)
		die("negative designated array index");
	if (idx < nsai)
		die("out-of-order designated array initializer");
	while (nsai < idx) {
		if (nsai >= NSAI)
			die("too many struct-array init items");
		sai_kind[nsai] = 'N';
		sai_val[nsai++] = 0;
	}
}

/* Sized array initializer `T name[N] = { … }`: after the brace items are
 * collected, zero-fill the tail so the data block is exactly `count`
 * elements (C zero-fills missing trailing initializers). */
void
sai_pad_to_count(int count)
{
	if (nsai > count)
		die("too many initializers for array");
	while (nsai < count) {
		if (nsai >= NSAI)
			die("too many struct-array init items");
		sai_kind[nsai] = 'N';
		sai_val[nsai++] = 0;
	}
}

void
sai_add_num(long v)
{
	if (nsai >= NSAI)
		die("too many struct-array init items");
	sai_kind[nsai] = 'N';
	sai_val[nsai++] = v;
}

void
sai_add_str(int idx)
{
	if (nsai >= NSAI)
		die("too many struct-array init items");
	sai_kind[nsai] = 'S';
	sai_val[nsai++] = idx;
}

void
sai_add_sym(char *sym, long off)
{
	if (nsai >= NSAI)
		die("too many struct-array init items");
	sai_kind[nsai] = 'A';
	strcpy(sai_sym[nsai], sym);
	sai_val[nsai++] = off;
}

/* Emit a kind-'A' (address symbol) data item ` %c $sym[+off]` at `dst`,
 * returning the number of characters written. */
int
sai_emit_sym(char *dst, char ir, int i)
{
	if (sai_val[i])
		return sprintf(dst, " %c $%s+%ld", ir, sai_sym[i], sai_val[i]);
	return sprintf(dst, " %c $%s", ir, sai_sym[i]);
}

/* Add one brace-list item from a parsed expression node.  A string
 * literal (op 'S') is recorded as a pointer item; anything else is a
 * static-initializer constant folded by cival_eval, which yields either
 * a pure integer (NUM / enum-const / arithmetic) or a symbol+addend
 * (`&global`, `&arr[i]`, function name, casts thereof) — the latter
 * emitted as a relocatable `$sym+off` data item. */
void cival_eval(Node *, struct CIVal *);
void
sai_add_expr(Node *n)
{
	struct CIVal v;

	if (n && n->op == 'S') {
		sai_add_str(n->u.n);
		return;
	}
	cival_eval(n, &v);
	if (v.issym)
		sai_add_sym(v.sym, v.off);
	else
		sai_add_num(v.off);
}

/* Emit a global data block holding an array of string-literal /
 * integer pointer values from sai_*.  Used for both file-scope and
 * block-scope-static `T *ARR[] = { "s1", "s2", ... };`. */
void
emit_pointer_array_data(unsigned elemtyp, char *name)
{
	static char buf[16384];
	int buflen = 0;
	int i;
	char ir = irtyp(elemtyp);  /* 'l' for any pointer */

	buflen += sprintf(buf + buflen, "align 8 {");
	for (i = 0; i < nsai; i++) {
		if (i)
			buflen += sprintf(buf + buflen, ",");
		if (sai_kind[i] == 'S' && sai_val[i] != 0)
			buflen += sprintf(buf + buflen, " %c $glo%ld",
			                  ir, sai_val[i]);
		else if (sai_kind[i] == 'A')
			buflen += sai_emit_sym(buf + buflen, ir, i);
		else
			buflen += sprintf(buf + buflen, " %c %ld",
			                  ir, sai_val[i]);
	}
	buflen += sprintf(buf + buflen, " }");

	if (nglo == NGlo)
		die("too many globals");
	ini[nglo] = alloc(buflen + 1);
	strcpy(ini[nglo], buf);
	strcpy(gloname[nglo], name);
	varadd(name, nglo++, elemtyp, 1);
	sai_clear();
}

/* Emit a global data block holding an array of scalar (non-pointer,
 * non-struct) values from sai_*.  Used for `T NAME[] = { n, n, ... };`
 * where T is an integer/char type, e.g.
 *   static const uint8_t rule_act_table[] = { 0, 2, 4, ... };
 * Each item is emitted with the QBE data width that matches the element
 * type (b/h/w/l).  Registered as pointer-to-element with the array flag,
 * matching the sized `T NAME[N];` rule. */
void emit_struct_array_data(int sidx, char *name);

void
emit_scalar_array_data(unsigned elemtyp, char *name)
{
	static char buf[65536];
	int buflen = 0;
	int i;
	char ir = irtyp(elemtyp);

	buflen += sprintf(buf + buflen, "align %d {", iralign(elemtyp));
	for (i = 0; i < nsai; i++) {
		if (i)
			buflen += sprintf(buf + buflen, ",");
		if (sai_kind[i] == 'S')
			die("scalar array initializer cannot hold a string");
		if (sai_kind[i] == 'A')
			buflen += sai_emit_sym(buf + buflen, ir, i);
		else
			buflen += sprintf(buf + buflen, " %c %ld", ir, sai_val[i]);
	}
	buflen += sprintf(buf + buflen, " }");

	if (nglo == NGlo)
		die("too many globals");
	ini[nglo] = alloc(buflen + 1);
	strcpy(ini[nglo], buf);
	strcpy(gloname[nglo], name);
	varadd(name, nglo++, IDIR(elemtyp), 1);
	var_set_arraybytes(name, SIZE(elemtyp) * nsai);
	sai_clear();
}

/* Wrapper used by the dcls rule for `static T *NAME[] = {...};` /
 * `static T NAME[] = {...};`. */
void
emit_static_pointer_array(unsigned ptr_type, char *name)
{
	if (KIND(ptr_type) == PTR)
		emit_pointer_array_data(ptr_type, name);
	else if (KIND(ptr_type) == STRUCT_T)
		emit_struct_array_data(DREF(ptr_type), name);
	else
		emit_scalar_array_data(ptr_type, name);
}

/* Emit `data $NAME = align A { ... }` for a struct-array initializer.
 * Items in sai_* are walked round-robin against the struct's members,
 * each emitted with the QBE type that matches the member it fills. */
void
emit_struct_array_data(int sidx, char *name)
{
	static char buf[65536];
	int buflen = 0;
	int i, memidx, nmem;
	unsigned mctyp;
	char ir;

	nmem = structh[sidx].nmembers;
	if (nmem == 0)
		die("struct-array init: empty struct");
	if (nsai % nmem != 0)
		die("struct-array init: item count not a multiple of members");

	buflen += sprintf(buf + buflen, "align 8 {");
	memidx = 0;
	for (i = 0; i < nsai; i++) {
		mctyp = structh[sidx].members[memidx].ctyp;
		ir = irtyp(mctyp);
		if (i)
			buflen += sprintf(buf + buflen, ",");
		if (sai_kind[i] == 'S') {
			/* String literal - emit as pointer */
			if (sai_val[i] == 0)
				buflen += sprintf(buf + buflen, " %c 0", ir);
			else
				buflen += sprintf(buf + buflen, " %c $glo%ld",
				                  ir, sai_val[i]);
		} else if (sai_kind[i] == 'A') {
			buflen += sai_emit_sym(buf + buflen, ir, i);
		} else {
			buflen += sprintf(buf + buflen, " %c %ld", ir, sai_val[i]);
		}
		memidx++;
		if (memidx == nmem)
			memidx = 0;
	}
	buflen += sprintf(buf + buflen, " }");

	if (nglo == NGlo)
		die("too many globals");
	ini[nglo] = alloc(buflen + 1);
	strcpy(ini[nglo], buf);
	strcpy(gloname[nglo], name);
	{
		unsigned styp = (sidx << 3) + STRUCT_T;
		varadd(name, nglo++, IDIR(styp), 1);
		/* Element count is nsai items / members-per-struct; record
		 * the whole-array byte size for sizeof(table). */
		if (nmem > 0)
			var_set_arraybytes(name, SIZE(styp) * (nsai / nmem));
	}
	sai_clear();
}

/* ===== file-scope aggregate / designated initializers (§1b) =====
 *
 * Handles `T NAME = { ... };` for struct/union variables, including
 * designated `.field = value`, nested braces (`.base = { ... }`),
 * array members (`.slots = { a, b, c }`), and casts inside values.
 *
 * The initializer is parsed into a generic Node tree (gaggr/gilist/
 * gitem grammar): a brace becomes a '{' node whose `l` is a chain of
 * list nodes (op 0, l=item, r=next); a sequential item is the value
 * node itself; a designated item is a 'D' node (l=value, r=IDENT) or
 * a 'd' node (l=value, r=index-expr).  We walk that tree against the
 * target type, constant-folding each scalar to an integer or a
 * symbol+addend, and emit a QBE `data` block whose byte layout matches
 * the struct's member offsets (inserting `z N` zero-fill for any gap a
 * designator skips and a trailing `z` to the full type size). */


/* Resolve a static lvalue (global var, `.`/`->` member access, or array
 * index) to a symbol + byte offset, returning its C type.  Sets *o to
 * the symbol-relative address.  Used by cival_addr for `&lvalue`. */
unsigned
cival_lval(Node *n, struct CIVal *o)
{
	if (n->op == 'V') {
		Symb *sv = varget(n->u.v);
		if (!sv || sv->t == Con)
			die("unsupported address-of base in initializer");
		o->issym = 1;
		o->sym[0] = 0;
		o->off = 0;
		strcpy(o->sym, n->u.v);
		return sv->ctyp;
	}
	if (n->op == '.') {
		/* &agg.member [.member...] → $base + Σ member offsets.  `->`
		 * desugars to `(*p).m`; that involves a pointer deref which
		 * isn't a static address, so it falls through to die below. */
		unsigned bt = cival_lval(n->l, o);
		int sidx, mi;
		if (KIND(bt) != STRUCT_T && KIND(bt) != UNION_T)
			die("member access on non-struct in initializer");
		sidx = DREF(bt);
		mi = structfindmember(sidx, n->r->u.v);
		if (mi < 0)
			die("unknown member in static address-of");
		o->off += structh[sidx].members[mi].offset;
		return structh[sidx].members[mi].ctyp;
	}
	if (n->op == '@' && n->l && n->l->op == '+') {
		/* &arr[i]  ==  arr + i  (i counted in elements) */
		unsigned bt = cival_lval(n->l->l, o);
		struct CIVal idx;
		cival_eval(n->l->r, &idx);
		if (idx.issym)
			die("non-constant index in static initializer");
		o->off += idx.off * (long)SIZE(DREF(bt));
		return DREF(bt);
	}
	die("unsupported address-of in static initializer");
	return NIL;
}

/* Evaluate &lvalue → symbol [+ byte offset]. */
void
cival_addr(Node *n, struct CIVal *o)
{
	cival_lval(n, o);
}

/* Constant-fold an initializer value to an integer or symbol+addend. */
void
cival_eval(Node *n, struct CIVal *o)
{
	struct CIVal a, b;
	Symb *sv;

	if (!n)
		die("null initializer expression");
	o->issym = 0;
	o->sym[0] = 0;
	o->off = 0;
	switch (n->op) {
	case 'N':
		o->off = n->u.n;
		return;
	case 'S':
		o->issym = 1;
		sprintf(o->sym, "glo%d", n->u.n);
		return;
	case 'V':
		sv = varget(n->u.v);
		if (!sv)
			die("undefined identifier in static initializer");
		if (sv->t == Con) {            /* enum constant */
			o->off = sv->u.n;
			return;
		}
		if (sv->t == Var)
			die("non-constant local in static initializer");
		/* global / extern / function name: decays to its address */
		o->issym = 1;
		strcpy(o->sym, n->u.v);
		return;
	case 'A':                              /* &lvalue */
		cival_addr(n->l, o);
		return;
	case 'K':                              /* (type)expr cast: value-preserving */
		cival_eval(n->l, o);
		return;
	case '+':
		cival_eval(n->l, &a);
		cival_eval(n->r, &b);
		if (a.issym && b.issym)
			die("two symbols added in static initializer");
		o->issym = a.issym || b.issym;
		strcpy(o->sym, a.issym ? a.sym : b.sym);
		o->off = a.off + b.off;
		return;
	case '-':
		if (!n->r) {                   /* unary minus */
			cival_eval(n->l, &a);
			if (a.issym)
				die("negate symbol in static initializer");
			o->off = -a.off;
			return;
		}
		cival_eval(n->l, &a);
		cival_eval(n->r, &b);
		if (b.issym)
			die("subtract symbol in static initializer");
		o->issym = a.issym;
		strcpy(o->sym, a.sym);
		o->off = a.off - b.off;
		return;
	default:
		/* *, /, %, &, |, ^, <<, >>, ~ : pure integer fold */
		o->off = const_eval(n);
		return;
	}
}

/* Item-separator helper: a `,` between QBE data items (none before the
 * first).  Every leaf (scalar or zero-fill) calls this first. */
void
agg_sep(char *buf, int *bl, int *first)
{
	if (*first)
		*first = 0;
	else
		*bl += sprintf(buf + *bl, ",");
}

void
agg_zfill(int bytes, char *buf, int *bl, int *first)
{
	if (bytes <= 0)
		return;
	agg_sep(buf, bl, first);
	*bl += sprintf(buf + *bl, " z %d", bytes);
}

/* Peel redundant braces around a scalar value: `.x = { 5 }`. */
Node *
agg_unwrap_scalar(Node *init)
{
	while (init && init->op == '{') {
		Node *list = init->l;
		init = list ? list->l : 0;     /* first list item */
		if (init && (init->op == 'D' || init->op == 'd'))
			init = init->l;        /* its value */
	}
	return init;
}

/* Evaluate a compile-time-constant floating expression to a host double.
 * Handles float/int literals, value-preserving casts, the four arithmetic
 * ops, and unary minus — including the `0 - x` form mkneg desugars `-x` into
 * (so a negative float initializer like `float n = -0.5f;` folds correctly).
 * Integer-only subexpressions fall back to const_eval.  Used only for static
 * float initializers (minic runs on the host, so host double is fine). */
double
const_eval_double(Node *n)
{
	double l, r;

	if (!n) die("null expression in float constant");
	switch (n->op) {
	case 'F':                      /* float literal */
		return strtod(n->u.v, 0);
	case 'K':                      /* (type)expr cast: value-preserving */
		return const_eval_double(n->l);
	case '+':
		return const_eval_double(n->l) + const_eval_double(n->r);
	case '*':
		return const_eval_double(n->l) * const_eval_double(n->r);
	case '/':
		l = const_eval_double(n->l);
		r = const_eval_double(n->r);
		if (r == 0.0) die("division by zero in float constant");
		return l / r;
	case '-':
		if (!n->r)             /* unary minus */
			return -const_eval_double(n->l);
		return const_eval_double(n->l) - const_eval_double(n->r);
	default:
		/* Integer constant promoted to float, e.g. `float x = 5;`. */
		return (double)const_eval(n);
	}
}

/* Produce the QBE float-literal text (the part after the `s_` prefix) for a
 * compile-time-constant single-precision initializer.  `%.17g` round-trips a
 * host double exactly; QBE's `s_` lexer (fscanf "_%f") accepts the sign,
 * decimal, and exponent forms this can produce, then rounds to binary32. */
void
cival_float_text(Node *n, char *out)
{
	sprintf(out, "%.17g", const_eval_double(n));
}

/* `T NAME = <const float expr>;` at file scope. */
void
emit_global_float_init(Node *n)
{
	char buf[96], ftext[64];

	if (parsed_type == NIL)
		die("invalid void declaration");
	if (nglo == NGlo)
		die("too many globals");
	cival_float_text(n, ftext);
	sprintf(buf, "{ s s_%s }", ftext);
	ini[nglo] = alloc(strlen(buf) + 1);
	strcpy(ini[nglo], buf);
	strcpy(gloname[nglo], parsed_ident);
	varadd(parsed_ident, nglo++, parsed_type, 0);
}

void
agg_emit_scalar(unsigned ctyp, Node *init, char *buf, int *bl, int *first)
{
	struct CIVal v;
	char ir = irtyp(ctyp);

	agg_sep(buf, bl, first);
	init = agg_unwrap_scalar(init);
	if (!init) {
		*bl += sprintf(buf + *bl, " %c 0", ir);
		return;
	}
	if (ISFLOAT(ctyp)) {
		/* Single-precision member: emit `s s_<value>` (QBE rounds to
		 * binary32 and the i8086 data path lays it out as 4 bytes). */
		char ftext[64];
		cival_float_text(init, ftext);
		*bl += sprintf(buf + *bl, " s s_%s", ftext);
		return;
	}
	cival_eval(init, &v);
	if (v.issym) {
		if (v.off)
			*bl += sprintf(buf + *bl, " %c $%s+%ld", ir, v.sym, v.off);
		else
			*bl += sprintf(buf + *bl, " %c $%s", ir, v.sym);
	} else {
		*bl += sprintf(buf + *bl, " %c %ld", ir, v.off);
	}
}

void agg_emit_value(unsigned, int, Node *, char *, int *, int *);

/* Emit an array member: `cnt` elements of element-type `elemty`. */
void
agg_emit_array(unsigned elemty, int cnt, Node *init, char *buf, int *bl, int *first)
{
	int elemsz = SIZE(elemty);
	int n = 0;
	Node *ln;

	Node **slots;
	int pos;

	if (!init || init->op != '{')
		die("array member requires a braced initializer");
	/* Buffer values into index-addressed slots so designated items may
	 * appear in any order (e.g. MicroPython's `[OP_ADD]=...` jump tables,
	 * whose enum indices aren't ascending).  A positional item lands at
	 * the running cursor; a `[k]=v` designator sets the cursor to k
	 * (C99 6.7.8) — both then advance it.  Unfilled slots emit as zero. */
	slots = alloc(cnt * sizeof *slots);
	for (n = 0; n < cnt; n++)
		slots[n] = 0;
	pos = 0;
	for (ln = init->l; ln; ln = ln->r) {
		Node *item = ln->l;
		Node *val = item;
		if (item->op == 'd') {         /* [k] = v */
			pos = const_eval(item->r);
			val = item->l;
		} else if (item->op == 'D') {
			die("field designator in array initializer");
		}
		if (pos < 0 || pos >= cnt)
			die("array designator out of bounds");
		slots[pos++] = val;
	}
	for (n = 0; n < cnt; ) {
		if (slots[n]) {
			agg_emit_value(elemty, 0, slots[n], buf, bl, first);
			n++;
		} else {
			/* Coalesce a run of unfilled slots into one zero-fill, so
			 * fully- or in-order-initialized arrays emit byte-for-byte
			 * as before. */
			int run = 0;
			while (n < cnt && !slots[n]) {
				run++;
				n++;
			}
			agg_zfill(run * elemsz, buf, bl, first);
		}
	}
}

/* Number of elements a braced initializer occupies, honouring `[k]=v`
 * designators (C99 6.7.8: a designator sets the cursor, so the count is the
 * highest index reached + 1).  Used to size a flexible-array-member init,
 * whose length is implied by its initializer rather than declared. */
int
agg_brace_count(Node *agg)
{
	int pos = 0, maxpos = 0;
	Node *ln;

	if (!agg || agg->op != '{')
		return 0;
	for (ln = agg->l; ln; ln = ln->r) {
		Node *item = ln->l;
		if (item->op == 'd')
			pos = const_eval(item->r);
		pos++;
		if (pos > maxpos)
			maxpos = pos;
	}
	return maxpos;
}

void
agg_emit_struct(int sidx, int isunion, Node *agg, char *buf, int *bl, int *first)
{
	int cursor = 0, memidx = 0;
	int structsize = structh[sidx].size;
	int nmembers = structh[sidx].nmembers;
	Node **memval;
	Node *ln;
	int mi;

	if (!agg || agg->op != '{')
		die("struct/union initializer must be braced");

	/* Pass 1: bind each initializer item to a member slot following C99
	 * cursor semantics (a positional item lands at the running cursor; a
	 * `.field =` designator sets the cursor to that member — both then
	 * advance it).  Buffering into member-indexed slots lets designated
	 * items appear in any order (e.g. MicroPython's mp_obj_type_t inits,
	 * whose `.slot_index_*` designators aren't in offset order).  Pass 2
	 * walks members in declaration order (= ascending offset), so the
	 * emitted layout is correct regardless of item order, and a
	 * fully/in-order-initialized struct emits byte-for-byte as before. */
	memval = alloc(nmembers * sizeof *memval);
	for (mi = 0; mi < nmembers; mi++)
		memval[mi] = 0;
	for (ln = agg->l; ln; ln = ln->r) {
		Node *item = ln->l;
		Node *val;

		if (item->op == 'D') {         /* .field = val */
			int k, found = -1;
			for (k = 0; k < nmembers; k++)
				if (strcmp(structh[sidx].members[k].name,
				           item->r->u.v) == 0) {
					found = k;
					break;
				}
			if (found < 0)
				die("unknown member in designated initializer");
			memidx = found;
			val = item->l;
		} else if (item->op == 'd') {
			die("array designator in struct initializer");
		} else {
			val = item;
		}
		if (memidx >= nmembers)
			die("too many initializers for struct");
		memval[memidx] = val;
		memidx++;
		if (isunion)
			break;                 /* one initialized member */
	}

	/* Pass 2: emit members in declaration order.  Uninitialized members
	 * (and bitfield units with no initialized field) are skipped so they
	 * fold into the next member's gap-fill (or the trailing zero-fill),
	 * coalescing exactly as the old single-pass emitter did. */
	for (mi = 0; mi < nmembers; ) {
		struct Member *m = &structh[sidx].members[mi];
		int msize;

		if (m->bitwidth) {
			/* Pack a run of bitfield members that share one storage
			 * unit (same m->offset) into a single scalar data item. */
			unsigned bfctyp = m->ctyp;
			int bfunit = SIZE(bfctyp);
			int bfbase = m->offset;
			unsigned long accum = 0;
			unsigned long unitmask =
			    bfunit >= 8 ? ~0UL : ((1UL << (bfunit * 8)) - 1);
			int j, any = 0;

			for (j = mi; j < nmembers; j++) {
				struct Member *bm = &structh[sidx].members[j];
				if (!bm->bitwidth || bm->offset != bfbase)
					break;
				if (memval[j])
					any = 1;
			}
			if (!any) {            /* whole unit unset — fold into gap */
				mi = j;
				continue;
			}
			agg_zfill(bfbase - cursor, buf, bl, first);
			cursor = bfbase;
			for (j = mi; j < nmembers; j++) {
				struct Member *bm = &structh[sidx].members[j];
				unsigned long fv, fmask;
				Node *fval;

				if (!bm->bitwidth || bm->offset != bfbase)
					break;
				fval = agg_unwrap_scalar(memval[j]);
				fv = fval ? (unsigned long)const_eval(fval) : 0;
				fmask = bm->bitwidth >= 64 ? ~0UL
				    : ((1UL << bm->bitwidth) - 1);
				accum |= (fv & fmask) << bm->bitoffset;
			}
			agg_sep(buf, bl, first);
			*bl += sprintf(buf + *bl, " %c %lu",
			    irtyp(bfctyp), accum & unitmask);
			cursor += bfunit;
			mi = j;
			continue;
		}

		if (!memval[mi]) {             /* unset — fold into next gap */
			mi++;
			continue;
		}

		agg_zfill(m->offset - cursor, buf, bl, first);
		cursor = m->offset;
		if (m->isflex) {
			/* Flexible array member `T x[];` — its length is implied by
			 * the initializer's element count, not declared.  Emit ALL
			 * brace elements (a scalar emit would drop all but the
			 * first); the member contributes 0 to structsize, so the
			 * extra bytes legitimately push cursor past structsize. */
			int nflex = agg_brace_count(memval[mi]);
			agg_emit_array(m->ctyp, nflex, memval[mi], buf, bl, first);
			msize = SIZE(m->ctyp) * nflex;
		} else {
			agg_emit_value(m->ctyp, m->count, memval[mi], buf, bl, first);
			msize = m->count ? SIZE(m->ctyp) * m->count : SIZE(m->ctyp);
		}
		cursor += msize;
		mi++;
	}
	if (cursor < structsize)
		agg_zfill(structsize - cursor, buf, bl, first);
}

/* Emit `init` as a value occupying the footprint of (ctyp, count). */
void
agg_emit_value(unsigned ctyp, int cnt, Node *init, char *buf, int *bl, int *first)
{
	if (cnt > 0)
		agg_emit_array(ctyp, cnt, init, buf, bl, first);
	else if (KIND(ctyp) == STRUCT_T || KIND(ctyp) == UNION_T)
		agg_emit_struct(DREF(ctyp), KIND(ctyp) == UNION_T,
		                init, buf, bl, first);
	else
		agg_emit_scalar(ctyp, init, buf, bl, first);
}

/* `T NAME = { ... };` at file scope.  Emits one `data $NAME = ...`. */
void
emit_global_aggregate(unsigned ctyp, char *name, Node *agg)
{
	static char buf[65536];
	int bl = 0, first = 1, idx;

	if (ctyp == NIL)
		die("invalid void declaration");
	bl = sprintf(buf, "align %d {", iralign(ctyp));
	if (KIND(ctyp) == STRUCT_T || KIND(ctyp) == UNION_T)
		agg_emit_struct(DREF(ctyp), KIND(ctyp) == UNION_T,
		                agg, buf, &bl, &first);
	else
		agg_emit_scalar(ctyp, agg, buf, &bl, &first);
	if (first)                             /* nothing emitted: keep block legal */
		bl += sprintf(buf + bl, " z 1");
	bl += sprintf(buf + bl, " }");
	/* Reuse an earlier tentative slot if this supersedes a `static const
	 * T name;` declaration; otherwise allocate a fresh global slot. */
	idx = glo_redef_index(name);
	if (idx < 0) {
		if (nglo == NGlo)
			die("too many globals");
		idx = nglo++;
		strcpy(gloname[idx], name);
		varadd(name, idx, ctyp, 0);
	}
	ini[idx] = alloc(bl + 1);
	strcpy(ini[idx], buf);
}

/* Decoded byte length (including the NUL terminator) of the string
 * literal stored at global index `idx`.  ini[idx] holds the QBE data
 * text `{ b "CONTENT", b 0 }`, where CONTENT carries C escape sequences
 * verbatim; count each escape as one byte so sizeof(char_array) is
 * correct.  Handles \xHH, \NNN octal, and single-char escapes. */
int
strlit_bytelen(int idx)
{
	char *s = ini[idx];
	int len = 0;
	int contentlen;

	s += 5;                 /* skip the `{ b "` prefix */
	contentlen = (int)strlen(s);
	contentlen -= 8;        /* drop the `", b 0 }` suffix */
	if (contentlen < 0)
		contentlen = 0;
	while (contentlen > 0) {
		if (*s == '\\' && contentlen > 1) {
			s++; contentlen--;          /* the escape char */
			if (*s == 'x') {
				s++; contentlen--;
				while (contentlen > 0 &&
				    ((*s >= '0' && *s <= '9') ||
				     (*s >= 'a' && *s <= 'f') ||
				     (*s >= 'A' && *s <= 'F'))) {
					s++; contentlen--;
				}
			} else if (*s >= '0' && *s <= '7') {
				int k = 1;
				s++; contentlen--;
				while (contentlen > 0 && k < 3 &&
				    *s >= '0' && *s <= '7') {
					s++; contentlen--; k++;
				}
			} else {
				s++; contentlen--;      /* one-char escape */
			}
		} else {
			s++; contentlen--;
		}
		len++;
	}
	return len + 1;         /* + NUL terminator */
}

/* `T NAME[] = "string";` — emit the literal's bytes as a char-array data
 * block (not a pointer to the string).  `static_local` routes the data
 * into a mangled file-scope global. */
void
emit_string_array(unsigned elemtyp, char *name, int str_idx, int static_local)
{
	int total = strlit_bytelen(str_idx);

	if (static_local) {
		emit_static_local(name, IDIR(elemtyp), 1, ini[str_idx]);
	} else {
		if (nglo == NGlo)
			die("too many globals");
		ini[nglo] = ini[str_idx];
		strcpy(gloname[nglo], name);
		varadd(name, nglo++, IDIR(elemtyp), 1);
	}
	var_set_arraybytes(name, total);
}

/* `T NAME[N] = "string";` — like emit_string_array but with an EXPLICIT
 * element count N (the `[expr]` form).  The literal's bytes (incl. its NUL)
 * occupy the front of the array and the remaining `N*sizeof(T) - natural`
 * bytes are zero-filled, matching C char-array string initialization.  The
 * QBE data block `{ b "...", b 0 }` is reused verbatim when N fits exactly;
 * otherwise a `, z PAD` zero-fill is spliced before the closing brace.  A
 * declared size SMALLER than the natural length (the exact-fit drop-NUL edge,
 * `char a[3]="abc"`) is unsupported (dies clearly) — no consumer. */
void
emit_string_array_sized(unsigned elemtyp, char *name, int str_idx, long count,
    int static_local)
{
	static char buf[65536];
	int natural;
	long total, pad;
	char *blk, *brace;

	if (elemtyp == NIL)
		die("invalid void array");
	natural = strlit_bytelen(str_idx);
	total = count * (long)SIZE(elemtyp);
	pad = total - natural;
	if (pad < 0)
		die("string initializer too long for array");
	if (pad == 0) {
		blk = ini[str_idx];
	} else {
		strcpy(buf, ini[str_idx]);
		brace = strrchr(buf, '}');
		if (!brace)
			die("malformed string literal data block");
		sprintf(brace, ", z %ld }", pad);
		blk = alloc(strlen(buf) + 1);
		strcpy(blk, buf);
	}
	if (static_local) {
		emit_static_local(name, IDIR(elemtyp), 1, blk);
	} else {
		if (nglo == NGlo)
			die("too many globals");
		ini[nglo] = blk;
		strcpy(gloname[nglo], name);
		varadd(name, nglo++, IDIR(elemtyp), 1);
	}
	var_set_arraybytes(name, total);
}

/* `T NAME[] = { ... };` / `T NAME[N] = { ... };` at file scope, routed
 * through the generic aggregate machinery (agg_emit_array) so each
 * element may itself be a designated struct, a nested array, or a
 * scalar/pointer.  `count` is the declared element count, or -1 for an
 * unsized `[]` (inferred from the brace list, honouring `[k]=`
 * designators).  Output is byte-identical to the older sai_* round-robin
 * path for the plain fully-specified cases (minic never pads structs),
 * but additionally supports `{ {.f=v}, {.f=v} }` per-element designators.
 * The var-type registration matches the legacy per-kind rules: pointer
 * element arrays register the element (pointer) type itself; scalar and
 * struct element arrays register IDIR(elemtyp). */
/* Build the `align N { ... }` QBE data-block text for an array
 * initializer into `buf`; returns the total array byte size.  `count`
 * is the declared element count, or -1 to infer it from the brace list
 * (honouring `[k]=` designators). */
long
build_array_init(unsigned elemtyp, long count, Node *agg, char *buf)
{
	int bl, first = 1;
	int align;

	if (elemtyp == NIL)
		die("invalid void array");
	if (!agg || agg->op != '{')
		die("array initializer must be braced");
	if (count < 0) {
		long pos = 0, maxpos = 0;
		Node *ln;
		for (ln = agg->l; ln; ln = ln->r) {
			Node *item = ln->l;
			if (item->op == 'd')
				pos = const_eval(item->r);
			pos++;
			if (pos > maxpos)
				maxpos = pos;
		}
		count = maxpos;
	}
	if (KIND(elemtyp) == PTR || KIND(elemtyp) == STRUCT_T
	    || KIND(elemtyp) == UNION_T)
		align = 8;
	else
		align = iralign(elemtyp);
	bl = sprintf(buf, "align %d {", align);
	agg_emit_array(elemtyp, count, agg, buf, &bl, &first);
	if (first)
		bl += sprintf(buf + bl, " z 1");
	bl += sprintf(buf + bl, " }");
	return (long)SIZE(elemtyp) * count;
}

/* Var-type to register for an array of `elemtyp`.  A C array of T decays to
 * T* (pointer-to-element), so the variable's value type is always
 * IDIR(elemtyp) — INCLUDING pointer-element arrays (`T *arr[]` decays to
 * `T **`).  The earlier pointer-element special case (register `elemtyp`
 * itself) was wrong: it made `arr[i]` scale subscripts by sizeof(*T) instead
 * of sizeof(T*), so e.g. an array of `mp_obj_type_t *` indexed at runtime
 * read 20-byte-strided garbage (MicroPython's mp_obj_get_type `types[]`). */
unsigned
array_vartyp(unsigned elemtyp)
{
	return IDIR(elemtyp);
}

void
emit_global_array(unsigned elemtyp, char *name, long count, Node *agg)
{
	static char buf[65536];
	long total;

	if (nglo == NGlo)
		die("too many globals");
	total = build_array_init(elemtyp, count, agg, buf);
	ini[nglo] = alloc(strlen(buf) + 1);
	strcpy(ini[nglo], buf);
	strcpy(gloname[nglo], name);
	maybe_mark_huge_global(nglo, name, total);
	varadd(name, nglo++, array_vartyp(elemtyp), 1);
	var_set_arraybytes(name, total);
}

/* Function-local `static T NAME[...] = { ... };` — same aggregate
 * machinery as emit_global_array, but the data lands in a mangled
 * file-scope global (persistent across calls) via emit_static_local. */
void
emit_static_array(unsigned elemtyp, char *name, long count, Node *agg)
{
	static char buf[65536];
	long total;

	total = build_array_init(elemtyp, count, agg, buf);
	emit_static_local(name, array_vartyp(elemtyp), 1, buf);
	var_set_arraybytes(name, total);
	maybe_mark_huge_global(nglo - 1, gloname[nglo - 1], total);
}

/* Apply the K&R parameter base type to every name in kr_namelist.
 * node->op encodes the declarator shape:
 *   0   plain IDENT
 *   'P' *IDENT       (one extra pointer level)
 *   'A' IDENT[...]    (array, decays to pointer)
 *   'X' *IDENT[...]   (two extra pointer levels: char *argv[]) */
void
kr_apply_types(unsigned base, Node *names)
{
	Node *n;
	unsigned t;
	for (n = names; n; n = n->r) {
		if (n->op == 'X')
			t = IDIR(IDIR(base));
		else if (n->op == 'A' || n->op == 'P')
			t = IDIR(base);
		else
			t = base;
		varadd(n->u.v, 0, t, 0);
	}
}

/* Emit a local declaration with initializer (used by static-with-init). */
void
emit_local_init(unsigned ctyp, Node *ident, Node *initexpr)
{
	int s;
	char *v;
	Node *init_node;

	if (ctyp == NIL)
		die("invalid void declaration");
	v = ident->u.v;
	s = SIZE(ctyp);
	varadd(v, 0, ctyp, 0);
	emit_local_alloc(v, ALLOC_T(), iralign(ctyp), s);
	if (initexpr) {
		init_node = mknode('=', ident, initexpr);
		expr(init_node);
	}
}

/* `static T name = init;` inside a function.  Lower constant shapes
 * (integer literal, negated literal, string literal) directly into the
 * mangled file-scope data global so the variable's address is stable
 * across calls and the value persists.  Anything else falls back to
 * the historical alloc-on-stack + runtime-init path; that path doesn't
 * give true `static` semantics, but it preserves behaviour for code
 * that only reads the variable after the (re)init runs. */
void
emit_static_local_init(unsigned ctyp, Node *ident, Node *initexpr)
{
	char buf[64];
	if (ctyp == NIL)
		die("invalid void declaration");
	if (initexpr->op == 'S') {
		sprintf(buf, "{ %c $glo%d }", irtyp(ctyp), initexpr->u.n);
		emit_static_local(ident->u.v, ctyp, 0, buf);
	} else if (initexpr->op == 'N') {
		sprintf(buf, "{ %c %d }", irtyp(ctyp), initexpr->u.n);
		emit_static_local(ident->u.v, ctyp, 0, buf);
	} else if (initexpr->op == '-' && initexpr->l &&
	           initexpr->l->op == 'N' && !initexpr->r) {
		sprintf(buf, "{ %c %d }", irtyp(ctyp), -initexpr->l->u.n);
		emit_static_local(ident->u.v, ctyp, 0, buf);
	} else {
		emit_local_init(ctyp, ident, initexpr);
	}
}

/* Walk a chain of init_decl Nodes (op='I', u.v=name, l=initexpr or 0)
 * and emit a local alloc + optional store for each. */
void
emit_local_init_list(unsigned ctyp, Node *list)
{
	Node *n;
	Node id;
	for (n = list; n; n = n->r) {
		id.op = 'V';
		id.l = id.r = 0;
		strcpy(id.u.v, n->u.v);
		emit_local_init(ctyp, &id, n->l);
	}
}

/* Build an IDENT node with the given declarator-kind tag in op:
 *   0   = plain IDENT (uses base type)
 *   'P' = *IDENT       (one extra pointer level)
 *   'A' = IDENT[]       (array; decays to pointer)
 *   'F' = IDENT()       (function returning base)
 *   'G' = *IDENT()      (function returning pointer to base)
 *   'H' = *IDENT(par1)  (ANSI prototype returning pointer to base;
 *                        the param list is stashed on n->l so the
 *                        extern walks can fnproto_record it) */
Node *
kr_name_node(char *name, char op)
{
	Node *n = mknode(op, 0, 0);
	strcpy(n->u.v, name);
	return n;
}

/* Node tag 'B' = sized array declarator IDENT[NUM].  The size is
 * stashed as a NUM child on n->l so n->u.v can keep the name. */
Node *
kr_array_node(char *name, int size)
{
	Node *sz = mknode('N', 0, 0);
	sz->u.n = size;
	Node *n = mknode('B', sz, 0);
	strcpy(n->u.v, name);
	return n;
}

/* Append an `IDENT = init` assignment for declarator name `v` to a
 * comma-chain (the mk_local_array_init shape).  The chain is returned
 * to the caller instead of being expr()'d here so a stmt-context
 * declaration's initializer runs in lexical/control-flow order — a
 * direct expr() at parse time lands in the function entry block, so a
 * decl inside a loop body would init ONCE instead of per iteration
 * ([[minic-decl-init-hoisting]], multi-declarator form). */
static Node *
multi_decl_chain_init(Node *chain, char *v, Node *init)
{
	Node *id, *asgn;

	id = mknode('V', 0, 0);
	strcpy(id->u.v, v);
	asgn = mknode('=', id, init);
	return chain ? mknode(',', chain, asgn) : asgn;
}

/* Same as emit_local_multi_decl but every declarator (including the
 * first) is in `list`.  Used when the first declarator is decorated
 * (`[N]`, `*`, `()`, etc.).  Returns the deferred initializer chain
 * (or 0); the caller decides placement. */
Node *
emit_local_multi_decl_full(unsigned base, Node *list)
{
	Node *n, *chain;
	unsigned t;
	char *v;

	if (base == NIL)
		die("invalid void declaration");
	chain = 0;
	for (n = list; n; n = n->r) {
		v = n->u.v;
		if (n->op == 'F') {
			varadd(v, 1, FUNC(base), 0);
			continue;
		}
		if (n->op == 'G') {
			/* `*ident()` — uniform-* peeling: when the first declarator
			 * absorbed `*` into base, subsequent items already match it.
			 * Treat as `FUNC(base)` like a plain 'F'. */
			varadd(v, 1, FUNC(base), 0);
			continue;
		}
		if (n->op == 'B') {
			int count = n->l->u.n;
			unsigned elem = (KIND(base) == PTR) ? DREF(base) : base;
			/* Array-of-array-typedef declarator in a multi-decl
			 * (`jmp_buf a[2], b[2];`): when the shared base is an array
			 * typedef (g_td_arraydim = inner dim D > 0), the element is
			 * itself a D-wide array, so this declarator's slot is
			 * count*D*sizeof(elem) and the var carries aoa_dim=D so a
			 * one-level subscript yields a row address (§7e mkidx).  For a
			 * plain element (D==0) this is byte-identical to the old path. */
			int aoa = g_td_arraydim;
			int total = count * SIZE(elem) * (aoa > 0 ? aoa : 1);
			/* Inner-block shadow rename (see emit_local_multi_decl). */
			v = block_scope_decl(n, IDIR(elem), 1);
			varadd(v, 0, IDIR(elem), 1);
			fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign(elem), total);
			if (aoa > 0)
				var_set_aoa_dim(v, aoa);
			continue;
		}
		if (n->op == 0 && g_td_arraydim > 0) {
			/* Array-typedef INSTANCE in a multi-decl (`jmp_buf a, b;`):
			 * the shared base was reduced to the element type, so this
			 * plain declarator is the whole D-wide array — size it
			 * D*sizeof(elem) and register IDIR(elem) array so it decays
			 * to its address (not a scalar element load).  No aoa_dim:
			 * it is a plain array typedef instance, not an aoa. */
			unsigned elem = g_td_arrayelem;
			int total = SIZE(elem) * g_td_arraydim;
			v = block_scope_decl(n, IDIR(elem), 1);
			varadd(v, 0, IDIR(elem), 1);
			fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign(elem), total);
			continue;
		}
		t = (n->op == 'P' || n->op == 'A') ? IDIR(base) : base;
		/* Route through block_scope_decl so a multi-declarator local that
		 * shadows a global/extern/function/enum or a different-typed
		 * outer local is alpha-renamed instead of dying "double
		 * definition" in varadd (single-decl already does this). */
		v = block_scope_decl(n, t, n->op == 'A' ? 1 : 0);
		varadd(v, 0, t, n->op == 'A' ? 1 : 0);
		fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign(t), SIZE(t));
		if ((KIND(t) == STRUCT_T || KIND(t) == UNION_T) && struct_has_bitfield(DREF(t)))
			emit_zero_local(v, SIZE(t));
		if ((n->op == 0 || n->op == 'P') && n->l)
			chain = multi_decl_chain_init(chain, v, n->l);
	}
	return chain;
}

/* Emit a multi-name local declaration: `type IDENT, ext_decllist;`.
 * Each declarator carries its own decoration in `op`.  Used for the
 * many K&R patterns such as `char *s1, *s2;` and the mixed
 * `char *initstr, *getenv();` (proto + var).  Allocs are emitted at
 * parse time (entry block, QBE convention); initializers are returned
 * as a deferred comma-chain (or 0) — the dcls-context caller expr()s
 * it immediately (entry == lexical order there), the stmt-context
 * caller wraps it in mkstmt(Expr,…) so it runs in control-flow order. */
Node *
emit_local_multi_decl(unsigned base, Node *firstnode, Node *rest)
{
	int s;
	Node *n, *chain;
	unsigned t;
	char *v;

	if (base == NIL)
		die("invalid void declaration");
	chain = 0;
	s = SIZE(base);
	/* First declarator is an array-typedef INSTANCE (`jmp_buf a, b;`):
	 * size the whole D-wide array and register IDIR(elem) array so it
	 * decays (see emit_global_arr_instance / emit_local_multi_decl_full).
	 * firstnode is always the bare leading IDENT here (a decorated first
	 * declarator routes to emit_local_multi_decl_full), so no op check. */
	if (g_td_arraydim > 0) {
		unsigned elem = g_td_arrayelem;
		int total = SIZE(elem) * g_td_arraydim;
		v = block_scope_decl(firstnode, IDIR(elem), 1);
		varadd(v, 0, IDIR(elem), 1);
		fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign(elem), total);
		goto rest_items;
	}
	/* Route the first declarator through block_scope_decl too, so a
	 * multi-declarator local shadowing a global/different-typed outer
	 * local is alpha-renamed rather than dying "double definition". */
	v = block_scope_decl(firstnode, base, 0);
	varadd(v, 0, base, 0);
	fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign(base), s);
	if ((KIND(base) == STRUCT_T || KIND(base) == UNION_T) && struct_has_bitfield(DREF(base)))
		emit_zero_local(v, s);
rest_items:
	for (n = rest; n; n = n->r) {
		/* When the leading declarator's `*` was absorbed by greedy
		 * type matching, the base already carries that pointer
		 * level.  In Stevie's uniform-* multi-decls (`T *X, *Y` or
		 * `T *X, Y[N]` etc.) the `*` we see in subsequent ext_decl
		 * items should match the base's level rather than add one.
		 * Peel one PTR off the base for consumers that would
		 * otherwise re-pointer it. */
		unsigned ebase = (KIND(base) == PTR) ? DREF(base) : base;
		v = n->u.v;
		if (n->op == 'F') {
			varadd(v, 1, FUNC(base), 0);
			continue;
		}
		if (n->op == 'G') {
			varadd(v, 1, FUNC(IDIR(ebase)), 0);
			continue;
		}
		if (n->op == 'B') {
			int count = n->l->u.n;
			int total = count * SIZE(ebase);
			v = block_scope_decl(n, IDIR(ebase), 1);
			varadd(v, 0, IDIR(ebase), 1);
			fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign(ebase), total);
			continue;
		}
		if (n->op == 'P') {
			t = IDIR(ebase);
			v = block_scope_decl(n, t, 0);
			varadd(v, 0, t, 0);
			fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign(t), SIZE(t));
			if (n->l)
				chain = multi_decl_chain_init(chain, v, n->l);
			continue;
		}
		if (n->op == 0 && g_td_arraydim > 0) {
			/* Array-typedef INSTANCE item (`jmp_buf a, b;` — b here). */
			unsigned elem = g_td_arrayelem;
			int total = SIZE(elem) * g_td_arraydim;
			v = block_scope_decl(n, IDIR(elem), 1);
			varadd(v, 0, IDIR(elem), 1);
			fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign(elem), total);
			continue;
		}
		/* Plain or [N] declarator: peel one * off the absorbed base
		 * so `char *p, c;` makes c a `char` (standard C semantics).
		 * `[N]` similarly lands at element-of-base. */
		t = (n->op == 'A') ? IDIR(ebase) : ebase;
		v = block_scope_decl(n, t, n->op == 'A' ? 1 : 0);
		varadd(v, 0, t, n->op == 'A' ? 1 : 0);
		fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign(t), SIZE(t));
		if ((KIND(t) == STRUCT_T || KIND(t) == UNION_T) && struct_has_bitfield(DREF(t)))
			emit_zero_local(v, SIZE(t));
		if (n->op == 0 && n->l)
			chain = multi_decl_chain_init(chain, v, n->l);
	}
	return chain;
}

/* Emit a file-scope array-typedef INSTANCE (`jmp_buf env;` at file or
 * static-local scope, where g_td_arraydim = D > 0 records the typedef's
 * inner dimension): a zero-filled data block of D*sizeof(elem) bytes,
 * registered as IDIR(elem) with the array flag so the name decays to its
 * address on use instead of being loaded as a scalar element.  This is a
 * plain array typedef instance, NOT an array-of-array-typedef, so no
 * aoa_dim is set.  Used by the bare-`;` and multi-declarator file-scope
 * rules.  Advances nglo. */
void
emit_global_arr_instance(char *name, unsigned elem, int dim)
{
	char buf[64];
	int total = SIZE(elem) * dim;
	if (nglo == NGlo)
		die("too many globals");
	sprintf(buf, "align %d { z %d }", iralign(elem), total);
	ini[nglo] = alloc(strlen(buf) + 1);
	strcpy(ini[nglo], buf);
	strcpy(gloname[nglo], name);
	varadd(name, nglo++, IDIR(elem), 1);
	var_set_arraybytes(name, total);
}

/* Emit a file-scope SIZED array global `name[count]`, aoa-aware.  When the
 * shared base is an array typedef (g_td_arraydim = D > 0) the element is the
 * D-wide inner array (byte size D*sizeof(elem), aoa flag set so name[i] is a
 * row address — see mkidx); otherwise a plain array of parsed_type.  Factored
 * from the `[expr] ';'` rule so the array-first multi-decl rule
 * (`jmp_buf fa[2], fb[2];`) reuses identical emission.  Advances nglo. */
void
emit_global_sized_array(char *name, long count)
{
	char buf[64];
	int elemsz, total;
	unsigned elemtyp;
	int aoa;
	if (nglo == NGlo)
		die("too many globals");
	if (g_td_arraydim > 0) {
		aoa = g_td_arraydim;
		elemtyp = g_td_arrayelem;
		elemsz = SIZE(elemtyp) * aoa;
	} else {
		aoa = 0;
		elemtyp = parsed_type;
		elemsz = SIZE(parsed_type);
	}
	total = elemsz * count;
	sprintf(buf, "align %d { z %d }", iralign(elemtyp), total);
	ini[nglo] = alloc(strlen(buf) + 1);
	strcpy(ini[nglo], buf);
	strcpy(gloname[nglo], name);
	maybe_mark_huge_global(nglo, name, total);
	varadd(name, nglo++, IDIR(elemtyp), 1);
	var_set_arraybytes(name, total);
	if (aoa > 0)
		var_set_aoa_dim(name, aoa);
}

/* --- function-local `static` MULTI-declarator support ---------------------
 * The `dcls STATIC ...` / statement-scope `STATIC ...` grammar carried only
 * single-declarator productions; `static int x, y;` / `static int a[3], b;` /
 * `static jmp_buf a, b;` were a hard parse error (a general grammar hole that
 * predates aoa and affects plain int too).  The three helpers below emit each
 * declarator of such a multi-decl as a mangled file-scope data global via
 * emit_static_local, sharing the base type with its siblings (so g_td_arraydim,
 * the inner dim of an array-typedef base such as jmp_buf, still applies).  All
 * are g_td_arraydim-gated so a non-array-typedef base emits byte-identically to
 * the existing single-declarator rules.  A scalar/pointer declarator may carry
 * an initializer (`static int x = 1, y = 2;` / `static char *p = a, *q = b;`),
 * folded into its own data block via emit_static_local_init exactly like the
 * single `static T v = init;` form (see emit_static_local_rest_item). */

/* Plain scalar or array-typedef INSTANCE first declarator (`static int x` /
 * `static jmp_buf a`).  Mirrors the `dcls STATIC type IDENT ';'` rule body. */
static void
emit_static_local_scalar_or_instance(unsigned base, char *v)
{
	char buf[64];
	if (base == NIL)
		die("invalid void declaration");
	if (g_td_arraydim > 0) {
		int total = SIZE(g_td_arrayelem) * g_td_arraydim;
		sprintf(buf, "align %d { z %d }", iralign(g_td_arrayelem), total);
		emit_static_local(v, IDIR(g_td_arrayelem), 1, buf);
		var_set_arraybytes(v, total);
	} else {
		emit_zero_init(buf, base);
		emit_static_local(v, base, 0, buf);
	}
}

/* Sized array declarator (`static int a[3]` / `static jmp_buf bufs[N]`),
 * aoa-aware like the statement-scope `STATIC type IDENT [ expr ] ;` rule. */
static void
emit_static_local_sized_array(unsigned base, char *v, long count)
{
	char buf[64];
	unsigned elemtyp = base;
	int aoa = 0;
	int total;
	if (base == NIL)
		die("invalid void array");
	if (g_td_arraydim > 0) {
		aoa = g_td_arraydim;
		elemtyp = g_td_arrayelem;
	}
	total = (aoa > 0 ? SIZE(elemtyp) * aoa : SIZE(base)) * count;
	sprintf(buf, "align %d { z %d }", iralign(elemtyp), total);
	emit_static_local(v, IDIR(elemtyp), 1, buf);
	var_set_arraybytes(v, total);
	if (aoa > 0)
		var_set_aoa_dim(v, aoa);
	maybe_mark_huge_global(nglo - 1, gloname[nglo - 1], total);
}

/* One ext_decllist item (the declarators after the first) of a static
 * multi-decl, dispatched on the ext_decl tag.  Subsequent items peel one
 * `*` off a base that greedily absorbed the leading pointer (the uniform-*
 * rule in emit_local_multi_decl), so `static char *p, *q;` makes both char*. */
static void
emit_static_local_rest_item(unsigned base, Node *n)
{
	char buf[64];
	char *v = n->u.v;
	unsigned ebase = (KIND(base) == PTR) ? DREF(base) : base;
	unsigned t;
	int total;

	if (n->op == 'F') {
		varadd(v, 1, FUNC(base), 0);
		return;
	}
	if (n->op == 'G' || n->op == 'H') {
		/* `*ident()` / `*ident(par1)` — K&R/ANSI proto returning a
		 * pointer; register the function type, no storage. */
		varadd(v, 1, FUNC(IDIR(ebase)), 0);
		return;
	}
	if (n->op == 'A')
		die("static array declarator needs a size");
	if (n->op == 'B') {
		long count;
		unsigned elemtyp = ebase;
		int aoa = 0;
		count = n->l->u.n;
		if (g_td_arraydim > 0) {
			aoa = g_td_arraydim;
			elemtyp = g_td_arrayelem;
		}
		total = (aoa > 0 ? SIZE(elemtyp) * aoa : SIZE(ebase)) * count;
		sprintf(buf, "align %d { z %d }", iralign(elemtyp), total);
		emit_static_local(v, IDIR(elemtyp), 1, buf);
		var_set_arraybytes(v, total);
		if (aoa > 0)
			var_set_aoa_dim(v, aoa);
		maybe_mark_huge_global(nglo - 1, gloname[nglo - 1], total);
		return;
	}
	if (n->op == 0 && g_td_arraydim > 0) {
		total = SIZE(g_td_arrayelem) * g_td_arraydim;
		sprintf(buf, "align %d { z %d }", iralign(g_td_arrayelem), total);
		emit_static_local(v, IDIR(g_td_arrayelem), 1, buf);
		var_set_arraybytes(v, total);
		return;
	}
	t = (n->op == 'P') ? IDIR(ebase) : ebase;
	if (n->l) {
		/* A scalar/pointer rest item WITH an initializer
		 * (`static int x = 1, y = 2;`, `static char *p = a, *q = b;`):
		 * fold the constant into its own mangled data block exactly like
		 * the single `static T v = init;` form (emit_static_local_init). */
		Node id;
		id.op = 'V';
		id.l = id.r = 0;
		strcpy(id.u.v, v);
		emit_static_local_init(t, &id, n->l);
		return;
	}
	emit_zero_init(buf, t);
	emit_static_local(v, t, 0, buf);
}

/* Emit a K&R-style function header.  Called from the prot_knr action
 * for definitions like `foo(a, b) int a; char *b; { ... }`.  `params`
 * is a Node chain of bare parameter names built by kr_idlist; each
 * was registered (via kr_param_dcls' kr_namelist actions) with its
 * declared type.  Anything not declared defaults to int.  Return type
 * is implicitly int. */
/* QBE function-definition keyword for the in-progress function: a C
 * `static` function has internal linkage, so emit a module-local `function`
 * (no `.globl`); everything else is `export function`.  See pending_static. */
const char *
fn_export_kw(void)
{
	/* __attribute__((interrupt)): pass the ISR property to QBE as
	 * `interrupt` linkage — the i8086 backend emits the full ISR
	 * prologue/epilogue (all-register save, static-memory ES save,
	 * DS/ES=DGROUP, iret).  The body itself ends in a normal `ret`. */
	if (cur_fn_interrupt)
		return pending_static ? "interrupt function"
		                      : "export interrupt function";
	return pending_static ? "function" : "export function";
}

void
emit_knr_func(char *fname, Node *params)
{
	Symb *s;
	Node *n;
	int t, m;

	for (n = params; n; n = n->r)
		if (!varget(n->u.v))
			varadd(n->u.v, 0, INT, 0);

	curfntyp = INT;
	cur_fn_labelid++;
	strncpy(cur_fn_name, fname, NString - 1);
	cur_fn_name[NString - 1] = 0;
	varadd(fname, 1, FUNC(INT), 0);
	fnproto_record(fname, params, INT);
	fprintf(of, "%s w $%s(", fn_export_kw(), fname);
	n = params;
	if (n)
		for (;;) {
			s = varget(n->u.v);
			fprintf(of, "%c ", is_aggr(s->ctyp) ? DATAPTR_T() : irtyp_ret(s->ctyp));
			fprintf(of, "%%t%d", tmp++);
			n = n->r;
			if (n)
				fprintf(of, ", ");
			else
				break;
		}
	fprintf(of, ") {\n");
	fprintf(of, "@l%d\n", lbl++);
	for (t = 0, n = params; n; t++, n = n->r) {
		s = varget(n->u.v);
		bind_param(n->u.v, s, t);
	}
}

/* Like emit_knr_func but uses parsed_type as the return type instead
 * of forcing int.  For `char *alloc(size) unsigned size; { ... }` the
 * caller has set curfntyp = parsed_type before invoking us. */
void
emit_knr_func_typed(char *fname, Node *params)
{
	Symb *s;
	Node *n;
	int t, m;

	for (n = params; n; n = n->r)
		if (!varget(n->u.v))
			varadd(n->u.v, 0, INT, 0);

	cur_fn_labelid++;
	strncpy(cur_fn_name, fname, NString - 1);
	cur_fn_name[NString - 1] = 0;
	varadd(fname, 1, FUNC(curfntyp), 0);
	fnproto_record(fname, params, curfntyp);

	/* Struct/union return-by-value: hidden first pointer parameter +
	 * pointer return (see cur_fn_sret notes near the top of the file). */
	cur_fn_sret = (KIND(curfntyp) == STRUCT_T || KIND(curfntyp) == UNION_T);
	cur_fn_sret_ctyp = curfntyp;

	if (cur_fn_sret)
		fprintf(of, "%s %c $%s(", fn_export_kw(), DATAPTR_T(), fname);
	else if (curfntyp == NIL)
		fprintf(of, "%s $%s(", fn_export_kw(), fname);
	else
		fprintf(of, "%s %c $%s(", fn_export_kw(), irtyp_ret(curfntyp), fname);
	if (cur_fn_sret) {
		fprintf(of, "%c %%t%d", DATAPTR_T(), tmp++);
		if (params)
			fprintf(of, ", ");
	}
	n = params;
	if (n)
		for (;;) {
			s = varget(n->u.v);
			fprintf(of, "%c ", is_aggr(s->ctyp) ? DATAPTR_T() : irtyp_ret(s->ctyp));
			fprintf(of, "%%t%d", tmp++);
			n = n->r;
			if (n)
				fprintf(of, ", ");
			else
				break;
		}
	fprintf(of, ") {\n");
	fprintf(of, "@l%d\n", lbl++);
	if (cur_fn_sret) {
		fprintf(of, "\t%%__sret =%c alloc4 %d\n", ALLOC_T(), DATAPTR_SZ());
		fprintf(of, "\tstore%c %%t0, %%__sret\n", DATAPTR_T());
	}
	for (t = (cur_fn_sret ? 1 : 0), n = params; n; t++, n = n->r) {
		s = varget(n->u.v);
		bind_param(n->u.v, s, t);
	}
}

Stmt *
mkfor(Node *ini, Node *tst, Node *inc, Stmt *s)
{
	Stmt *s1, *forst;

	if (ini)
		s1 = mkstmt(Expr, ini, 0, 0);
	else
		s1 = 0;
	if (!tst) {
		tst = mknode('N', 0, 0);
		tst->u.n = 1;
	}
	/* For: p1=cond (Node), p2=body (Stmt), p4=inc (Node, may be 0).
	 * Use a dedicated statement type so that `continue` can land on the
	 * inc step — lowering to While(cond, Seq(body, inc)) loses that, since
	 * the lowered while's continue target is the cond check and the inc
	 * step gets skipped. */
	forst = mkstmt(For, tst, s, 0);
	forst->p4 = inc;
	if (s1)
		return mkstmt(Seq, s1, forst, 0);
	else
		return forst;
}

%}

%union {
	Node *n;
	Stmt *s;
	unsigned u;
}

%token <n> NUM
%token <n> FNUM
%token <n> STR
%token <n> IDENT
%token PP MM LE GE SIZEOF SHL SHR ARROW ELLIPSIS
%token ADDEQ SUBEQ MULEQ DIVEQ MODEQ
%token ANDEQ OREQ XOREQ SHLEQ SHREQ

%token TVOID TCHAR TSHORT TINT TLNG TLNGLNG TUNSIGNED TSIGNED TFLOAT TDOUBLE CONST VOLATILE TBOOL TFAR INLINE STATIC EXTERN STATIC_ASSERT ALIGNOF ALIGNAS GENERIC ASM ATTRIBUTE
%token IF ELSE WHILE DO FOR BREAK CONTINUE RETURN GOTO
%token ENUM SWITCH CASE DEFAULT TYPEDEF TNAME STRUCT UNION

%left ','
%right '=' ADDEQ SUBEQ MULEQ DIVEQ MODEQ ANDEQ OREQ XOREQ SHLEQ SHREQ
%right '?' ':'
%left OR
%left AND
%left '|'
%left '^'
%left '&'
%left EQ NE
%left '<' '>' LE GE
%left SHL SHR
%left '+' '-'
%left '*' '/' '%'

%type <u> type
%type <s> stmt stmts asmstmt
%type <n> expr exp0 pref post arg0 arg1 par0 par1 fptpar0 fptpar1 initlist inititem generic_list generic_assoc idlist kr_idlist kr_namelist kr_name sm_more_names ext_decllist ext_decl comma_expr comma_exp0 init_decllist init_decl gaggr gilist gitem gival forinit_var gfnptr_decllist gfnptr_decl
%type <n> asmoutputs asmoutputlist asmoutput asminputs asminputlist asminput asmclobbers asmclobberlist
%token <u> TNAME

%%

prog: | prog kfunc | prog attr_kfunc | prog typed_decl | prog attr_typed_decl | prog edcl | prog tdcl | prog sdcl | prog static_assert_dcl | prog externdcl | prog gfnptrdcl | prog ';' ;

attr_kfunc: attrspec storageopt inlineopt init_attr prot_knr '{' dcls stmts '}'
{
	/* Interrupt handlers end in a normal `ret` too: the ISR property
	 * travels as QBE `interrupt` linkage (see fn_export_kw) and the
	 * backend turns every ret of the function into the full
	 * register-restore + iret epilogue. */
	if (!stmt($8, -1, -1))
		fprintf(of, "\tret 0\n");
	fprintf(of, "}\n\n");
};

attr_typed_decl: attrspec type_and_ident_noattr typed_decl_rest
{
	/* __attribute__((xxx)) type ident ... - attributes already set by attrspec */
}
               | attrspec STATIC type_and_ident_noattr typed_decl_rest
{
	/* `__attribute__((xxx)) static type ident ...` — the mirror of the
	 * `STATIC attrspec ...` form in typed_decl; MicroPython spells the
	 * slice helper `MP_NOINLINE static mp_obj_t *build_slice_…(…)`, i.e.
	 * attribute BEFORE the storage class.  attrspec set the attribute
	 * flags; type_and_ident_noattr does NOT reset them.  The lexer's
	 * pending_static flag (set on STATIC at brace_depth 0 regardless of a
	 * preceding ATTRIBUTE) already gives the function internal linkage. */
	glo_mark_static_range(glo_decl_start);
};

attrspec: ATTRIBUTE '(' '(' attrreset attrlist ')' ')';

type_and_ident_noattr: type IDENT
{
	parsed_type = $1;
	strcpy(parsed_ident, $2->u.v);
	glo_decl_start = nglo;
};

edcl: enumstart enums '}' ';'
    ;

enumstart: ENUM IDENT '{'  { enumval = 0; }
         | ENUM '{'         { enumval = 0; }
         ;

enums: enum
     | enums ',' enum
     | enums ','
     ;

enum: IDENT
{
	varadd($1->u.v, enumval, INT, 0);
	{
		unsigned eh0 = hash($1->u.v), eh = eh0;
		do {
			if (strcmp(varh[eh].v, $1->u.v) == 0) {
				varh[eh].enumconst = 1;
				break;
			}
			eh = (eh + 1) % NVar;
		} while (eh != eh0);
	}
	enumval++;
}
    | IDENT '=' expr
{
	/* Enumerator with an explicit constant-expression initializer.
	 * const_eval folds it at parse time and resolves references to
	 * prior enum constants (e.g. `B = A - 1`), so the enums grammar
	 * stays a left-to-right list with each name registered as it
	 * reduces. */
	enumval = const_eval($3);
	varadd($1->u.v, enumval, INT, 0);
	{
		unsigned eh0 = hash($1->u.v), eh = eh0;
		do {
			if (strcmp(varh[eh].v, $1->u.v) == 0) {
				varh[eh].enumconst = 1;
				break;
			}
			eh = (eh + 1) % NVar;
		} while (eh != eh0);
	}
	enumval++;
}
    ;

externdcl: EXTERN type IDENT ';'
{
	/* Extern variable - just register in symbol table, no allocation */
	if ($2 == NIL)
		die("invalid void extern declaration");
	varaddextern($3->u.v, $2, 0);
}
         | EXTERN type IDENT '[' ']' ';'
{
	/* Extern array without size - register as pointer */
	if ($2 == NIL)
		die("invalid void extern array");
	varaddextern($3->u.v, IDIR($2), 1);
}
         | EXTERN type IDENT '[' expr ']' ';'
{
	/* Extern array with size - register as pointer.  The dimension may be
	   any constant expression (e.g. `extern char buf[(32) + 1];`); no
	   storage is allocated here, but the total byte size is recorded so
	   sizeof on the extern array answers correctly.  NOTE a bare-NUM
	   dimension does NOT reduce here - it goes through ext_decllist as a
	   B node (see the multi-name rule below). */
	if ($2 == NIL)
		die("invalid void extern array");
	varaddextern($3->u.v, IDIR($2), 1);
	var_set_arraybytes($3->u.v, SIZE($2) * const_eval($5));
}
         | EXTERN STRUCT IDENT IDENT ';'
{
	/* Extern struct variable: extern struct foo bar;  An undefined tag
	 * is an incomplete type — legal for an extern decl (the definition
	 * lives in another TU).  Forward-declare it rather than die, mirroring
	 * the `type: STRUCT IDENT` rule. */
	int idx = structfind($3->u.v);
	if (idx < 0)
		idx = structadd_forward($3->u.v, 0);
	unsigned styp = (idx << 3) + STRUCT_T;
	varaddextern($4->u.v, styp, 0);
}
         | EXTERN STRUCT IDENT IDENT '[' ']' ';'
{
	/* Extern struct array without size: extern struct foo bar[]; */
	int idx = structfind($3->u.v);
	if (idx < 0)
		idx = structadd_forward($3->u.v, 0);
	unsigned styp = (idx << 3) + STRUCT_T;
	varaddextern($4->u.v, IDIR(styp), 1);
}
         | EXTERN STRUCT IDENT '*' IDENT ';'
{
	/* Extern struct pointer: extern struct foo *bar; */
	int idx = structfind($3->u.v);
	if (idx < 0)
		idx = structadd_forward($3->u.v, 0);
	unsigned styp = (idx << 3) + STRUCT_T;
	varaddextern($5->u.v, IDIR(styp), 0);
}
         | EXTERN type '(' '*' IDENT ')' '(' fptpar0 ')' ';'
{
	/* Extern function pointer: extern int (*callback)(int, int); */
	unsigned fptr_type = IDIR(FUNC($2));
	varaddextern($5->u.v, fptr_type, 0);
}
         | EXTERN type IDENT '(' ')' ';'
{
	/* K&R-style extern function declaration: extern char *strchr();
	 * Register the function return type; argument types are unknown. */
	if ($2 == NIL)
		varadd($3->u.v, 1, FUNC(NIL), 0);
	else
		varadd($3->u.v, 1, FUNC($2), 0);
}
         | EXTERN type IDENT '(' par1 ')' ';'
{
	/* Extern with typed prototype: extern int foo(int, int);
	 * MiniC just records the return type.  varclr drops the par1
	 * param names from the symtab — a file-scope prototype must not
	 * leak its param bindings, or a later decl reusing the name with
	 * a different type dies with a bogus double definition. */
	if ($2 == NIL)
		varadd($3->u.v, 1, FUNC(NIL), 0);
	else
		varadd($3->u.v, 1, FUNC($2), 0);
	fnproto_record($3->u.v, $5, $2);
	varclr();
}
         | EXTERN type ext_decllist ';'
{
	/* Multi-name extern declaration:
	 *   extern int Cursrow, Curscol, Cursvcol, Curswant;
	 *   extern char Redobuff[], Insbuff[];
	 *   extern char *malloc(), *strcpy();
	 * Each declarator is registered with the same base type ($2),
	 * adjusted for arrays and functions per `ext_decl_kind`. */
	Node *n;
	unsigned t;
	for (n = $3; n; n = n->r) {
		if (n->op == 'F') {
			t = FUNC($2);
		} else if (n->op == 'G') {
			t = FUNC(IDIR($2));
		} else if (n->op == 'H') {
			t = FUNC(IDIR($2));
			fnproto_record(n->u.v, n->l, IDIR($2));
		} else if (n->op == 'A' || n->op == 'B') {
			/* B = sized array declarator (the bare-NUM dimension form
			 * reduces through ext_decl, NOT the dedicated rule above).
			 * Before this branch existed it fell into the scalar else:
			 * the symbol registered as a plain base-type scalar, so a
			 * reference LOADED its first bytes instead of decaying to
			 * the array address (gc_add got seg 0 and wrote the IVT). */
			if ($2 == NIL)
				die("invalid void extern array");
			t = IDIR($2);
		} else if (n->op == 'P') {
			if ($2 == NIL)
				die("invalid void extern pointer");
			t = IDIR($2);
		} else {
			if ($2 == NIL)
				die("invalid void extern declaration");
			t = $2;
		}
		varaddextern(n->u.v, t, (n->op == 'A' || n->op == 'B') ? 1 : 0);
		if (n->op == 'B')
			var_set_arraybytes(n->u.v, SIZE($2) * n->l->u.n);
	}
	varclr();
}
         ;

gfnptrdcl: type gfnptr_decllist ';'
{
	/* File-scope function-pointer variable declaration, one or more
	 * declarators sharing the return type: void (*v)(void);  (§9a single)
	 * int (*a)(int), (*b)(int);  (§9b multi-declarator + qualified pointee). */
	emit_global_fnptr_list($1, $2, 0);
}
         | STATIC type gfnptr_decllist ';'
{
	/* static file-scope fn-ptr declaration (internal linkage). */
	emit_global_fnptr_list($2, $3, 1);
}
         ;

gfnptr_decllist: gfnptr_decl                       { $$ = $1; }
               | gfnptr_decl ',' gfnptr_decllist   { $1->r = $3; $$ = $1; }
               ;

gfnptr_decl: '(' '*' IDENT ')' '(' fptpar0 ')'
{
	$$ = mk_fnptr_decl($3->u.v, $6, 0);
}
           | '(' '*' IDENT ')' '(' fptpar0 ')' '=' expr
{
	$$ = mk_fnptr_decl($3->u.v, $6, $9);
}
           | gfnptr_quals '(' '*' IDENT ')' '(' fptpar0 ')'
{
	/* §9c: a qualified pointee — `void __far (*v)(void);` (§9b), and now
	 * `void __attribute__((interrupt)) (*v)(void);` and the combined far +
	 * attribute forms (newlibc interrupts.h spells a far ISR
	 * `void __far __attribute__((interrupt)) ...`).  gfnptr_quals collapses
	 * the whole qualifier run into ONE symbol, so the declarator's $ indices
	 * are fixed regardless of how many qualifiers were written.  Every
	 * qualifier is ACCEPTED and DROPPED: __far is a memory-model property on
	 * this toolchain and an interrupt/weak attribute on a fn-ptr VARIABLE has
	 * no codegen meaning (the ISR ABI lives on a function DEFINITION, not a
	 * pointer's pointee type), so the pointer type is IDIR(FUNC(base))
	 * identical to the unqualified declarator. */
	$$ = mk_fnptr_decl($4->u.v, $7, 0);
}
           | gfnptr_quals '(' '*' IDENT ')' '(' fptpar0 ')' '=' expr
{
	$$ = mk_fnptr_decl($4->u.v, $7, $10);
}
           ;

/* A non-empty qualifier run on a file-scope fn-ptr declarator.  It is
 * deliberately NOT nullable (the bare, no-qualifier declarator keeps its own
 * gfnptr_decl productions above) so an empty reduction can never compete with
 * typed_decl's `type TFAR attropt IDENT` on a TFAR lookahead.  gfnptr_attr
 * reuses attropt's OWN `attrreset` empty marker (NOT the §8r fp_attr form,
 * whose separate `fp_attr_save` marker collided reduce/reduce with attrreset
 * when §9b tried it at file scope), so the attribute is parsed by the exact
 * same item sequence as attropt and is distinguished only by the token after
 * the closing `))`: IDENT continues typed_decl, `(` continues this fn-ptr
 * declarator. */
gfnptr_quals: TFAR
            | gfnptr_attr
            | TFAR gfnptr_attr
            | gfnptr_attr TFAR
            ;

gfnptr_attr: ATTRIBUTE '(' '(' attrreset attrlist ')' ')'
{
	/* File-scope fn-ptr VARIABLE: the attribute (interrupt/weak/...) has no
	 * codegen meaning on a pointer, so drop whatever attrlist set.  The reset
	 * is defensive — every top-level definition that reads cur_fn_interrupt
	 * runs through its own attropt/attrreset first — but it keeps the global
	 * state clean across the declaration. */
	cur_fn_interrupt = 0;
	cur_fn_weak = 0;
};

ext_decllist: ext_decl
{
	$$ = $1;
}
            | ext_decl ',' ext_decllist
{
	$1->r = $3;
	$$ = $1;
}
            ;

ext_decl: IDENT                 { $$ = kr_name_node($1->u.v, 0); }
        | '*' IDENT             { $$ = kr_name_node($2->u.v, 'P'); }
        | IDENT '[' ']'         { $$ = kr_name_node($1->u.v, 'A'); }
        | IDENT '[' NUM ']'     { $$ = kr_array_node($1->u.v, $3->u.n); }
        | '*' IDENT '(' ')'     { $$ = kr_name_node($2->u.v, 'G'); }
        | '*' IDENT '(' par1 ')' { $$ = kr_name_node($2->u.v, 'H'); $$->l = $4; }
        | IDENT '(' ')'         { $$ = kr_name_node($1->u.v, 'F'); }
        | IDENT '=' expr        { $$ = kr_name_node($1->u.v, 0); $$->l = $3; }
        | '*' IDENT '=' expr    { $$ = kr_name_node($2->u.v, 'P'); $$->l = $4; }
        ;

tdcl: TYPEDEF type IDENT ';'
{
	typhadd($3->u.v, $2);
}
    | TYPEDEF type IDENT '[' expr ']' ';'
{
	/* Array typedef: `typedef int jmp_buf[8];`.  Kept as a real array
	 * type (decays to a pointer when passed to setjmp) — the dimension
	 * is folded by const_eval, the element type is $2. */
	if ($2 == NIL)
		die("invalid void array typedef");
	typhadd_array($3->u.v, $2, const_eval($5));
}
    | TYPEDEF ENUM IDENT IDENT ';' { typhadd($4->u.v, INT); }
    | TYPEDEF STRUCT IDENT IDENT ';' { typedef_struct_tag($3->u.v, $4->u.v); }
    | TYPEDEF typedefenum    {}
    | TYPEDEF typedefstruct  {}
    | TYPEDEF typedefunion   {}
    | TYPEDEF type '(' '*' IDENT ')' '(' fptpar0 ')' ';'
{
	/* Function pointer typedef: typedef int (*callback_t)(int, int); */
	unsigned fptr_type = IDIR(FUNC($2));  /* Pointer to function returning type */
	typhadd($5->u.v, fptr_type);
	typhset_fpid($5->u.v, fpproto_alloc($2, $8));  /* args + float ret (§2s/§5b) */
}
    ;

typedefenum: typedefenumstart enums '}' IDENT ';'
{
	/* Enum constants already added by enums rule */
	/* Typedef the enum type name to int (enums are ints in C) */
	typhadd($4->u.v, INT);
}
           ;

typedefenumstart: ENUM '{'
{
	enumval = 0;
}
                | ENUM IDENT '{'
{
	enumval = 0;
}
                ;

typedefstruct: typedefstructstart smembers '}' IDENT ';'
{
	/* Create typedef to the (tagged) struct */
	int idx = curstruct;
	structfinish(idx);
	curstruct = -1;
	typhadd($4->u.v, (idx << 3) + STRUCT_T);
}
             ;

typedefunion: typedefunionstart smembers '}' IDENT ';'
{
	/* Create typedef to the (tagged) union */
	int idx = curstruct;
	structfinish(idx);
	curstruct = -1;
	typhadd($4->u.v, (idx << 3) + UNION_T);
}
            ;

typedefstructstart: STRUCT IDENT '{'
{
	/* Tagged-only typedef start.  The anonymous STRUCT { form is reached
	 * through type: nested_s_begin smembers, so typedef struct {} T parses
	 * via TYPEDEF type IDENT.  Tagged-only avoids a 2nd STRUCT { marker. */
	curstruct = structadd($2->u.v, 0);
}
                  ;

typedefunionstart: UNION IDENT '{'
{
	curstruct = structadd($2->u.v, 1);
}
                 ;

static_assert_dcl: STATIC_ASSERT '(' expr ',' STR ')' ';'
{
	/* _Static_assert(constant-expression, string-literal).  The
	 * condition is a general constant expression; only evaluate it
	 * when const_eval can fold it (MicroPython embeds offsetof()
	 * address comparisons that are true at compile time but not
	 * foldable here — accept and skip those). */
	if (constfoldable($3) && const_eval($3) == 0)
		die("static assertion failed");
}
    ;

sdcl: structstart smembers '}' ';'
{
	structfinish(curstruct);
	curstruct = -1;  /* Done defining this struct */
}
    | STRUCT IDENT ';'
{
	/* Forward struct declaration: `struct _mp_print_t;` — register an
	 * incomplete tag so later `struct _mp_print_t *` / definition work. */
	structadd_forward($2->u.v, 0);
}
    | UNION IDENT ';'
{
	structadd_forward($2->u.v, 1);
}
    | structstart smembers '}' IDENT '[' NUM ']' ';'
{
	emit_struct_global_array($4->u.v, $6->u.n);
}
    | STATIC structstart smembers '}' IDENT '[' NUM ']' ';'
{
	emit_struct_global_array($5->u.v, $7->u.n);
	glostatic[nglo - 1] = 1;  /* the slot emit_struct_global_array
	                           * just registered (§6b) */
}
    ;

structstart: STRUCT IDENT '{'  { curstruct = structadd($2->u.v, 0); }
           | UNION IDENT '{'    { curstruct = structadd($2->u.v, 1); }
           ;

smembers:
        | smembers type IDENT ';'
{
	/* An array-typedef member (`jmp_buf jmpbuf;`) must allocate the
	 * full array, not a bare pointer. */
	if (g_td_arraydim > 0)
		structaddarrmember(curstruct, $3->u.v, g_td_arrayelem, g_td_arraydim);
	else {
		structaddmember(curstruct, $3->u.v, $2);
		/* fn-ptr typedef member (`F cb;`): inherit the proto so
		 * `obj->cb(...)' coerces args (§2s). */
		if (g_td_fpid >= 0)
			structset_last_fpid(curstruct, g_td_fpid);
	}
}
        | smembers type IDENT '[' expr ']' ';'
{
	/* Array dimension is a constant-expression (e.g.
	 * `void *regs[((13))]`); const_eval folds it. */
	if ($2 == NIL)
		die("invalid void array member");
	structaddarrmember(curstruct, $3->u.v, $2, const_eval($5));
}
        | smembers type IDENT '[' ']' ';'
{
	/* Flexible array member: `char data[];` — contributes 0 bytes,
	 * sits at the current offset. */
	if ($2 == NIL)
		die("invalid void array member");
	structaddarrmember(curstruct, $3->u.v, $2, 0);
}
        | smembers type IDENT ':' expr ';'
{
	/* Bitfield width is a constant-expression (e.g.
	 * `size_t total_prev_len : (8 * sizeof(size_t) - 1)`).  sizeof
	 * already folds to an 'N' node, so const_eval handles it. */
	structaddbitfield(curstruct, $3->u.v, $2, const_eval($5));
}
        | smembers type IDENT ',' sm_more_names ';'
{
	/* Multi-name member: `struct line *prev, *next;` — all share the
	 * same base type ($2).  Note: per-declarator pointer levels beyond
	 * the first declarator are tolerated but not honored (each '*' in
	 * sm_more_names is consumed for syntactic compatibility but does
	 * not add another pointer level).  This works for the common K&R
	 * pattern where every name in the list has matching decoration. */
	Node *n;
	structaddmember(curstruct, $3->u.v, $2);
	for (n = $5; n; n = n->r)
		structaddmember(curstruct, n->u.v, $2);
}
        | smembers type '(' '*' IDENT ')' '(' fptpar0 ')' ';'
{
	/* Function-pointer member literal: `int (*fn)(int, int);` */
	/* A void-returning function pointer member (`void (*close)(void *);`)
	 * is legal; FUNC(NIL) encodes the void return type. */
	structaddmember(curstruct, $5->u.v, IDIR(FUNC($2)));
	/* Record the member's parameter types so an indirect call through it
	 * (`obj->fn(...)') coerces arguments to the declared widths (§2q). */
	structset_last_fpid(curstruct, fpproto_alloc($2, $8));
}
        | smembers attrspec
        | smembers nestedagg
        ;

sm_more_names: IDENT
{
	Node *n = mknode(0, 0, 0);
	strcpy(n->u.v, $1->u.v);
	$$ = n;
}
             | '*' IDENT
{
	Node *n = mknode(0, 0, 0);
	strcpy(n->u.v, $2->u.v);
	$$ = n;
}
             | sm_more_names ',' IDENT
{
	Node *n = mknode(0, 0, 0);
	strcpy(n->u.v, $3->u.v);
	$1->r = n;
	$$ = $1;
}
             | sm_more_names ',' '*' IDENT
{
	Node *n = mknode(0, 0, 0);
	strcpy(n->u.v, $4->u.v);
	$1->r = n;
	$$ = $1;
}
             ;

nestedagg: nested_s_begin smembers '}' ';'
{
	/* Anonymous nested struct: hoist its members into the parent. */
	int idx = curstruct;
	structfinish(idx);
	curstruct = structstk[--structstksp];
	hoistanonymous(curstruct, idx);
}
         | nested_u_begin smembers '}' ';'
{
	/* Anonymous nested union: hoist its members into the parent. */
	int idx = curstruct;
	structfinish(idx);
	curstruct = structstk[--structstksp];
	hoistanonymous(curstruct, idx);
}
         ;

nested_s_begin: STRUCT '{'
{
	char nm[NString];
	if (structstksp >= NStructNest)
		die("struct nesting too deep");
	sprintf(nm, "__nested_%d", nestedanoncount++);
	structstk[structstksp++] = curstruct;
	curstruct = structadd(nm, 0);
}
              ;

nested_u_begin: UNION '{'
{
	char nm[NString];
	if (structstksp >= NStructNest)
		die("struct nesting too deep");
	sprintf(nm, "__nested_%d", nestedanoncount++);
	structstk[structstksp++] = curstruct;
	curstruct = structadd(nm, 1);
}
              ;

typed_decl: type_and_ident typed_decl_rest
{
	/* type_and_ident saves to globals, typed_decl_rest uses them */
}
          | STATIC type_and_ident typed_decl_rest
{
	/* `static` storage class: internal linkage.  Function linkage is
	 * handled by fn_export_kw/pending_static at the header; DATA slots
	 * registered by typed_decl_rest are retro-marked here so they emit
	 * as plain `data` (no .globl) — §6b. */
	glo_mark_static_range(glo_decl_start);
}
          | INLINE type_and_ident typed_decl_rest
{
	/* `inline` is a hint; MiniC emits the function normally. */
}
          | STATIC INLINE type_and_ident typed_decl_rest
{
	/* `static inline` — same treatment. */
	glo_mark_static_range(glo_decl_start);
}
          | INLINE STATIC type_and_ident typed_decl_rest
{
	/* `inline static` — same treatment. */
	glo_mark_static_range(glo_decl_start);
}
          | STATIC attrspec type_and_ident_noattr typed_decl_rest
{
	/* `static __attribute__((xxx)) type ident ...` — MicroPython spells
	 * `static __attribute__((noreturn)) void f(...)`.  attrspec set the
	 * attribute flags; type_and_ident_noattr does NOT reset them. */
	glo_mark_static_range(glo_decl_start);
};

type_and_ident: type IDENT
{
	cur_fn_interrupt = 0;
	cur_fn_weak = 0;
	parsed_type = $1;
	strcpy(parsed_ident, $2->u.v);
	glo_decl_start = nglo;
}
              | type attropt IDENT
{
	/* type __attribute__((xxx)) ident - attributes already set by attropt */
	parsed_type = $1;
	strcpy(parsed_ident, $3->u.v);
	glo_decl_start = nglo;
}
              | type TFAR attropt IDENT
{
	/* type __far __attribute__((xxx)) ident — ia16-gcc spells far ISRs
	 * `void __far __attribute__((interrupt)) timer_isr(void)` (newlibc
	 * interrupts.h).  The __far qualifies the FUNCTION (far call/ret),
	 * which on this toolchain is a memory-model property, so it is
	 * accepted and dropped from the parsed type. */
	parsed_type = $1;
	strcpy(parsed_ident, $4->u.v);
	glo_decl_start = nglo;
};

typed_decl_rest: ansi_func_proto '{' dcls stmts '}'
{
	/* ANSI function body.  Interrupt handlers end in a normal `ret`
	 * too: the ISR property travels as QBE `interrupt` linkage (see
	 * fn_export_kw) and the backend turns every ret of the function
	 * into the full register-restore + iret epilogue. */
	if (!stmt($4, -1, -1)) {
		if (curfntyp == NIL)
			fprintf(of, "\tret\n");
		else
			fprintf(of, "\tret 0\n");
	}
	fprintf(of, "}\n\n");
}
               | ansi_proto_register ';'
               | ansi_proto_register ATTRIBUTE '(' '(' attrlist ')' ')' ';'
{
	/* Postfix attribute on a prototype:
	 *   void _init(void) __attribute__((weak));   (newlibc syscalls.c)
	 * The attribute flags attrlist set are reset by the next
	 * definition's type_and_ident / init markers, so a prototype-only
	 * weak/interrupt marker is recorded-and-dropped. */
}
{
	/* ANSI function prototype: type name(args);  Registers the type
	 * without emitting any IR for a stub function. */
}
               | ';'
{
	/* Global variable */
	char buf[64];
	if (parsed_type == NIL)
		die("invalid void declaration");
	if (g_td_arraydim > 0) {
		/* File-scope array-typedef INSTANCE (`static jmp_buf env;`):
		 * a D-wide array, not a scalar element (see
		 * emit_global_arr_instance). */
		emit_global_arr_instance(parsed_ident, g_td_arrayelem, g_td_arraydim);
	} else {
	if (nglo == NGlo)
		die("too many string literals");
	emit_zero_init(buf, parsed_type);
	ini[nglo] = alloc(strlen(buf) + 1);
	strcpy(ini[nglo], buf);
	strcpy(gloname[nglo], parsed_ident);
	varadd(parsed_ident, nglo++, parsed_type, 0);
	/* Uninitialized file-scope definition: a later initialized definition
	 * of the same name may supersede this (C tentative definition). */
	mark_tentative(parsed_ident);
	}
}
               | '=' NUM ';'
{
	/* Global variable with integer initializer. */
	char buf[64];
	if (parsed_type == NIL)
		die("invalid void declaration");
	if (nglo == NGlo)
		die("too many string literals");
	sprintf(buf, "{ %c %d }", irtyp(parsed_type), $2->u.n);
	ini[nglo] = alloc(strlen(buf) + 1);
	strcpy(ini[nglo], buf);
	strcpy(gloname[nglo], parsed_ident);
	varadd(parsed_ident, nglo++, parsed_type, 0);
}
               | '=' expr ';'
{
	/* File-scope scalar initializer that is a constant expression:
	 * `static const size_t X = A >= 0x100 ? RULE_A : ... : 0;` (a long
	 * nested ternary chain over enum constants in py/parse.c), and the
	 * simpler arithmetic / bitwise / cast forms.  Folded via const_eval.
	 * Subsumes the former bare-NUM, -NUM, (NUM), and (-NUM) rules
	 * (byte-identical output: emit_global_int_init).  A bare STR still
	 * reduces via the '=' STR rule below (its '.' shifts ';'); '=' gaggr
	 * is distinguished by its leading brace.  Non-constant initializers
	 * die in const_eval. */
	if (ISFLOAT(parsed_type)) {
		emit_global_float_init($2);
	} else {
		/* cival_eval extends const_eval with symbol addresses, so
		 * `char **environ = __env;` and `int *p = &x;` emit a
		 * relocated `$sym+off` item (§6a) — pure-integer folds are
		 * byte-identical to the old const_eval path. */
		struct CIVal v;
		cival_eval($2, &v);
		if (v.issym)
			emit_global_sym_init(v.sym, v.off);
		else
			emit_global_int_init((int)v.off);
	}
}
               | '=' gaggr ';'                   { emit_global_aggregate(parsed_type, parsed_ident, $2); }
               | '[' expr ']' ';'
{
	/* Global array of basic type: emit a zero-filled data block.
	 * QBE syntax: `data $name = align N { z TOTAL_BYTES }`.  Dimension
	 * is a constant-expression (uses expr, not a bare NUM, so it does
	 * not shift/reduce-conflict with the sized `[expr] = {…}` init
	 * rule).  emit_global_sized_array is aoa-aware (`jmp_buf bufs[N]`). */
	if (parsed_type == NIL)
		die("invalid void array");
	emit_global_sized_array(parsed_ident, const_eval($2));
}
               | '[' expr ']' ',' ext_decllist ';'
{
	/* Array-FIRST multi-name top-level declaration:
	 *   int counts[3], total;          (plain)
	 *   static jmp_buf fa[2], fb[2];   (array-of-array-typedef, §8g GAP1)
	 * The leading declarator's `[expr]` had no multi-decl production
	 * (only `[expr] ';'` / `[expr] = {…} ';'` existed), so this form was a
	 * parse error.  Emit the first as a sized array (aoa-aware), then walk
	 * ext_decllist for the rest — mirroring the plain-first
	 * `, ext_decllist ';'` rule's item handling. */
	Node *n;
	unsigned t;
	char buf[64];
	int aoa = g_td_arraydim;
	unsigned aelem = g_td_arrayelem;
	if (parsed_type == NIL)
		die("invalid void array");
	emit_global_sized_array(parsed_ident, const_eval($2));
	for (n = $5; n; n = n->r) {
		if (n->op == 'B') {
			/* sized array item — `fb[2]` (aoa-aware via the helper). */
			emit_global_sized_array(n->u.v, n->l->u.n);
		} else if (n->op == 0 && aoa > 0) {
			/* array-typedef INSTANCE item (`jmp_buf fa[2], fb;`). */
			emit_global_arr_instance(n->u.v, aelem, aoa);
		} else if (n->op == 'F') {
			varadd(n->u.v, 1, FUNC(parsed_type), 0);
		} else if (n->op == 'A') {
			t = IDIR(parsed_type);
			if (nglo == NGlo)
				die("too many globals");
			sprintf(buf, "align %d { z 0 }", iralign(parsed_type));
			ini[nglo] = alloc(strlen(buf) + 1);
			strcpy(ini[nglo], buf);
			strcpy(gloname[nglo], n->u.v);
			varadd(n->u.v, nglo++, t, 1);
		} else {
			if (nglo == NGlo)
				die("too many globals");
			emit_zero_init(buf, parsed_type);
			ini[nglo] = alloc(strlen(buf) + 1);
			strcpy(ini[nglo], buf);
			strcpy(gloname[nglo], n->u.v);
			varadd(n->u.v, nglo++, parsed_type, 0);
		}
	}
}
               | '=' STR ';'
{
	/* Global pointer initialized with a string literal:
	 *   char *Version = "STEVIE - Version 3.69b";
	 * The lexer already reserved a global slot for the string itself
	 * (in `ini[$2->u.n]`).  Allocate a separate slot for the pointer
	 * variable that points at it. */
	char buf[64];
	if (parsed_type == NIL)
		die("invalid void declaration");
	if (nglo == NGlo)
		die("too many globals");
	sprintf(buf, "{ l $glo%d }", $2->u.n);
	ini[nglo] = alloc(strlen(buf) + 1);
	strcpy(ini[nglo], buf);
	strcpy(gloname[nglo], parsed_ident);
	varadd(parsed_ident, nglo++, parsed_type, 0);
}
               | ',' ext_decllist ';'
{
	/* Multi-name top-level declaration without `extern`:
	 *   int Cursrow, Curscol, Cursvcol;          (definitions)
	 *   bool_t bufempty(), buf1line();           (K&R prototypes)
	 * The first name was captured by type_and_ident as a plain
	 * variable.  Emit a global for it, then walk ext_decllist for
	 * the remaining declarators. */
	Node *n;
	unsigned t;
	char buf[64];
	int aoa = g_td_arraydim;   /* >0: shared base is an array typedef */
	unsigned aelem = g_td_arrayelem;
	if (parsed_type == NIL)
		die("invalid void declaration");
	/* First name: emit as plain global (or D-wide array instance when the
	 * shared base is an array typedef, `static jmp_buf a, b;`). */
	if (aoa > 0) {
		emit_global_arr_instance(parsed_ident, aelem, aoa);
	} else {
	if (nglo == NGlo)
		die("too many globals");
	emit_zero_init(buf, parsed_type);
	ini[nglo] = alloc(strlen(buf) + 1);
	strcpy(ini[nglo], buf);
	strcpy(gloname[nglo], parsed_ident);
	varadd(parsed_ident, nglo++, parsed_type, 0);
	}
	for (n = $2; n; n = n->r) {
		if (n->op == 0 && aoa > 0) {
			/* array-typedef INSTANCE item (`jmp_buf a, b;` — b here). */
			emit_global_arr_instance(n->u.v, aelem, aoa);
			continue;
		}
		if (n->op == 'F') {
			t = FUNC(parsed_type);
			varadd(n->u.v, 1, t, 0);
		} else if (n->op == 'A') {
			t = IDIR(parsed_type);
			if (nglo == NGlo)
				die("too many globals");
			sprintf(buf, "align %d { z 0 }", iralign(parsed_type));
			ini[nglo] = alloc(strlen(buf) + 1);
			strcpy(ini[nglo], buf);
			strcpy(gloname[nglo], n->u.v);
			varadd(n->u.v, nglo++, t, 1);
		} else if (n->op == 'B') {
			/* Sized array declarator in a multi-name file-scope decl,
			 * e.g. int a, b 10 elements.  Emit a real zero block of the
			 * full array size (the scalar else-branch used to emit one
			 * element and register a scalar - wrong size AND no decay). */
			int total = SIZE(parsed_type) * n->l->u.n;
			t = IDIR(parsed_type);
			if (nglo == NGlo)
				die("too many globals");
			sprintf(buf, "align %d { z %d }", iralign(parsed_type), total);
			ini[nglo] = alloc(strlen(buf) + 1);
			strcpy(ini[nglo], buf);
			strcpy(gloname[nglo], n->u.v);
			varadd(n->u.v, nglo++, t, 1);
			var_set_arraybytes(n->u.v, total);
		} else {
			if (nglo == NGlo)
				die("too many globals");
			emit_zero_init(buf, parsed_type);
			ini[nglo] = alloc(strlen(buf) + 1);
			strcpy(ini[nglo], buf);
			strcpy(gloname[nglo], n->u.v);
			varadd(n->u.v, nglo++, parsed_type, 0);
		}
	}
}
               | knr_func_proto '{' dcls stmts '}'
{
	/* K&R definition with explicit return type:
	 *   char *alloc(size) unsigned size; { ... }
	 *   int EnvEval(s, len) char *s; int len; { ... }
	 * The function header was already emitted when knr_func_proto
	 * reduced (so it precedes any local-decl emit from dcls). */
	if (!stmt($4, -1, -1)) {
		if (curfntyp == NIL)
			fprintf(of, "\tret\n");
		else
			fprintf(of, "\tret 0\n");
	}
	fprintf(of, "}\n\n");
}
               | '(' init_ansi par0 ')' ',' ext_decllist ';'
{
	/* Multi-name K&R prototype:
	 *   char *alloc(), *strsave(), *mkstr();
	 *   void filealloc(), freeall();
	 * The first name (with its `()`) is registered as a function
	 * returning parsed_type; ext_decllist handles the rest. */
	Node *n;
	unsigned t;
	if (parsed_type == NIL)
		varadd(parsed_ident, 1, FUNC(NIL), 0);
	else
		varadd(parsed_ident, 1, FUNC(parsed_type), 0);
	for (n = $6; n; n = n->r) {
		if (n->op == 'F' || n->op == 0) {
			/* Function (with or without leading *). */
			t = (parsed_type == NIL) ? FUNC(NIL) : FUNC(parsed_type);
			varadd(n->u.v, 1, t, 0);
		} else if (n->op == 'A') {
			t = IDIR(parsed_type);
			varadd(n->u.v, 0, t, 1);
		} else if (n->op == 'H') {
			t = (parsed_type == NIL) ? FUNC(NIL)
			                         : FUNC(IDIR(parsed_type));
			varadd(n->u.v, 1, t, 0);
			fnproto_record(n->u.v, n->l, IDIR(parsed_type));
		} else {
			varadd(n->u.v, 1, FUNC(parsed_type), 0);
		}
	}
	varclr();
}
               | '[' ']' '=' gaggr ';'
{
	/* TYP NAME[] = { ... };  Routed through the generic aggregate
	 * machinery: each element may be a scalar, a pointer, or a
	 * (possibly designated) struct.  Element count inferred from the
	 * brace list.  Used for `struct charinfo chars[] = {...}`,
	 * `struct P arr[] = { {.a=1}, {.a=2} }`, scalar tables, and
	 * `char *msgs[] = {"a","b","c"}`. */
	emit_global_array(parsed_type, parsed_ident, -1, $4);
}
               | '[' expr ']' '=' gaggr ';'
{
	/* TYP NAME[N] = { … };  Explicit size — agg_emit_array zero-fills
	 * any missing trailing elements. */
	emit_global_array(parsed_type, parsed_ident, const_eval($2), $5);
}
               | '[' ']' '=' STR ';'
{
	/* char NAME[] = "string";  Emit the literal bytes as a char array
	 * (NUL-terminated), not a pointer to the string. */
	if (parsed_type == NIL)
		die("invalid void array");
	emit_string_array(parsed_type, parsed_ident, $4->u.n, 0);
}
               | '[' expr ']' '=' STR ';'
{
	/* char NAME[N] = "string";  Explicit size — emit the literal bytes
	 * (NUL-terminated), zero-filled to N elements. */
	if (parsed_type == NIL)
		die("invalid void array");
	emit_string_array_sized(parsed_type, parsed_ident, $5->u.n,
	    const_eval($2), 0);
}
               ;

sai_init_clear: { sai_clear(); };

opt_trailing_comma: | ',';

sai_list: sai_item
        | sai_list ',' sai_item
        ;

sai_item: expr                 { sai_add_expr($1); }
        | '[' expr ']' '=' expr { sai_designate(const_eval($2)); sai_add_expr($5); }
        | '{' sai_list opt_trailing_comma '}' { }
        ;

ansi_proto_register: '(' init_ansi par0 ')'
{
	/* Prototype-only registration: register function type, no IR emission. */
	curfntyp = parsed_type;
	varadd(parsed_ident, 1, FUNC(curfntyp), 0);
	fnproto_record(parsed_ident, $3, curfntyp);
	varclr();
};

knr_func_proto: '(' init_kr kr_idlist ')' kr_param_dcls
{
	/* Reduces between the K&R parameter list and the body, so the
	 * function header is emitted before dcls/stmts emit anything. */
	curfntyp = parsed_type;
	emit_knr_func_typed(parsed_ident, $3);
};

ansi_func_proto: '(' init_ansi par0 ')'
{
	Symb *s;
	Node *n;
	int t, m;

	curfntyp = parsed_type;
	cur_fn_labelid++;
	strncpy(cur_fn_name, parsed_ident, NString - 1);
	cur_fn_name[NString - 1] = 0;
	varadd(parsed_ident, 1, FUNC(curfntyp), 0);
	fnproto_record(parsed_ident, $3, curfntyp);

	/* Struct/union return-by-value: lower to a hidden first pointer
	 * parameter (caller-allocated result storage) plus a pointer
	 * return.  See the cur_fn_sret notes near the top of the file. */
	cur_fn_sret = (KIND(curfntyp) == STRUCT_T || KIND(curfntyp) == UNION_T);
	cur_fn_sret_ctyp = curfntyp;

	if (cur_fn_sret)
		fprintf(of, "%s %c $%s(", fn_export_kw(), DATAPTR_T(), parsed_ident);
	else if (curfntyp == NIL)
		fprintf(of, "%s $%s(", fn_export_kw(), parsed_ident);
	else
		fprintf(of, "%s %c $%s(", fn_export_kw(), irtyp_ret(curfntyp), parsed_ident);
	if (cur_fn_sret) {
		/* Hidden return pointer occupies %t0; real params follow. */
		fprintf(of, "%c %%t%d", DATAPTR_T(), tmp++);
		if ($3)
			fprintf(of, ", ");
	}
	n = $3;
	if (n)
		for (;;) {
			s = varget(n->u.v);
			fprintf(of, "%c ", is_aggr(s->ctyp) ? DATAPTR_T() : irtyp_ret(s->ctyp));
			fprintf(of, "%%t%d", tmp++);
			n = n->r;
			if (n)
				fprintf(of, ", ");
			else
				break;
		}
	fprintf(of, ") {\n");
	fprintf(of, "@l%d\n", lbl++);
	if (cur_fn_sret) {
		/* Spill the hidden pointer to a fixed-name slot so `ret` can
		 * reload it from whichever basic block it lands in. */
		fprintf(of, "\t%%__sret =%c alloc4 %d\n", ALLOC_T(), DATAPTR_SZ());
		fprintf(of, "\tstore%c %%t0, %%__sret\n", DATAPTR_T());
	}
	for (t = (cur_fn_sret ? 1 : 0), n=$3; n; t++, n=n->r) {
		s = varget(n->u.v);
		bind_param(n->u.v, s, t);
	}
};

init_ansi:
{
	varclr();
	tmp = 0;
	clit = 0;
	cur_fn_sret = 0;
};

init_kr:
{
	/* Reset symbol table for a K&R function body.  parsed_type and
	 * parsed_ident were set by type_and_ident before this fires. */
	varclr();
	tmp = 0;
	clit = 0;
	cur_fn_interrupt = 0;
	cur_fn_weak = 0;
	cur_fn_sret = 0;
};

init:
{
	varclr();
	tmp = 0;
	clit = 0;
	cur_fn_interrupt = 0;
	cur_fn_weak = 0;
	cur_fn_sret = 0;
};

init_attr: { varclr(); tmp = 0; clit = 0; cur_fn_sret = 0; };

inlineopt: INLINE
         |
         ;

storageopt: STATIC
          | EXTERN
          |
          ;

attrreset: { cur_fn_interrupt = 0; cur_fn_weak = 0; };

attropt: ATTRIBUTE '(' '(' attrreset attrlist ')' ')'
       | attrreset
       ;

attrlist: attritem
        | attrlist ',' attritem
        ;

attritem: IDENT {
        /* Handle specific attributes */
        if (strcmp($1->u.v, "interrupt") == 0) {
            cur_fn_interrupt = 1;
        } else if (strcmp($1->u.v, "weak") == 0) {
            cur_fn_weak = 1;
        }
        /* Other attributes are silently ignored for compatibility */
    }
    ;

kfunc: storageopt inlineopt attropt init prot_knr '{' dcls stmts '}'
{
	/* Interrupt handlers end in a normal `ret` too — see
	 * fn_export_kw (QBE `interrupt` linkage). */
	if (!stmt($8, -1, -1))
		fprintf(of, "\tret 0\n");
	fprintf(of, "}\n\n");
};

prot_knr: IDENT '(' par0 ')'
{
	Symb *s;
	Node *n;
	int t, m;

	curfntyp = INT;
	cur_fn_labelid++;
	strncpy(cur_fn_name, $1->u.v, NString - 1);
	cur_fn_name[NString - 1] = 0;
	varadd($1->u.v, 1, FUNC(INT), 0);
	fprintf(of, "%s w $%s(", fn_export_kw(), $1->u.v);
	n = $3;
	if (n)
		for (;;) {
			s = varget(n->u.v);
			fprintf(of, "%c ", irtyp_ret(s->ctyp));
			fprintf(of, "%%t%d", tmp++);
			n = n->r;
			if (n)
				fprintf(of, ", ");
			else
				break;
		}
	fprintf(of, ") {\n");
	fprintf(of, "@l%d\n", lbl++);
	for (t=0, n=$3; n; t++, n=n->r) {
		s = varget(n->u.v);
		m = SIZE(s->ctyp);
		emit_local_alloc(n->u.v, ALLOC_T(), iralign(s->ctyp), m);
		fprintf(of, "\tstore%c %%t%d", irtyp(s->ctyp), t);
		fprintf(of, ", %%%s\n", n->u.v);
	}
}
        | IDENT '(' kr_idlist ')' kr_param_dcls
{
	emit_knr_func($1->u.v, $3);
};

kr_idlist: IDENT
{
	Node *n = mknode(0, 0, 0);
	strcpy(n->u.v, $1->u.v);
	$$ = n;
}
         | IDENT ',' kr_idlist
{
	Node *n = mknode(0, 0, $3);
	strcpy(n->u.v, $1->u.v);
	$$ = n;
}
         ;

kr_param_dcls: { }
             | kr_param_dcls type kr_namelist ';' { kr_apply_types($2, $3); }
             ;

kr_namelist: kr_name
{
	$$ = $1;
}
           | kr_name ',' kr_namelist
{
	$1->r = $3;
	$$ = $1;
}
           ;

kr_name: IDENT                 { $$ = kr_name_node($1->u.v, 0); }
       | '*' IDENT             { $$ = kr_name_node($2->u.v, 'P'); }
       | IDENT '[' ']'         { $$ = kr_name_node($1->u.v, 'A'); }
       | IDENT '[' NUM ']'     { $$ = kr_name_node($1->u.v, 'A'); }
       | '*' IDENT '[' ']'     { $$ = kr_name_node($2->u.v, 'X'); }
       | '*' IDENT '[' NUM ']' { $$ = kr_name_node($2->u.v, 'X'); }
       ;

par0: par1
    |                     { $$ = 0; }
    ;
par1: type IDENT ',' par1 { $$ = param($2->u.v, $1, $4); }
    | type IDENT          { $$ = param($2->u.v, $1, 0); }
    | type IDENT '[' ']' ',' par1
{
	/* Array parameter decays to pointer: int f(char buf[], ...) */
	$$ = param($2->u.v, ($1 & FAR) ? IDIR_FAR($1) : IDIR($1), $6);
}
    | type IDENT '[' ']'
{
	$$ = param($2->u.v, ($1 & FAR) ? IDIR_FAR($1) : IDIR($1), 0);
}
    | type IDENT '[' expr ']' ',' par1
{
	/* Sized array parameter: the dimension is documentation only
	 * (uint8_t out[11] is uint8_t *out), folded and discarded. */
	$$ = param($2->u.v, ($1 & FAR) ? IDIR_FAR($1) : IDIR($1), $7);
}
    | type IDENT '[' expr ']'
{
	$$ = param($2->u.v, ($1 & FAR) ? IDIR_FAR($1) : IDIR($1), 0);
}
    | type ',' par1       { $$ = abstract_param($1, $3); }
    | type                { $$ = abstract_param($1, 0); }
    | ELLIPSIS            { $$ = 0; /* variadic marker: ... proto only, no IR */ }
    | type '(' '*' IDENT ')' '(' fptpar0 ')' ',' par1 {
        /* Function pointer parameter: int (*callback)(int, int), ...
         * Record the fpid (§5b) so a call through the PARAMETER coerces
         * args and recovers a float return class (modmath.c's
         * math_generic_1(x, mp_float_t (*f)(mp_float_t)) shape). */
        unsigned fptr_type = IDIR(FUNC($1));
        $$ = param($4->u.v, fptr_type, $10);
        varsetfpid($4->u.v, fpproto_alloc($1, $7));
    }
    | type '(' '*' IDENT ')' '(' fptpar0 ')' {
        /* Function pointer parameter: int (*callback)(int, int) */
        unsigned fptr_type = IDIR(FUNC($1));
        $$ = param($4->u.v, fptr_type, 0);
        varsetfpid($4->u.v, fpproto_alloc($1, $7));
    }
    | type fpquals '(' '*' IDENT ')' '(' fptpar0 ')' ',' par1 {
        /* §8r: function pointer parameter whose pointee carries far/attribute
         * qualifiers — newlibc interrupts.c's
         *   static void isr_entry(ivt_entry_t *entry,
         *                         void __far __attribute__((interrupt)) (*isr)(void))
         * The fpquals (__far and/or __attribute__((interrupt))) qualify the
         * pointed-to function (a far-call/iret calling convention), which on
         * this toolchain is a memory-model property; they are accepted and
         * dropped, exactly as the `type TFAR attropt IDENT` function-header
         * rule drops them.  The pointer type is computed identically to the
         * unqualified fn-ptr param above. */
        unsigned fptr_type = IDIR(FUNC($1));
        $$ = param($5->u.v, fptr_type, $11);
        varsetfpid($5->u.v, fpproto_alloc($1, $8));
    }
    | type fpquals '(' '*' IDENT ')' '(' fptpar0 ')' {
        unsigned fptr_type = IDIR(FUNC($1));
        $$ = param($5->u.v, fptr_type, 0);
        varsetfpid($5->u.v, fpproto_alloc($1, $8));
    }
    ;

/* §8r: non-empty qualifier run between a fn-ptr param's return type and its
 * `(*name)` declarator.  Distinct first-tokens (TFAR / ATTRIBUTE) keep this
 * conflict-free against the `type TFAR '*'` pointer-type extension. */
fpquals: TFAR
       | fp_attr
       | TFAR fp_attr
       ;

fp_attr: ATTRIBUTE '(' '(' fp_attr_save attrlist ')' ')'
{
	/* attrlist's attritem actions mutate cur_fn_interrupt/cur_fn_weak;
	 * restore the enclosing function's values saved by fp_attr_save. */
	cur_fn_interrupt = fp_saved_interrupt;
	cur_fn_weak = fp_saved_weak;
};

fp_attr_save: { fp_saved_interrupt = cur_fn_interrupt; fp_saved_weak = cur_fn_weak; };

fptpar0: fptpar1
       |                  { $$ = 0; }
       ;
fptpar1: type ',' fptpar1        { $$ = mkptype($1, $3); }
       | type                    { $$ = mkptype($1, 0); }
       | type IDENT ',' fptpar1  { $$ = mkptype($1, $4); }
       | type IDENT              { $$ = mkptype($1, 0); }
       | ELLIPSIS                { $$ = 0; }
       ;

dcls:
    | dcls enumstart enums '}' ';'
{
	/* Function-local enum declaration: `enum { ARG_sep, ARG_end };`.
	 * The enums rule already registered each constant via varadd; an
	 * anonymous local enum introduces only the constants (no storage).
	 * Mirrors the file-scope `edcl` rule. */
}
    | dcls type IDENT ';'
{
	int s, i;
	char *v;

	if ($2 == NIL)
		die("invalid void declaration");
	if (g_td_arraydim > 0) {
		/* Array-typedef local (`jmp_buf env;`): allocate the whole
		 * array and register it as an array so it decays to a
		 * pointer-to-element on use. */
		int total = SIZE(g_td_arrayelem) * g_td_arraydim;
		v = block_scope_decl($3, IDIR(g_td_arrayelem), 1);
		varadd(v, 0, IDIR(g_td_arrayelem), 1);
		fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(),
			iralign(g_td_arrayelem), total);
	} else {
	s = SIZE($2);
	v = block_scope_decl($3, $2, 0);
	varadd(v, 0, $2, 0);
	/* fn-ptr typedef variable (`F fp;` where `typedef RET(*F)(...)`):
	 * inherit the proto so an indirect call `fp(...)' coerces args (§2s). */
	if (g_td_fpid >= 0)
		varsetfpid(v, g_td_fpid);
	emit_local_alloc(v, ALLOC_T(), iralign($2), s);

	/* Implicit zero-init only when the struct has a bitfield (see
	 * struct_has_bitfield); an explicit `= {0}` zeroes via the rule above. */
	if ((KIND($2) == STRUCT_T || KIND($2) == UNION_T) && struct_has_bitfield(DREF($2)))
		emit_zero_local(v, s);
	(void)i;
	}
}
    | dcls type IDENT '=' expr ';'
{
	/* Local declaration with initializer: int x = expr; */
	int s;
	char *v;
	Node *init_node;

	if ($2 == NIL)
		die("invalid void declaration");
	v = block_scope_decl($3, $2, 0);
	s = SIZE($2);
	varadd(v, 0, $2, 0);
	/* fn-ptr typedef variable with initializer (`F fp = somefunc;`):
	 * inherit the proto so a later indirect call coerces args (§2s). */
	if (g_td_fpid >= 0)
		varsetfpid(v, g_td_fpid);
	emit_local_alloc(v, ALLOC_T(), iralign($2), s);
	/* Evaluate initializer as `IDENT = expr` */
	init_node = mknode('=', $3, $5);
	expr(init_node);
}
    | dcls type IDENT '=' '{' initlist '}' ';'
{
	/* Local aggregate initializer: `struct P p = { 1, 2 };`.
	 * Desugar to `p; p = (type){ 1, 2 };` — allocate the var, then
	 * assign a compound literal of the same type (the 'L'
	 * compound-literal path handles member/designator placement and
	 * the struct-copy assignment). */
	int s;
	char *v;
	Node *clit_node, *init_node;

	if ($2 == NIL)
		die("invalid void declaration");
	v = block_scope_decl($3, $2, 0);
	s = SIZE($2);
	varadd(v, 0, $2, 0);
	fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign($2), s);
	if (initlist_is_zero($6) &&
	    (KIND($2) == STRUCT_T || KIND($2) == UNION_T)) {
		/* `S s = {0};` — zero the target directly (no compound-literal
		 * temp, no struct copy). */
		emit_zero_local(v, s);
	} else {
		clit_node = mknode('L', $6, 0);
		clit_node->u.n = (int)$2;
		init_node = mknode('=', $3, clit_node);
		expr(init_node);
	}
}
    | dcls type IDENT ',' ext_decllist ';'
{
	/* dcls context: parse-time emit IS the entry block, which is also
	 * the lexical position of the declaration — emit the chain now. */
	Node *ch = emit_local_multi_decl($2, $3, $5);
	if (ch)
		expr(ch);
}
    | dcls type IDENT '[' expr ']' ',' ext_decllist ';'
{
	Node *first = kr_array_node($3->u.v, const_eval($5));
	Node *ch;
	first->r = $8;
	ch = emit_local_multi_decl_full($2, first);
	if (ch)
		expr(ch);
}
    | dcls type IDENT '(' ')' ',' ext_decllist ';'
{
	/* `T name1(), name2, ...;` — first declarator is K&R proto.
	 * Build a 'F' node and chain. */
	Node *first = kr_name_node($3->u.v, 'F');
	Node *ch;
	first->r = $7;
	ch = emit_local_multi_decl_full($2, first);
	if (ch)
		expr(ch);
}
    | dcls type IDENT '=' expr ',' init_decllist ';'
{
	/* Multi-name local declaration with initializers, all sharing
	 * the same base type:  int row = 0, col = 0;
	 * Each item in init_decllist is an IDENT with optional `=` expr
	 * (the init expr is hung off node->l). */
	emit_local_init($2, $3, $5);
	emit_local_init_list($2, $7);
}
    | dcls type IDENT '(' ')' ';'
{
	/* Local K&R prototype:  void tabinout();  Register the function
	 * type without allocating any storage. */
	varadd($3->u.v, 1, FUNC($2), 0);
}
    | dcls type IDENT '(' par1 ')' ';'
{
	/* Local ANSI prototype with typed args. */
	varadd($3->u.v, 1, FUNC($2), 0);
	fnproto_record($3->u.v, $5, $2);
}
    | dcls ALIGNAS '(' NUM ')' type IDENT ';'
{
	/* _Alignas(constant) type var; */
	int s;
	char *v;
	int align;

	if ($6 == NIL)
		die("invalid void declaration");
	v = $7->u.v;
	s = SIZE($6);
	align = $4->u.n;

	/* Validate alignment is power of 2 */
	if (align <= 0 || (align & (align - 1)) != 0)
		die("_Alignas requires power of 2");

	varadd(v, 0, $6, 0);
	/* Emit comment about alignment requirement */
	fprintf(of, "\t# _Alignas(%d) for %%%s\n", align, v);
	fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), align, s);
}
    | dcls ALIGNAS '(' type ')' type IDENT ';'
{
	/* _Alignas(type) type var; */
	int s;
	char *v;
	int align;

	if ($6 == NIL)
		die("invalid void declaration");
	v = $7->u.v;
	s = SIZE($6);

	/* Calculate alignment from type */
	if (KIND($4) == CHR)
		align = 1;
	else if (KIND($4) == LNG || ISFLOAT($4))
		align = 4;
	else
		align = 2;

	varadd(v, 0, $6, 0);
	/* Emit comment about alignment requirement */
	fprintf(of, "\t# _Alignas(%s) = %d for %%%s\n",
		KIND($4) == CHR ? "char" :
		KIND($4) == LNG ? "long" : "int",
		align, v);
	fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), align, s);
}
    | dcls STATIC type IDENT ';'
{
	/* Function-local static scalar/struct (uninitialized): emit as a
	 * zero-filled file-scope global with mangled name `_<fn>_<var>` so
	 * its address persists across calls. */
	char buf[64];
	if ($3 == NIL)
		die("invalid void declaration");
	if (g_td_arraydim > 0) {
		/* static array-typedef INSTANCE (`static jmp_buf env;` in a fn):
		 * a D-wide array, registered IDIR(elem) so it decays. */
		int total = SIZE(g_td_arrayelem) * g_td_arraydim;
		sprintf(buf, "align %d { z %d }", iralign(g_td_arrayelem), total);
		emit_static_local($4->u.v, IDIR(g_td_arrayelem), 1, buf);
		var_set_arraybytes($4->u.v, total);
	} else {
	emit_zero_init(buf, $3);
	emit_static_local($4->u.v, $3, 0, buf);
	}
}
    | dcls STATIC type IDENT '=' expr ';' { emit_static_local_init($3, $4, $6); }
    | dcls STATIC type IDENT '[' ']' '=' gaggr ';'
{
	/* Function-local static array with initializer, routed through the
	 * generic aggregate machinery so each element may be a scalar,
	 * pointer, or (possibly designated) struct/union.  Element count
	 * inferred from the brace list. */
	emit_static_array($3, $4->u.v, -1, $8);
}
    | dcls STATIC type IDENT '[' expr ']' '=' gaggr ';'
{
	/* Sized function-local static array with initializer. */
	emit_static_array($3, $4->u.v, const_eval($6), $9);
}
    | dcls STATIC type IDENT '[' ']' '=' STR ';'
{
	/* Function-local static char array initialised from a string
	 * literal: `static const byte whitespace[] = " ...";` */
	if ($3 == NIL)
		die("invalid void array");
	emit_string_array($3, $4->u.v, $8->u.n, 1);
}
    | dcls STATIC type IDENT '[' expr ']' '=' STR ';'
{
	/* Function-local static SIZED char array from a string literal:
	 * `static char cwd[64] = "/";` — literal bytes at the front, the
	 * rest zero-filled (file-scope production §8v, static_local form). */
	if ($3 == NIL)
		die("invalid void array");
	emit_string_array_sized($3, $4->u.v, $9->u.n, const_eval($6), 1);
}
    | dcls STATIC type IDENT '[' expr ']' ';'
{
	/* Function-local static array (uninitialized) — emit as a
	 * zero-filled file-scope data global with mangled name.
	 * Dimension is a constant-expression (const_eval folds sizeof). */
	char buf[64];
	int total;
	if ($3 == NIL)
		die("invalid void array");
	total = SIZE($3) * const_eval($6);
	sprintf(buf, "align %d { z %d }", iralign($3), total);
	emit_static_local($4->u.v, IDIR($3), 1, buf);
	var_set_arraybytes($4->u.v, total);
	maybe_mark_huge_global(nglo - 1, gloname[nglo - 1], total);
}
    | dcls STATIC type IDENT ',' ext_decllist ';'
{
	/* Function-local static MULTI-declarator, plain-first form:
	 * `static int x, y;` / `static jmp_buf a, b;`.  Each declarator
	 * becomes its own mangled file-scope data global. */
	Node *n;
	emit_static_local_scalar_or_instance($3, $4->u.v);
	for (n = $6; n; n = n->r)
		emit_static_local_rest_item($3, n);
}
    | dcls STATIC type IDENT '[' expr ']' ',' ext_decllist ';'
{
	/* Function-local static MULTI-declarator, array-first form:
	 * `static int a[3], b;`. */
	Node *n;
	emit_static_local_sized_array($3, $4->u.v, const_eval($6));
	for (n = $9; n; n = n->r)
		emit_static_local_rest_item($3, n);
}
    | dcls STATIC type IDENT '=' expr ',' ext_decllist ';'
{
	/* Function-local static MULTI-declarator, initialized-first form:
	 * `static int x = 1, y = 2;` / `static char *p = a, *q = b;`.
	 * The first declarator's init folds via emit_static_local_init (the
	 * single `static T v = init;` rule); each rest item (which may itself
	 * carry an initializer captured by ext_decl) via the helper. */
	Node *n;
	emit_static_local_init($3, $4, $6);
	for (n = $8; n; n = n->r)
		emit_static_local_rest_item($3, n);
}
    | dcls EXTERN type IDENT ';'
{
	/* extern declaration inside a function body: declares an external
	 * symbol, no local storage allocation. */
	if ($3 == NIL)
		die("invalid void declaration");
	varaddextern($4->u.v, $3, 0);
}
    | dcls EXTERN type IDENT '(' ')' ';'
{
	/* Local K&R-style extern function decl:  extern char *strncpy(); */
	varadd($4->u.v, 1, FUNC($3), 0);
}
    | dcls EXTERN type ext_decllist ';'
{
	/* Multi-name local extern decl. */
	Node *n;
	unsigned t;
	for (n = $4; n; n = n->r) {
		if (n->op == 'F')
			t = FUNC($3);
		else if (n->op == 'G')
			t = FUNC(IDIR($3));
		else if (n->op == 'H') {
			t = FUNC(IDIR($3));
			fnproto_record(n->u.v, n->l, IDIR($3));
		}
		else if (n->op == 'A' || n->op == 'B' || n->op == 'P')
			t = IDIR($3);
		else
			t = $3;
		varaddextern(n->u.v, t, (n->op == 'A' || n->op == 'B') ? 1 : 0);
		if (n->op == 'B')
			var_set_arraybytes(n->u.v, SIZE($3) * n->l->u.n);
	}
}
    | dcls type IDENT '[' expr ']' ';'
{
	/* Array declaration without initialization.  Dimension is a
	 * constant-expression (const_eval folds sizeof / arithmetic). */
	int s, n, total;
	char *v;

	if ($2 == NIL)
		die("invalid void array");
	n = const_eval($5);  /* array size */
	if (g_td_arraydim > 0) {
		/* Array whose element is an array typedef (`jmp_buf bufs[N]`):
		 * size = N * D * sizeof(elem); register as IDIR(elem) and flag
		 * array-of-array so bufs[i] yields a row address (see mkidx). */
		unsigned elem = g_td_arrayelem;
		int dim = g_td_arraydim;
		v = $3->u.v;
		total = SIZE(elem) * dim * n;
		varadd(v, 0, IDIR(elem), 1);
		var_set_arraybytes(v, total);
		var_set_aoa_dim(v, dim);
		fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(),
			iralign(elem), total);
	} else {
	v = $3->u.v;
	s = SIZE($2);  /* element size */
	total = s * n;
	varadd(v, 0, IDIR($2), 1);  /* Store as pointer to element type - IS AN ARRAY */
	var_set_arraybytes(v, total);
	fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign($2), total);
	}
}
    | dcls type IDENT '[' expr ']' '=' '{' initlist '}' ';'
{
	/* Array declaration with initialization.  Dimension is a
	 * constant-expression (const_eval folds sizeof / arithmetic). */
	int s, n, total;
	char *v;
	int aoa = g_td_arraydim;   /* >0: element is itself an array typedef */

	if ($2 == NIL)
		die("invalid void array");
	v = $3->u.v;
	n = const_eval($5);  /* array size (row count) */
	if (aoa > 0) {
		/* 2-D table `row3_t t[N] = {{…},{…}}`: element is a dim-wide
		 * array typedef, so size = N*dim*sizeof(elem), flag aoa (so a
		 * one-level subscript yields a row address, §7e mkidx), and
		 * flatten the braced rows into per-element stores.  The dcls
		 * context emits at parse time (entry block == lexical order). */
		unsigned elem = g_td_arrayelem;
		int rr;
		Node *ch;
		total = SIZE(elem) * aoa * n;
		varadd(v, 0, IDIR(elem), 1);
		var_set_arraybytes(v, total);
		var_set_aoa_dim(v, aoa);
		fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign(elem), total);
		ch = mk_aoa_array_init(v, $9, aoa, 1, n, &rr);
		if (ch)
			expr(ch);
	} else {
	s = SIZE($2);  /* element size */
	total = s * n;
	varadd(v, 0, IDIR($2), 1);  /* Store as pointer to element type - IS AN ARRAY */
	var_set_arraybytes(v, total);
	fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign($2), total);

	/* Zero-initialize the array first */
	{
		int j;
		for (j = 0; j < n; j++) {
			fprintf(of, "\t%%t%d =w add ", tmp);
			fprintf(of, "%%%s, %d\n", v, j * s);
			fprintf(of, "\tstore%c 0, %%t%d\n", irtyp($2), tmp);
			tmp++;
		}
	}

	/* Generate initialization code with designated initializer support */
	{
		Node *init = $9;
		int i = 0;
		while (init) {
			Node *item = init->l;
			int idx;
			Symb val;

			if (item->op == 'd') {
				/* Designated array initializer: [index] = value */
				idx = item->r->u.n;
				if (idx < 0 || idx >= n)
					die("array index out of bounds in initializer");
				val = expr(item->l);
			} else {
				/* Regular initializer at current index */
				idx = i;
				if (idx >= n)
					die("too many initializers for array");
				val = expr(item);
				i++;  /* Only increment for non-designated */
			}

			fprintf(of, "\t%%t%d =w add ", tmp);
			fprintf(of, "%%%s, %d\n", v, idx * s);
			fprintf(of, "\tstore%c ", irtyp($2));
			psymb(val);
			fprintf(of, ", %%t%d\n", tmp);
			tmp++;
			init = init->r;

			/* After a designated initializer, continue from that index */
			if (item->op == 'd')
				i = idx + 1;
		}
	}
	}
}
    | dcls type IDENT '[' ']' '=' '{' initlist '}' ';'
{
	/* Local UNSIZED array with initializer: `T a[] = { x, y };`.
	 * Element count is inferred from the initializer list (max
	 * designated index + 1, or the sequential item count).  Runtime
	 * values are allowed (each item goes through expr()), unlike the
	 * file-scope `[]` form which folds to static data. */
	int s, n, i;
	char *v;
	Node *it;
	int aoa = g_td_arraydim;   /* >0: element is itself an array typedef */

	if ($2 == NIL)
		die("invalid void array");
	v = $3->u.v;
	if (aoa > 0) {
		/* Unsized 2-D table `row3_t t[] = {{…},{…}}`: row count inferred
		 * from the braced rows; size rows*dim*sizeof(elem), flag aoa,
		 * flatten (no zero-fill — exactly as long as the list). */
		unsigned elem = g_td_arrayelem;
		int rr;
		Node *ch;
		ch = mk_aoa_array_init(v, $8, aoa, 0, 0, &rr);
		varadd(v, 0, IDIR(elem), 1);
		var_set_arraybytes(v, SIZE(elem) * aoa * rr);
		var_set_aoa_dim(v, aoa);
		fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(),
			iralign(elem), SIZE(elem) * aoa * rr);
		if (ch)
			expr(ch);
	} else {
	s = SIZE($2);

	/* First pass: determine element count. */
	n = 0;
	i = 0;
	for (it = $8; it; it = it->r) {
		int idx;
		if (it->l->op == 'd')
			idx = it->l->r->u.n;
		else
			idx = i++;
		if (idx + 1 > n)
			n = idx + 1;
		if (it->l->op == 'd')
			i = idx + 1;
	}

	varadd(v, 0, IDIR($2), 1);
	var_set_arraybytes(v, s * n);
	fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign($2), s * n);

	/* Second pass: store each initializer (no zero-fill needed — an
	 * unsized array is exactly as long as its initializer list). */
	{
		Node *init = $8;
		i = 0;
		while (init) {
			Node *item = init->l;
			int idx;
			Symb val;
			if (item->op == 'd') {
				idx = item->r->u.n;
				val = expr(item->l);
				i = idx + 1;
			} else {
				idx = i++;
				val = expr(item);
			}
			fprintf(of, "\t%%t%d =w add %%%s, %d\n", tmp, v, idx * s);
			fprintf(of, "\tstore%c ", irtyp($2));
			psymb(val);
			fprintf(of, ", %%t%d\n", tmp);
			tmp++;
			init = init->r;
		}
	}
	}
}
    | dcls type IDENT '[' expr ']' '=' STR ';'
{
	/* Function-top SIZED (non-static) char array from a string literal:
	 * `char buf[64] = "hi";`.  Local stack array, initialised at runtime
	 * (literal bytes then zero-filled slack) via a deferred store chain
	 * emit_at parse time (dcls entry block == lexical order). */
	int n, total;
	char *v;
	Node *ch;
	if ($2 == NIL)
		die("invalid void array");
	v = $3->u.v;
	n = const_eval($5);
	total = SIZE($2) * n;
	if (total < strlit_bytelen($8->u.n))
		die("string initializer too long for array");
	varadd(v, 0, IDIR($2), 1);
	var_set_arraybytes(v, total);
	fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign($2), total);
	ch = mk_local_string_init(v, $8->u.n, total);
	if (ch)
		expr(ch);
}
    | dcls type IDENT '[' ']' '=' STR ';'
{
	/* Function-top UNSIZED (non-static) char array from a string literal:
	 * `char buf[] = "hi";`.  Size is the literal length incl NUL. */
	int total;
	char *v;
	Node *ch;
	if ($2 == NIL)
		die("invalid void array");
	v = $3->u.v;
	total = strlit_bytelen($7->u.n);
	varadd(v, 0, IDIR($2), 1);
	var_set_arraybytes(v, total);
	fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign($2), total);
	ch = mk_local_string_init(v, $7->u.n, total);
	if (ch)
		expr(ch);
}
    | dcls type '(' '*' IDENT ')' '(' fptpar0 ')' ';'
{
	/* Function pointer declaration: int (*fptr)(int, int); */
	char *v;
	unsigned fptr_type;

	/* void-returning function pointer (`void (*fp)(...)`) is legal. */
	v = $5->u.v;
	fptr_type = IDIR(FUNC($2));  /* Pointer to function returning type */
	varadd(v, 0, fptr_type, 0);  /* Not an array */
	varsetfpid(v, fpproto_alloc($2, $8));  /* args + float ret (§2q/§5b) */
	fprintf(of, "\t%%%s =%c alloc4 %d\n", v, CODEPTR_T(), CODEPTR_SZ());
}
    | dcls type '(' '*' IDENT ')' '(' fptpar0 ')' '=' expr ';'
{
	/* Function pointer declaration with initializer:
	 *   int (*fptr)(int, int) = adder;  */
	char *v;
	unsigned fptr_type;
	Node *init_node;

	/* void-returning function pointer is legal. */
	v = $5->u.v;
	fptr_type = IDIR(FUNC($2));
	varadd(v, 0, fptr_type, 0);
	varsetfpid(v, fpproto_alloc($2, $8));  /* args + float ret (§2q/§5b) */
	fprintf(of, "\t%%%s =%c alloc4 %d\n", v, CODEPTR_T(), CODEPTR_SZ());
	init_node = mknode('=', $5, $11);
	expr(init_node);
}
    | dcls type '(' '*' IDENT ')' '(' fptpar0 ')' ',' ext_decllist ';'
{
	/* Function pointer + K&R protos in one decl:
	 *   int (*move)(), inc(), dec();
	 * The first item is a fnptr local; subsequent ext_decllist items
	 * are walked with the same uniform-* peeling as plain multi-decls. */
	char *v;
	unsigned fptr_type;
	Node *first;

	/* void-returning function pointer is legal. */
	v = $5->u.v;
	fptr_type = IDIR(FUNC($2));
	varadd(v, 0, fptr_type, 0);
	fprintf(of, "\t%%%s =%c alloc4 %d\n", v, CODEPTR_T(), CODEPTR_SZ());
	(void)first;
	{
		Node *ch = emit_local_multi_decl_full($2, $11);
		if (ch)
			expr(ch);
	}
}
    | dcls STATIC_ASSERT '(' expr ',' STR ')' ';'
{
	/* _Static_assert in local scope (general const-expr condition). */
	if (constfoldable($4) && const_eval($4) == 0)
		die("static assertion failed");
}
    | dcls TYPEDEF type '(' '*' IDENT ')' '(' fptpar0 ')' ';'
{
	/* Function-local typedef of a function-pointer type.  C permits
	 * typedefs in block scope; minic keeps a single global typedef
	 * table, which is fine as long as the name does not clash.  Mirrors
	 * the file-scope fnptr-typedef rule.  Needed by py stream.c. */
	typhadd($6->u.v, IDIR(FUNC($3)));
	typhset_fpid($6->u.v, fpproto_alloc($3, $9));  /* args + float ret (§2s/§5b) */
}
    ;

idlist: IDENT                  { $$ = $1; $$->r = 0; }
      | IDENT ',' idlist       { $$ = $1; $$->r = $3; }
      ;

inititem: expr                        { $$ = $1; }
        | '.' IDENT '=' expr          { $$ = mknode('D', $4, $2); }
        | '[' NUM ']' '=' expr        { $$ = mknode('d', $5, $2); }
        | '{' initlist '}'            { $$ = mknode('{', $2, 0); }
        | '.' IDENT '=' '{' initlist '}' { $$ = mknode('D', mknode('{', $5, 0), $2); }
        ;

initlist: inititem                    { $$ = mknode(0, $1, 0); }
        | inititem ','                { $$ = mknode(0, $1, 0); }
        | inititem ',' initlist       { $$ = mknode(0, $1, $3); }
        ;

gaggr: '{' gilist opt_trailing_comma '}'   { $$ = mknode('{', $2, 0); }
     ;

gilist: gitem                 { $$ = mknode(0, $1, 0); }
      | gilist ',' gitem      { Node *p = $1; while (p->r) p = p->r; p->r = mknode(0, $3, 0); $$ = $1; }
      ;

gitem: gival                  { $$ = $1; }
     | '.' IDENT '=' gival    { $$ = mknode('D', $4, $2); }
     | '[' expr ']' '=' gival { $$ = mknode('d', $5, $2); }
     ;

gival: expr                   { $$ = $1; }
     | gaggr                  { $$ = $1; }
     ;

/* Qualifier cluster that behaves like a lone `volatile` for type
 * purposes: `const volatile T` and `volatile const T` (newlibc MMIO
 * spelling `const volatile uint16_t __far *`) — const adds nothing in
 * minic, volatile drives QVOLATILE exactly as before. */
vol_qual: VOLATILE
        | CONST VOLATILE
        | VOLATILE CONST
        ;

type: type TFAR '*'                  { $$ = IDIR_FAR($1); g_td_arraydim = 0; g_td_fpid = -1; g_decl_volatile = 0; /* forming a pointer consumes any pending pointee-volatile (now in the type bit via IDIR) so the pointer OBJECT stays non-volatile; the trailing-VOLATILE rule re-sets it for the volatile-pointer case. */ }
        | type '*' TFAR              { $$ = IDIR_FAR($1); g_td_arraydim = 0; g_td_fpid = -1; g_decl_volatile = 0; }
        | type '*'                   { $$ = ($1 & FAR) ? IDIR_FAR($1) : IDIR($1); g_td_arraydim = 0; g_td_fpid = -1; g_decl_volatile = 0; }
        | type '*' CONST             { $$ = ($1 & FAR) ? IDIR_FAR($1) : IDIR($1); g_td_arraydim = 0; g_td_fpid = -1; g_decl_volatile = 0; }
        | type '*' VOLATILE          { $$ = ($1 & FAR) ? IDIR_FAR($1) : IDIR($1); g_td_arraydim = 0; g_td_fpid = -1; g_decl_volatile = 1; }
        | TFAR type                  { $$ = $2 | FAR; }
        | TCHAR                      { $$ = CHR; }
    | TSHORT                     { $$ = INT | SHORT; }
    | TINT     { $$ = INT; }
    | TLNG     { $$ = LNG; }
    | TLNGLNG  { $$ = LNG; /* long long aliases to 32-bit long on i8086 */ }
    | TBOOL    { $$ = CHR | UNSIGNED; }
    | TFLOAT   { $$ = INT | FLOAT; }
    | TDOUBLE  { $$ = INT | FLOAT; /* no 8087 / no soft-double on i8086: double aliases to single-precision (Ks) */ }
    | TVOID    { $$ = NIL; }
    | TUNSIGNED TCHAR    { $$ = CHR | UNSIGNED; }
    | TUNSIGNED TSHORT   { $$ = INT | SHORT | UNSIGNED; }
    | TUNSIGNED TINT     { $$ = INT | UNSIGNED; }
    | TUNSIGNED TLNG     { $$ = LNG | UNSIGNED; }
    | TUNSIGNED TLNGLNG  { $$ = LNG | UNSIGNED; }
    | TUNSIGNED          { $$ = INT | UNSIGNED; }
    | TSHORT TINT            { $$ = INT | SHORT; }
    | TLNG TINT              { $$ = LNG; /* long int */ }
    | TLNGLNG TINT           { $$ = LNG; /* long long int */ }
    | TUNSIGNED TSHORT TINT  { $$ = INT | SHORT | UNSIGNED; }
    | TUNSIGNED TLNG TINT    { $$ = LNG | UNSIGNED; }
    | TUNSIGNED TLNGLNG TINT { $$ = LNG | UNSIGNED; /* unsigned long long int */ }
    | TSIGNED                { $$ = INT; /* signed == default signedness */ }
    | TSIGNED TCHAR          { $$ = CHR; }
    | TSIGNED TSHORT         { $$ = INT | SHORT; }
    | TSIGNED TINT           { $$ = INT; }
    | TSIGNED TLNG           { $$ = LNG; }
    | TSIGNED TLNGLNG        { $$ = LNG; }
    | TSIGNED TSHORT TINT    { $$ = INT | SHORT; }
    | TSIGNED TLNG TINT      { $$ = LNG; }
    | TSIGNED TLNGLNG TINT   { $$ = LNG; }
    | CONST TVOID        { $$ = NIL; /* const void (e.g. const void *) */ }
    | CONST TCHAR        { $$ = CHR; }
    | CONST TSHORT       { $$ = INT | SHORT; }
    | CONST TINT         { $$ = INT; }
    | CONST TLNG         { $$ = LNG; }
    | CONST TLNGLNG      { $$ = LNG; }
    | CONST TFLOAT       { $$ = INT | FLOAT; }
    | CONST TDOUBLE      { $$ = INT | FLOAT; /* double aliases to single (Ks), as bare TDOUBLE */ }
    | CONST TUNSIGNED TCHAR    { $$ = CHR | UNSIGNED; }
    | CONST TUNSIGNED TSHORT   { $$ = INT | SHORT | UNSIGNED; }
    | CONST TUNSIGNED TINT     { $$ = INT | UNSIGNED; }
    | CONST TUNSIGNED TLNG     { $$ = LNG | UNSIGNED; }
    | CONST TUNSIGNED TLNGLNG  { $$ = LNG | UNSIGNED; }
    | CONST TUNSIGNED          { $$ = INT | UNSIGNED; }
    | vol_qual TVOID        { $$ = NIL | QVOLATILE; g_decl_volatile = 1; }
    | vol_qual TCHAR        { $$ = CHR | QVOLATILE; g_decl_volatile = 1; }
    | vol_qual TSHORT       { $$ = INT | SHORT | QVOLATILE; g_decl_volatile = 1; }
    | vol_qual TINT         { $$ = INT | QVOLATILE; g_decl_volatile = 1; }
    | vol_qual TLNG         { $$ = LNG | QVOLATILE; g_decl_volatile = 1; }
    | vol_qual TLNGLNG      { $$ = LNG | QVOLATILE; g_decl_volatile = 1; }
    | vol_qual TFLOAT       { $$ = INT | FLOAT | QVOLATILE; g_decl_volatile = 1; }
    | vol_qual TDOUBLE      { $$ = INT | FLOAT | QVOLATILE; g_decl_volatile = 1; }
    | vol_qual TUNSIGNED TCHAR    { $$ = CHR | UNSIGNED | QVOLATILE; g_decl_volatile = 1; }
    | vol_qual TUNSIGNED TSHORT   { $$ = INT | SHORT | UNSIGNED | QVOLATILE; g_decl_volatile = 1; }
    | vol_qual TUNSIGNED TINT     { $$ = INT | UNSIGNED | QVOLATILE; g_decl_volatile = 1; }
    | vol_qual TUNSIGNED TLNG     { $$ = LNG | UNSIGNED | QVOLATILE; g_decl_volatile = 1; }
    | vol_qual TUNSIGNED TLNGLNG  { $$ = LNG | UNSIGNED | QVOLATILE; g_decl_volatile = 1; }
    | vol_qual TUNSIGNED          { $$ = INT | UNSIGNED | QVOLATILE; g_decl_volatile = 1; }
    | CONST TNAME    { $$ = $2; }
    | vol_qual TNAME { $$ = $2 | QVOLATILE; g_decl_volatile = 1; }
    | STRUCT IDENT {
        /* An undefined tag here is an incomplete type — legal when only
         * referenced through a pointer or extern decl (e.g.
         * `extern const struct _mp_obj_str_t foo;`).  Forward-declare it
         * rather than die; structadd completes it if a body arrives. */
        int idx = structfind($2->u.v);
        if (idx < 0)
            idx = structadd_forward($2->u.v, 0);
        $$ = (idx << 3) + STRUCT_T;
    }
    | UNION IDENT {
        int idx = structfind($2->u.v);
        if (idx < 0)
            idx = structadd_forward($2->u.v, 1);
        $$ = (idx << 3) + UNION_T;
    }
    | CONST STRUCT IDENT {
        int idx = structfind($3->u.v);
        if (idx < 0)
            idx = structadd_forward($3->u.v, 0);
        $$ = (idx << 3) + STRUCT_T;
    }
    | vol_qual STRUCT IDENT {
        /* `volatile struct S` — the WHOLE aggregate is volatile, so every
         * member access through it (incl. via a `volatile struct S *p`
         * deref) must be a volatile load/store.  Encode QVOLATILE on the
         * struct type so it rides up through IDIR and back down through
         * DREF (the pointee bit at position 28 shifts to 25); the member-
         * access sites (case '.') OR it onto each member's value type.
         * g_decl_volatile is still set for the direct-var path, and the
         * `type '*'` rule clears it when a pointer is formed (the qualifier
         * then lives in the type bit, pointer OBJECT stays non-volatile). */
        int idx = structfind($3->u.v);
        if (idx < 0)
            idx = structadd_forward($3->u.v, 0);
        $$ = ((idx << 3) + STRUCT_T) | QVOLATILE;
        g_decl_volatile = 1;
    }
    | CONST UNION IDENT {
        int idx = structfind($3->u.v);
        if (idx < 0)
            idx = structadd_forward($3->u.v, 1);
        $$ = (idx << 3) + UNION_T;
    }
    | vol_qual UNION IDENT {
        /* `volatile union U` — see VOLATILE STRUCT above. */
        int idx = structfind($3->u.v);
        if (idx < 0)
            idx = structadd_forward($3->u.v, 1);
        $$ = ((idx << 3) + UNION_T) | QVOLATILE;
        g_decl_volatile = 1;
    }
    | nested_s_begin smembers '}' {
        /* Anonymous struct used directly as a type: `struct { ... }`
         * (in a cast `(struct {…} *)`, a local decl `struct {…} v;`, a
         * typedef `typedef struct {…} T;`, or a struct member `struct {…}
         * name;`).  Shares the nested_s_begin marker (which pushes the
         * enclosing curstruct, or -1 at top level) so there is exactly one
         * reduce action for `STRUCT '{'` — no reduce/reduce conflict. */
        int idx = curstruct;
        structfinish(idx);
        curstruct = structstk[--structstksp];
        $$ = (idx << 3) + STRUCT_T;
    }
    | nested_u_begin smembers '}' {
        /* Anonymous union used directly as a type. */
        int idx = curstruct;
        structfinish(idx);
        curstruct = structstk[--structstksp];
        $$ = (idx << 3) + UNION_T;
    }
    | ENUM IDENT           { $$ = INT; /* enum Tag: an enumeration value is an int */ }
    | CONST ENUM IDENT     { $$ = INT; }
    | VOLATILE ENUM IDENT  { $$ = INT; g_decl_volatile = 1; }
    | TNAME    { $$ = $1; }
    ;

stmt: ';'                            { $$ = 0; }
    | '{' stmts '}'                  { $$ = $2; }
    | BREAK ';'                      { $$ = mkstmt(Break, 0, 0, 0); }
    | CONTINUE ';'                   { $$ = mkstmt(Continue, 0, 0, 0); }
    | RETURN expr ';'                { $$ = mkstmt(Ret, $2, 0, 0); }
    | RETURN ';'                     { $$ = mkstmt(Ret, 0, 0, 0); }
    | GOTO IDENT ';'                 { Stmt *s = mkstmt(Goto, 0, 0, 0); strcpy(s->label, $2->u.v); $$ = s; }
    | IDENT ':' stmt                 { Stmt *s = mkstmt(Label, $3, 0, 0); strcpy(s->label, $1->u.v); $$ = s; }
    | enumstart enums '}' ';'        {
        /* Block-scoped (inner-block) anonymous/tagged enum declaration:
         * introduces only the constants (registered by the enums rule),
         * no storage.  Mirrors the dcls-level and file-scope edcl rules. */
        $$ = 0;
    }
    | type IDENT ';'                 {
        int s;
        char *v;
        if ($1 == NIL)
            die("invalid void declaration");
        v = block_scope_decl($2, $1, 0);
        s = SIZE($1);
        varadd(v, 0, $1, 0);
        emit_local_alloc(v, ALLOC_T(), iralign($1), s);
        $$ = 0;
    }
    | type IDENT '=' expr ';'        {
        /* Block-scoped variable with initializer.  alloc happens at
         * parse time (function entry, like all locals), but the
         * initializer assignment must run only when control flow
         * reaches the declaration — not unconditionally at the start
         * of the function.  Wrap the assignment as an Expr Stmt so
         * stmt() emits it in lexical order. */
        int s;
        char *v;
        Node *init_node;
        if ($1 == NIL)
            die("invalid void declaration");
        v = block_scope_decl($2, $1, 0);
        s = SIZE($1);
        varadd(v, 0, $1, 0);
        emit_local_alloc(v, ALLOC_T(), iralign($1), s);
        init_node = mknode('=', $2, $4);
        $$ = mkstmt(Expr, init_node, 0, 0);
    }
    | type IDENT '=' '{' initlist '}' ';'  {
        /* Block-scoped local aggregate initializer:
         * `struct P p = { 1, 2 };` — same desugaring as the dcls-context
         * rule (declare + compound-literal assignment), but the
         * assignment is wrapped as an Expr stmt so it runs in lexical
         * order rather than at function entry. */
        int s;
        char *v;
        Node *clit_node, *init_node;
        if ($1 == NIL)
            die("invalid void declaration");
        v = block_scope_decl($2, $1, 0);
        s = SIZE($1);
        varadd(v, 0, $1, 0);
        fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign($1), s);
        if (initlist_is_zero($5) &&
            (KIND($1) == STRUCT_T || KIND($1) == UNION_T)) {
            /* `S s = {0};` mid-block — defer a `memset(&s, 0, s)` (no
             * compound-literal temp, no struct copy).  Deferred (not a direct
             * emit) so it re-zeroes on each loop re-entry, like the general
             * initializer path. */
            Node *vnode = mknode('V', 0, 0);
            Node *addr, *zero, *size, *callee, *args;
            strcpy(vnode->u.v, v);
            addr = mknode('A', vnode, 0);
            zero = mknode('N', 0, 0); zero->u.n = 0;
            size = mknode('N', 0, 0); size->u.n = s;
            /* call args are wrapper nodes: mknode(0, expr, next) */
            args = mknode(0, addr, mknode(0, zero, mknode(0, size, 0)));
            callee = mknode('V', 0, 0);
            strcpy(callee->u.v, "memset");
            $$ = mkstmt(Expr, mknode('C', callee, args), 0, 0);
        } else {
            clit_node = mknode('L', $5, 0);
            clit_node->u.n = (int)$1;
            init_node = mknode('=', $2, clit_node);
            $$ = mkstmt(Expr, init_node, 0, 0);
        }
    }
    | type IDENT '[' expr ']' ';'     {
        /* Block-scoped fixed-size array.  Dimension is a
         * constant-expression (const_eval folds sizeof / arithmetic).
         * Must mirror the dcls-context rule's use of expr so a bare
         * NUM dim doesn't shift/reduce-conflict between the two. */
        int s, n, total;
        char *v;
        if ($1 == NIL)
            die("invalid void array");
        v = block_scope_decl($2, IDIR($1), 1);
        n = const_eval($4);
        s = SIZE($1);
        total = s * n;
        varadd(v, 0, IDIR($1), 1);
        var_set_arraybytes(v, total);
        fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign($1), total);
        $$ = 0;
    }
    | type IDENT '[' expr ']' '=' '{' initlist '}' ';' {
        /* Block-scoped sized array with initializer.  Stores are
         * deferred (mkstmt Expr) so they run in control-flow order;
         * the array is zero-filled then initialized (partial-init
         * semantics). */
        int n, total;
        char *v;
        Node *chain;
        int aoa = g_td_arraydim;   /* >0: element is an array typedef */
        unsigned elem = aoa > 0 ? g_td_arrayelem : $1;
        if ($1 == NIL)
            die("invalid void array");
        v = block_scope_decl($2, IDIR(elem), 1);
        n = const_eval($4);
        total = SIZE(elem) * n * (aoa > 0 ? aoa : 1);
        varadd(v, 0, IDIR(elem), 1);
        var_set_arraybytes(v, total);
        fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign(elem), total);
        if (aoa > 0) {
            /* 2-D table `row3_t t[N] = {{…},{…}}` mid-block: flatten braced
             * rows (deferred Expr stmt → control-flow order, §7e mkidx). */
            int rr;
            var_set_aoa_dim(v, aoa);
            chain = mk_aoa_array_init(v, $8, aoa, 1, n, &rr);
        } else {
            chain = mk_local_array_init(v, $8, 1, n, &n);
        }
        $$ = mkstmt(Expr, chain, 0, 0);
    }
    | type IDENT '[' ']' '=' '{' initlist '}' ';' {
        /* Block-scoped UNSIZED array with initializer: count inferred
         * from the list (no zero-fill — length is exact). */
        int n;
        char *v;
        Node *chain;
        int aoa = g_td_arraydim;   /* >0: element is an array typedef */
        unsigned elem = aoa > 0 ? g_td_arrayelem : $1;
        if ($1 == NIL)
            die("invalid void array");
        v = block_scope_decl($2, IDIR(elem), 1);
        if (aoa > 0) {
            /* Unsized 2-D table `row3_t t[] = {{…},{…}}` mid-block: rows
             * inferred from the braced list. */
            int rr;
            chain = mk_aoa_array_init(v, $7, aoa, 0, 0, &rr);
            varadd(v, 0, IDIR(elem), 1);
            var_set_arraybytes(v, SIZE(elem) * aoa * rr);
            var_set_aoa_dim(v, aoa);
            fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(),
                iralign(elem), SIZE(elem) * aoa * rr);
        } else {
            chain = mk_local_array_init(v, $7, 0, 0, &n);
            varadd(v, 0, IDIR($1), 1);
            var_set_arraybytes(v, SIZE($1) * n);
            fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign($1), SIZE($1) * n);
        }
        $$ = mkstmt(Expr, chain, 0, 0);
    }
    | type IDENT '[' expr ']' '=' STR ';' {
        /* Block-scoped SIZED (non-static) char array from a string literal:
         * mid-block `char buf[N] = "hi";`.  Stack array; the store chain is
         * deferred as an Expr stmt so it re-runs on each block re-entry. */
        int n, total;
        char *v;
        Node *chain;
        if ($1 == NIL)
            die("invalid void array");
        v = block_scope_decl($2, IDIR($1), 1);
        n = const_eval($4);
        total = SIZE($1) * n;
        if (total < strlit_bytelen($7->u.n))
            die("string initializer too long for array");
        varadd(v, 0, IDIR($1), 1);
        var_set_arraybytes(v, total);
        fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign($1), total);
        chain = mk_local_string_init(v, $7->u.n, total);
        $$ = mkstmt(Expr, chain, 0, 0);
    }
    | type IDENT '[' ']' '=' STR ';' {
        /* Block-scoped UNSIZED (non-static) char array from a string literal:
         * mid-block `char buf[] = "hi";`.  Size is the literal length, NUL
         * included; deferred store chain (control-flow order). */
        int total;
        char *v;
        Node *chain;
        if ($1 == NIL)
            die("invalid void array");
        v = block_scope_decl($2, IDIR($1), 1);
        total = strlit_bytelen($6->u.n);
        varadd(v, 0, IDIR($1), 1);
        var_set_arraybytes(v, total);
        fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign($1), total);
        chain = mk_local_string_init(v, $6->u.n, total);
        $$ = mkstmt(Expr, chain, 0, 0);
    }
    | type IDENT '[' expr ']' ',' ext_decllist ';' {
        /* Block-scoped multi-declarator whose FIRST declarator is a sized
         * array: `int arr[3], *counter;`.  Mirrors the dcls-context
         * array-first rule (kr_array_node builds a 'B' node for the array,
         * emit_local_multi_decl_full handles every declarator including
         * the array and the shadow rename), but DEFERS the initializer
         * chain as an Expr stmt so it runs in control-flow order — the
         * stmt-context multi-decl convention (see the plain rule below). */
        Node *first = kr_array_node($2->u.v, const_eval($4));
        Node *ch;
        first->r = $7;
        ch = emit_local_multi_decl_full($1, first);
        $$ = ch ? mkstmt(Expr, ch, 0, 0) : 0;
    }
    | type IDENT ',' ext_decllist ';' {
        /* Block-scoped multi-variable decl with full per-declarator
         * decoration support (`*`, `[]`, `[N]`, `()`).  Reuses the
         * same helper as the dcls-context multi-decl rule, but DEFERS
         * the initializer chain as an Expr stmt so it runs in
         * control-flow order — `T k, nf = 0;` in a loop body must
         * re-init nf each iteration, not once at function entry. */
        Node *ch;
        ch = emit_local_multi_decl($1, $2, $4);
        $$ = ch ? mkstmt(Expr, ch, 0, 0) : 0;
    }
    | type IDENT '=' expr ',' init_decllist ';' {
        /* Block-scoped multi-declarator where the FIRST declarator has
         * an initializer: `int a = 1, b = 2;` inside a block (the dcls
         * rule covers function top).  Allocs at parse time; ALL inits
         * chained into one deferred Expr stmt, in source order. */
        Node *chain, *n;
        char *v;
        if ($1 == NIL)
            die("invalid void declaration");
        v = block_scope_decl($2, $1, 0);
        varadd(v, 0, $1, 0);
        emit_local_alloc(v, ALLOC_T(), iralign($1), SIZE($1));
        chain = mknode('=', $2, $4);
        for (n = $6; n; n = n->r) {
            char *nv = block_scope_decl(n, $1, 0);
            varadd(nv, 0, $1, 0);
            emit_local_alloc(nv, ALLOC_T(), iralign($1), SIZE($1));
            if (n->l)
                chain = mknode(',', chain,
                    multi_decl_chain_init(0, nv, n->l));
        }
        $$ = mkstmt(Expr, chain, 0, 0);
    }
    | type '(' '*' IDENT ')' '(' fptpar0 ')' ';' {
        /* Block-scoped function pointer: `int (*fp)(int, int);` */
        char *v;
        unsigned fptr_type;
        /* void-returning function pointer is legal. */
        v = $4->u.v;
        fptr_type = IDIR(FUNC($1));
        varadd(v, 0, fptr_type, 0);
        fprintf(of, "\t%%%s =%c alloc4 %d\n", v, CODEPTR_T(), CODEPTR_SZ());
        $$ = 0;
    }
    | type '(' '*' IDENT ')' '(' fptpar0 ')' '=' expr ';' {
        /* Block-scoped fn-ptr with initializer:
         *   int (*fp)(int, int) = adder; */
        char *v;
        unsigned fptr_type;
        Node *init_node;
        /* void-returning function pointer is legal. */
        v = $4->u.v;
        fptr_type = IDIR(FUNC($1));
        varadd(v, 0, fptr_type, 0);
        fprintf(of, "\t%%%s =%c alloc4 %d\n", v, CODEPTR_T(), CODEPTR_SZ());
        init_node = mknode('=', $4, $10);
        $$ = mkstmt(Expr, init_node, 0, 0);
    }
    | STATIC type IDENT ';'          {
        /* Statement-scope `static T var;` — same treatment as the
         * top-of-block form: emit as mangled file-scope data global. */
        char buf[64];
        if ($2 == NIL)
            die("invalid void declaration");
        emit_zero_init(buf, $2);
        emit_static_local($3->u.v, $2, 0, buf);
        $$ = 0;
    }
    | STATIC type IDENT '=' expr ';' {
        /* Statement-scope static scalar with initializer. */
        emit_static_local_init($2, $3, $5);
        $$ = 0;
    }
    | STATIC type IDENT '[' ']' '=' gaggr ';' {
        /* Statement-scope unsized static array with initializer, via
         * the aggregate machinery into a mangled file-scope data
         * global.  Needed for in-block static lookup tables such as
         * py obj.c const pointer-to-type tables. */
        emit_static_array($2, $3->u.v, -1, $7);
        $$ = 0;
    }
    | STATIC type IDENT '[' expr ']' '=' gaggr ';' {
        /* Statement-scope sized static array with initializer. */
        emit_static_array($2, $3->u.v, const_eval($5), $8);
        $$ = 0;
    }
    | STATIC type IDENT '[' ']' '=' STR ';' {
        /* Statement-scope static char array from a string literal. */
        if ($2 == NIL)
            die("invalid void array");
        emit_string_array($2, $3->u.v, $7->u.n, 1);
        $$ = 0;
    }
    | STATIC type IDENT '[' expr ']' '=' STR ';' {
        /* Statement-scope SIZED static char array from a string literal:
         * mid-block `static char cwd[64] = "/";` (the §8v file-scope
         * production, static_local form). */
        if ($2 == NIL)
            die("invalid void array");
        emit_string_array_sized($2, $3->u.v, $8->u.n, const_eval($5), 1);
        $$ = 0;
    }
    | STATIC type IDENT '[' expr ']' ';' {
        /* Statement-scope uninitialized sized static array. */
        char buf[64];
        int total;
        unsigned elemtyp = $2;
        int aoa = 0;
        if ($2 == NIL)
            die("invalid void array");
        /* Array-of-array-typedef static local (`static jmp_buf bufs[N]`):
         * element byte size is D*sizeof(elem); register IDIR(elem) and flag
         * aoa so bufs[i] is a row address (see mkidx). */
        if (g_td_arraydim > 0) {
            aoa = g_td_arraydim;
            elemtyp = g_td_arrayelem;
        }
        total = (aoa > 0 ? SIZE(elemtyp) * aoa : SIZE($2)) * const_eval($5);
        sprintf(buf, "align %d { z %d }", iralign(elemtyp), total);
        emit_static_local($3->u.v, IDIR(elemtyp), 1, buf);
        var_set_arraybytes($3->u.v, total);
        if (aoa > 0)
            var_set_aoa_dim($3->u.v, aoa);
        maybe_mark_huge_global(nglo - 1, gloname[nglo - 1], total);
        $$ = 0;
    }
    | STATIC type IDENT ',' ext_decllist ';' {
        /* Statement-scope static MULTI-declarator, plain-first form. */
        Node *n;
        emit_static_local_scalar_or_instance($2, $3->u.v);
        for (n = $5; n; n = n->r)
            emit_static_local_rest_item($2, n);
        $$ = 0;
    }
    | STATIC type IDENT '[' expr ']' ',' ext_decllist ';' {
        /* Statement-scope static MULTI-declarator, array-first form. */
        Node *n;
        emit_static_local_sized_array($2, $3->u.v, const_eval($5));
        for (n = $8; n; n = n->r)
            emit_static_local_rest_item($2, n);
        $$ = 0;
    }
    | STATIC type IDENT '=' expr ',' ext_decllist ';' {
        /* Statement-scope static MULTI-declarator, initialized-first form:
         * `static int x = 1, y = 2;`.  Mirrors the dcls-scope rule. */
        Node *n;
        emit_static_local_init($2, $3, $5);
        for (n = $7; n; n = n->r)
            emit_static_local_rest_item($2, n);
        $$ = 0;
    }
    | EXTERN type IDENT ';'          {
        /* extern in statement scope: register as external symbol, no alloc. */
        if ($2 == NIL)
            die("invalid void declaration");
        varaddextern($3->u.v, $2, 0);
        $$ = 0;
    }
    | STATIC_ASSERT '(' expr ',' STR ')' ';' {
        /* _Static_assert in statement scope (general const-expr). */
        if (constfoldable($3) && const_eval($3) == 0)
            die("static assertion failed");
        $$ = 0;
    }
    | expr ';'                       { $$ = mkstmt(Expr, $1, 0, 0); }
    | WHILE '(' expr ')' stmt        { $$ = mkstmt(While, $3, $5, 0); }
    | DO stmt WHILE '(' expr ')' ';' { $$ = mkstmt(DoWhile, $2, $5, 0); }
    | IF '(' expr ')' stmt ELSE stmt { $$ = mkstmt(If, $3, $5, $7); }
    | IF '(' expr ')' stmt           { $$ = mkstmt(If, $3, $5, 0); }
    | FOR '(' comma_exp0 ';' comma_exp0 ';' comma_exp0 ')' stmt
                                     { $$ = mkfor($3, $5, $7, $9); }
    | FOR '(' forinit_var expr ';' comma_exp0 ';' comma_exp0 ')' stmt
                                     {
        /* C99 for-init.  Test and increment use comma_exp0 (matching the
         * plain `for`) so a comma increment works, e.g.
         *   for (size_t i = n; i > 0; i--, ptrs++)
         * in py/gc.c and py/bc.c.  forinit_var did the rename, varadd and
         * alloc for the first declarator (reducing at type IDENT =,
         * before the test clause is lexed, so a sibling-for collision
         * such as compile.c with for int i then for size_t i gets its
         * uses stamped to the renamed slot). */
        Node *init_expr;
        init_expr = mknode('=', $3, $4);
        $$ = mkfor(init_expr, $6, $8, $10);
    }
    | FOR '(' forinit_var expr ',' '*' IDENT '=' expr ';' comma_exp0 ';' comma_exp0 ')' stmt
                                     {
        /* Two-pointer-declarator C99 for-init: the symmetric
         * `for (T *a = e1, *b = e2; ...)` form.  The first star is
         * folded into the base type, so both vars share forinit_basetyp.
         * Inits run left-to-right via a comma node.  Covers the
         * MicroPython spelling in py objstr.c and py qstr.c. */
        int s;
        Node *i2, *ini;
        s = SIZE(forinit_basetyp);
        varadd($7->u.v, 0, forinit_basetyp, 0);
        fprintf(of, "\t%%%s =%c alloc%d %d\n", $7->u.v, ALLOC_T(), iralign(forinit_basetyp), s);
        i2 = mknode('=', $7, $9);
        ini = mknode(',', mknode('=', $3, $4), i2);
        $$ = mkfor(ini, $11, $13, $15);
    }
    | FOR '(' forinit_var expr ',' init_decllist ';' comma_exp0 ';' comma_exp0 ')' stmt
                                     {
        /* Multi-scalar-declarator C99 for-init sharing one base type:
         *   for (size_t block = 0, len = 0, len_free = 0; !finish;)
         * in py gc.c.  Allocate each declarator now (like every local);
         * chain the initializers into one comma-expression that mkfor
         * runs at loop entry.  Distinguished from the two-pointer form
         * by the token after the comma (IDENT here vs a star there). */
        int s;
        Node *ini, *id, *n;
        s = SIZE(forinit_basetyp);
        ini = mknode('=', $3, $4);
        for (n = $6; n; n = n->r) {
            varadd(n->u.v, 0, forinit_basetyp, 0);
            fprintf(of, "\t%%%s =%c alloc%d %d\n", n->u.v, ALLOC_T(), iralign(forinit_basetyp), s);
            if (n->l) {
                id = mknode('V', 0, 0);
                strcpy(id->u.v, n->u.v);
                ini = mknode(',', ini, mknode('=', id, n->l));
            }
        }
        $$ = mkfor(ini, $8, $10, $12);
    }
    | SWITCH '(' expr ')' stmt       { $$ = mkstmt(Switch, $3, $5, 0); }
    | CASE pref ':' stmt             { Stmt *s = mkstmt(Case, 0, $4, 0); s->val = const_eval($2); $$ = s; }
    | CASE pref '+' pref ':' stmt    { Stmt *s = mkstmt(Case, 0, $6, 0); s->val = const_eval($2) + const_eval($4); $$ = s; }
    | CASE pref '-' pref ':' stmt    { Stmt *s = mkstmt(Case, 0, $6, 0); s->val = const_eval($2) - const_eval($4); $$ = s; }
    | CASE pref '*' pref ':' stmt    { Stmt *s = mkstmt(Case, 0, $6, 0); s->val = const_eval($2) * const_eval($4); $$ = s; }
    | CASE pref '|' pref ':' stmt    { Stmt *s = mkstmt(Case, 0, $6, 0); s->val = const_eval($2) | const_eval($4); $$ = s; }
    | CASE pref '&' pref ':' stmt    { Stmt *s = mkstmt(Case, 0, $6, 0); s->val = const_eval($2) & const_eval($4); $$ = s; }
    | CASE pref SHL pref ':' stmt    { Stmt *s = mkstmt(Case, 0, $6, 0); s->val = const_eval($2) << const_eval($4); $$ = s; }
    | CASE pref SHR pref ':' stmt    { Stmt *s = mkstmt(Case, 0, $6, 0); s->val = const_eval($2) >> const_eval($4); $$ = s; }
    | DEFAULT ':' stmt               { $$ = mkstmt(Default, 0, $3, 0); }
    | asmstmt                        { $$ = $1; }
    ;

forinit_var: type IDENT '='
{
    /* First declarator of a C99 for-init.  Factored out as its own
     * reduction so the rename, varadd and alloc run at type IDENT =,
     * which (since all three for-init rules share this prefix) is a
     * single-action state miniyacc default-reduces WITHOUT lexing
     * lookahead, so the rename binding is established before the test
     * and increment clauses are lexed, letting the lexer stamp their
     * uses to the renamed slot.  This is the for-init analogue of the
     * stmt-level block_scope_decl wiring; it closes the sibling
     * for-loop double-definition in compile.c.  The base type is
     * stashed in forinit_basetyp for any later declarators in the
     * two-pointer or multi-scalar forms. */
    char *v;
    int s;
    if ($1 == NIL)
        die("invalid void declaration");
    forinit_basetyp = $1;
    v = block_scope_decl($2, $1, 0);
    s = SIZE($1);
    varadd(v, 0, $1, 0);
    emit_local_alloc(v, ALLOC_T(), iralign($1), s);
    $$ = $2;
}
    ;

asmstmt: ASM '(' STR ')' ';' {
        /* Simple inline assembly: asm("code"); */
        struct AsmStmt *a = alloc(sizeof *a);
        Stmt *s = mkstmt(Asm, a, 0, 0);
        getstrlit($3->u.n, a->code, sizeof(a->code));
        a->noutputs = 0;
        a->ninputs = 0;
        a->nclobbers = 0;
        a->isvolatile = 0;
        $$ = s;
    }
    | ASM VOLATILE '(' STR ')' ';' {
        /* Volatile inline assembly: asm volatile("code"); */
        struct AsmStmt *a = alloc(sizeof *a);
        Stmt *s = mkstmt(Asm, a, 0, 0);
        getstrlit($4->u.n, a->code, sizeof(a->code));
        a->noutputs = 0;
        a->ninputs = 0;
        a->nclobbers = 0;
        a->isvolatile = 1;
        $$ = s;
    }
    | ASM '(' STR ':' asmoutputs ')' ';' {
        /* Inline assembly with outputs: asm("code" : outputs); */
        struct AsmStmt *a = (struct AsmStmt *)$5;
        Stmt *s = mkstmt(Asm, a, 0, 0);
        getstrlit($3->u.n, a->code, sizeof(a->code));
        a->isvolatile = 0;
        $$ = s;
    }
    | ASM VOLATILE '(' STR ':' asmoutputs ')' ';' {
        /* Volatile inline assembly with outputs: asm volatile("code" : outputs); */
        struct AsmStmt *a = (struct AsmStmt *)$6;
        Stmt *s = mkstmt(Asm, a, 0, 0);
        getstrlit($4->u.n, a->code, sizeof(a->code));
        a->isvolatile = 1;
        $$ = s;
    }
    | ASM '(' STR ':' asmoutputs ':' asminputs ')' ';' {
        /* Inline assembly with outputs and inputs: asm("code" : outputs : inputs); */
        struct AsmStmt *a = (struct AsmStmt *)$5;
        struct AsmStmt *ainputs = (struct AsmStmt *)$7;
        Stmt *s;
        /* Merge inputs into a */
        a->ninputs = ainputs->ninputs;
        for (int i = 0; i < ainputs->ninputs; i++) {
            a->inputs[i] = ainputs->inputs[i];
        }
        getstrlit($3->u.n, a->code, sizeof(a->code));
        a->isvolatile = 0;
        s = mkstmt(Asm, a, 0, 0);
        $$ = s;
    }
    | ASM VOLATILE '(' STR ':' asmoutputs ':' asminputs ')' ';' {
        /* Volatile inline assembly with outputs and inputs */
        struct AsmStmt *a = (struct AsmStmt *)$6;
        struct AsmStmt *ainputs = (struct AsmStmt *)$8;
        Stmt *s;
        /* Merge inputs into a */
        a->ninputs = ainputs->ninputs;
        for (int i = 0; i < ainputs->ninputs; i++) {
            a->inputs[i] = ainputs->inputs[i];
        }
        getstrlit($4->u.n, a->code, sizeof(a->code));
        a->isvolatile = 1;
        s = mkstmt(Asm, a, 0, 0);
        $$ = s;
    }
    | ASM '(' STR ':' ':' asminputs ')' ';' {
        /* Inline assembly with inputs only: asm("code" :: inputs); */
        struct AsmStmt *a = (struct AsmStmt *)$6;
        Stmt *s = mkstmt(Asm, a, 0, 0);
        getstrlit($3->u.n, a->code, sizeof(a->code));
        a->noutputs = 0;
        a->isvolatile = 0;
        $$ = s;
    }
    | ASM VOLATILE '(' STR ':' ':' asminputs ')' ';' {
        /* Volatile inline assembly with inputs only: asm volatile("code" :: inputs); */
        struct AsmStmt *a = (struct AsmStmt *)$7;
        Stmt *s = mkstmt(Asm, a, 0, 0);
        getstrlit($4->u.n, a->code, sizeof(a->code));
        a->noutputs = 0;
        a->isvolatile = 1;
        $$ = s;
    }
    | ASM '(' STR ':' asmoutputs ':' asminputs ':' asmclobbers ')' ';' {
        /* Inline assembly with outputs, inputs, and clobbers: asm("code" : outputs : inputs : clobbers); */
        struct AsmStmt *a = (struct AsmStmt *)$5;
        struct AsmStmt *ainputs = (struct AsmStmt *)$7;
        struct AsmStmt *aclobbers = (struct AsmStmt *)$9;
        Stmt *s;
        /* Merge inputs into a */
        a->ninputs = ainputs->ninputs;
        for (int i = 0; i < ainputs->ninputs; i++) {
            a->inputs[i] = ainputs->inputs[i];
        }
        /* Merge clobbers into a */
        a->nclobbers = aclobbers->nclobbers;
        for (int i = 0; i < aclobbers->nclobbers; i++) {
            strcpy(a->clobbers[i], aclobbers->clobbers[i]);
        }
        getstrlit($3->u.n, a->code, sizeof(a->code));
        a->isvolatile = 0;
        s = mkstmt(Asm, a, 0, 0);
        $$ = s;
    }
    | ASM VOLATILE '(' STR ':' asmoutputs ':' asminputs ':' asmclobbers ')' ';' {
        /* Volatile inline assembly with outputs, inputs, and clobbers */
        struct AsmStmt *a = (struct AsmStmt *)$6;
        struct AsmStmt *ainputs = (struct AsmStmt *)$8;
        struct AsmStmt *aclobbers = (struct AsmStmt *)$10;
        Stmt *s;
        /* Merge inputs into a */
        a->ninputs = ainputs->ninputs;
        for (int i = 0; i < ainputs->ninputs; i++) {
            a->inputs[i] = ainputs->inputs[i];
        }
        /* Merge clobbers into a */
        a->nclobbers = aclobbers->nclobbers;
        for (int i = 0; i < aclobbers->nclobbers; i++) {
            strcpy(a->clobbers[i], aclobbers->clobbers[i]);
        }
        getstrlit($4->u.n, a->code, sizeof(a->code));
        a->isvolatile = 1;
        s = mkstmt(Asm, a, 0, 0);
        $$ = s;
    }
    | ASM '(' STR ':' ':' asminputs ':' asmclobbers ')' ';' {
        /* Inline assembly with inputs and clobbers only: asm("code" :: inputs : clobbers); */
        struct AsmStmt *a = (struct AsmStmt *)$6;
        struct AsmStmt *aclobbers = (struct AsmStmt *)$8;
        Stmt *s;
        /* Merge clobbers into a */
        a->nclobbers = aclobbers->nclobbers;
        for (int i = 0; i < aclobbers->nclobbers; i++) {
            strcpy(a->clobbers[i], aclobbers->clobbers[i]);
        }
        getstrlit($3->u.n, a->code, sizeof(a->code));
        a->noutputs = 0;
        a->isvolatile = 0;
        s = mkstmt(Asm, a, 0, 0);
        $$ = s;
    }
    | ASM VOLATILE '(' STR ':' ':' asminputs ':' asmclobbers ')' ';' {
        /* Volatile inline assembly with inputs and clobbers only */
        struct AsmStmt *a = (struct AsmStmt *)$7;
        struct AsmStmt *aclobbers = (struct AsmStmt *)$9;
        Stmt *s;
        /* Merge clobbers into a */
        a->nclobbers = aclobbers->nclobbers;
        for (int i = 0; i < aclobbers->nclobbers; i++) {
            strcpy(a->clobbers[i], aclobbers->clobbers[i]);
        }
        getstrlit($4->u.n, a->code, sizeof(a->code));
        a->noutputs = 0;
        a->isvolatile = 1;
        s = mkstmt(Asm, a, 0, 0);
        $$ = s;
    }
    | ASM '(' STR ':' asmoutputs ':' ':' asmclobbers ')' ';' {
        /* Inline assembly with outputs and clobbers only: asm("code" : outputs : : clobbers); */
        struct AsmStmt *a = (struct AsmStmt *)$5;
        struct AsmStmt *aclobbers = (struct AsmStmt *)$8;
        Stmt *s;
        /* Merge clobbers into a */
        a->nclobbers = aclobbers->nclobbers;
        for (int i = 0; i < aclobbers->nclobbers; i++) {
            strcpy(a->clobbers[i], aclobbers->clobbers[i]);
        }
        getstrlit($3->u.n, a->code, sizeof(a->code));
        a->isvolatile = 0;
        s = mkstmt(Asm, a, 0, 0);
        $$ = s;
    }
    | ASM VOLATILE '(' STR ':' asmoutputs ':' ':' asmclobbers ')' ';' {
        /* Volatile inline assembly with outputs and clobbers only */
        struct AsmStmt *a = (struct AsmStmt *)$6;
        struct AsmStmt *aclobbers = (struct AsmStmt *)$9;
        Stmt *s;
        /* Merge clobbers into a */
        a->nclobbers = aclobbers->nclobbers;
        for (int i = 0; i < aclobbers->nclobbers; i++) {
            strcpy(a->clobbers[i], aclobbers->clobbers[i]);
        }
        getstrlit($4->u.n, a->code, sizeof(a->code));
        a->isvolatile = 1;
        s = mkstmt(Asm, a, 0, 0);
        $$ = s;
    }
    | ASM '(' STR ':' ':' ':' asmclobbers ')' ';' {
        /* Inline assembly with clobbers only: asm("code" ::: clobbers); */
        struct AsmStmt *a = (struct AsmStmt *)$7;
        Stmt *s;
        getstrlit($3->u.n, a->code, sizeof(a->code));
        a->noutputs = 0;
        a->ninputs = 0;
        a->isvolatile = 0;
        s = mkstmt(Asm, a, 0, 0);
        $$ = s;
    }
    | ASM VOLATILE '(' STR ':' ':' ':' asmclobbers ')' ';' {
        /* Volatile inline assembly with clobbers only: asm volatile("code" ::: clobbers); */
        struct AsmStmt *a = (struct AsmStmt *)$8;
        Stmt *s;
        getstrlit($4->u.n, a->code, sizeof(a->code));
        a->noutputs = 0;
        a->ninputs = 0;
        a->isvolatile = 1;
        s = mkstmt(Asm, a, 0, 0);
        $$ = s;
    }
    ;

asmoutputs:
        {
        struct AsmStmt *a = alloc(sizeof *a);
        a->noutputs = 0;
        a->ninputs = 0;
        a->nclobbers = 0;
        $$ = (Node *)a;
    }
    | asmoutputlist {
        $$ = $1;
    }
    ;

asmoutputlist: asmoutput {
        $$ = $1;
    }
    | asmoutputlist ',' asmoutput {
        struct AsmStmt *a = (struct AsmStmt *)$1;
        struct AsmStmt *b = (struct AsmStmt *)$3;
        if (a->noutputs < 4) {
            a->outputs[a->noutputs] = b->outputs[0];
            a->noutputs++;
        }
        $$ = (Node *)a;
    }
    ;

asmoutput: STR '(' expr ')' {
        struct AsmStmt *a = alloc(sizeof *a);
        a->noutputs = 1;
        a->ninputs = 0;
        a->nclobbers = 0;
        getstrlit($1->u.n, a->outputs[0].constraint, NString);
        a->outputs[0].expr = $3;
        $$ = (Node *)a;
    }
    ;

asminputs:
        {
        struct AsmStmt *a = alloc(sizeof *a);
        a->noutputs = 0;
        a->ninputs = 0;
        a->nclobbers = 0;
        $$ = (Node *)a;
    }
    | asminputlist {
        $$ = $1;
    }
    ;

asminputlist: asminput {
        $$ = $1;
    }
    | asminputlist ',' asminput {
        struct AsmStmt *a = (struct AsmStmt *)$1;
        struct AsmStmt *b = (struct AsmStmt *)$3;
        if (a->ninputs < 4) {
            a->inputs[a->ninputs] = b->inputs[0];
            a->ninputs++;
        }
        $$ = (Node *)a;
    }
    ;

asminput: STR '(' expr ')' {
        struct AsmStmt *a = alloc(sizeof *a);
        a->noutputs = 0;
        a->ninputs = 1;
        a->nclobbers = 0;
        getstrlit($1->u.n, a->inputs[0].constraint, NString);
        a->inputs[0].expr = $3;
        $$ = (Node *)a;
    }
    ;

asmclobbers:
        {
        struct AsmStmt *a = alloc(sizeof *a);
        a->noutputs = 0;
        a->ninputs = 0;
        a->nclobbers = 0;
        $$ = (Node *)a;
    }
    | asmclobberlist {
        $$ = $1;
    }
    ;

asmclobberlist: STR {
        struct AsmStmt *a = alloc(sizeof *a);
        a->noutputs = 0;
        a->ninputs = 0;
        a->nclobbers = 1;
        getstrlit($1->u.n, a->clobbers[0], NString);
        $$ = (Node *)a;
    }
    | asmclobberlist ',' STR {
        struct AsmStmt *a = (struct AsmStmt *)$1;
        if (a->nclobbers < 8) {
            getstrlit($3->u.n, a->clobbers[a->nclobbers], NString);
            a->nclobbers++;
        }
        $$ = (Node *)a;
    }
    ;

stmts: stmts stmt { $$ = mkstmt(Seq, $1, $2, 0); }
     |            { $$ = 0; }
     ;

expr: pref
    | expr '?' expr ':' expr { $$ = mknode('?', $1, mknode(':', $3, $5)); }
    | expr '=' expr     { $$ = mknode('=', $1, $3); }
    | expr ADDEQ expr   { $$ = mknode('=', $1, mknode('+', $1, $3)); }
    | expr SUBEQ expr   { $$ = mknode('=', $1, mknode('-', $1, $3)); }
    | expr MULEQ expr   { $$ = mknode('=', $1, mknode('*', $1, $3)); }
    | expr DIVEQ expr   { $$ = mknode('=', $1, mknode('/', $1, $3)); }
    | expr MODEQ expr   { $$ = mknode('=', $1, mknode('%', $1, $3)); }
    | expr ANDEQ expr   { $$ = mknode('=', $1, mknode('&', $1, $3)); }
    | expr OREQ expr    { $$ = mknode('=', $1, mknode('|', $1, $3)); }
    | expr XOREQ expr   { $$ = mknode('=', $1, mknode('^', $1, $3)); }
    | expr SHLEQ expr   { $$ = mknode('=', $1, mknode('L', $1, $3)); }
    | expr SHREQ expr   { $$ = mknode('=', $1, mknode('R', $1, $3)); }
    | expr '+' expr     { $$ = mknode('+', $1, $3); }
    | expr '-' expr     { $$ = mknode('-', $1, $3); }
    | expr '*' expr     { $$ = mknode('*', $1, $3); }
    | expr '/' expr     { $$ = mknode('/', $1, $3); }
    | expr '%' expr     { $$ = mknode('%', $1, $3); }
    | expr '<' expr     { $$ = mknode('<', $1, $3); }
    | expr '>' expr     { $$ = mknode('<', $3, $1); }
    | expr LE expr      { $$ = mknode('l', $1, $3); }
    | expr GE expr      { $$ = mknode('l', $3, $1); }
    | expr EQ expr      { $$ = mknode('e', $1, $3); }
    | expr NE expr      { $$ = mknode('n', $1, $3); }
    | expr '&' expr     { $$ = mknode('&', $1, $3); }
    | expr '|' expr     { $$ = mknode('|', $1, $3); }
    | expr '^' expr     { $$ = mknode('^', $1, $3); }
    | expr SHL expr     { $$ = mknode('L', $1, $3); }
    | expr SHR expr     { $$ = mknode('R', $1, $3); }
    | expr AND expr     { $$ = mknode('a', $1, $3); }
    | expr OR expr      { $$ = mknode('o', $1, $3); }
    ;

exp0: expr
    |                   { $$ = 0; }
    ;

comma_expr: expr
          | comma_expr ',' expr { $$ = mknode(',', $1, $3); }
          ;

init_decllist: init_decl                       { $$ = $1; }
             | init_decl ',' init_decllist     { $1->r = $3; $$ = $1; }
             ;

init_decl: IDENT
{
	Node *n = mknode('I', 0, 0);
	strcpy(n->u.v, $1->u.v);
	n->l = 0;
	$$ = n;
}
         | IDENT '=' expr
{
	Node *n = mknode('I', $3, 0);
	strcpy(n->u.v, $1->u.v);
	$$ = n;
}
         ;

comma_exp0: comma_expr
          |                     { $$ = 0; }
          ;

pref: post
    | '-' pref          { $$ = mkneg($2); }
    | '*' pref          { $$ = mknode('@', $2, 0); }
    | '&' pref          { $$ = mknode('A', $2, 0); }
    | '~' pref          { $$ = mknode('~', $2, 0); }
    | '!' pref          { $$ = mknode('!', $2, 0); }
    | PP pref           { $$ = mknode('p', $2, 0); }
    | MM pref           { $$ = mknode('m', $2, 0); }
    | '(' type ')' pref { $$ = mknode('K', $4, 0); $$->u.n = $2; }
    | '(' type '(' '*' ')' '(' fptpar0 ')' ')' pref {
        /* Cast to a function-pointer type: `(RET (*)(PARAMS)) expr`
         * (py/parse.c: `(void (*)(void *))(mp_lexer_free)`).  minic models
         * a function pointer as IDIR(FUNC(rettype)); the cast just
         * reinterprets the operand's value with that type. */
        $$ = mknode('K', $10, 0);
        $$->u.n = IDIR(FUNC($2));
    }
    ;

post: NUM
    | FNUM
    | STR
    | IDENT
    | SIZEOF '(' type ')' { $$ = mknode('N', 0, 0); $$->u.n = SIZE($3); }
    | SIZEOF '(' type '[' expr ']' ')' {
        /* sizeof(T[dim]) == SIZE(T)*dim.  When dim isn't a foldable
         * constant (MicroPython's MP_STATIC_ASSERT puts an address
         * comparison there and discards the result via (void)), use a
         * dummy dimension of 1 so the expression still type-checks. */
        int dim = constfoldable($5) ? const_eval($5) : 1;
        $$ = mknode('N', 0, 0);
        $$->u.n = SIZE($3) * dim;
    }
    | SIZEOF '(' expr ')' {
        /* sizeof of an expression: unevaluated; report its type size.
         * A bare array variable reports its whole-array byte size;
         * everything else routes through typeof_expr. */
        int member_array_bytes;
        $$ = mknode('N', 0, 0);
        if ($3->op == 'V' && var_arraybytes($3->u.v) > 0)
            $$->u.n = var_arraybytes($3->u.v);
        else if ((member_array_bytes = sizeof_member_array_expr($3)) > 0)
            $$->u.n = member_array_bytes;
        else
            $$->u.n = SIZE(typeof_expr($3));
    }
    | ALIGNOF '(' type ')' {
        /* _Alignof returns alignment requirement for type */
        int align;
        /* For 8086: most types align to 2 bytes (word boundary) */
        /* Except char which aligns to 1 */
        if (KIND($3) == CHR)
            align = 1;
        else if (KIND($3) == LNG || ISFLOAT($3))
            align = 4;  /* long and float/double align to 4 bytes */
        else
            align = 2;  /* int, short, pointers align to 2 bytes */

        $$ = mknode('N', 0, 0);
        $$->u.n = align;
    }
    | '(' type ')' '{' initlist '}' {
        /* Compound literal: (type){ initializer }
         * Creates a temporary with the given type and initializes it.
         * Node 'L' stores: u.n = type, l = initlist
         */
        $$ = mknode('L', $5, 0);
        $$->u.n = (int)$2;  /* Store type */
    }
    | '(' comma_expr ')'  { $$ = $2; }
    | post '(' arg0 ')'   {
        /* Function call. Direct when callee is a bare IDENT (V node);
         * otherwise indirect (e.g. (*fp)(...), arr[i](...), etc.). */
        if ($1->op == 'V')
            $$ = mknode('C', $1, $3);
        else
            $$ = mknode('I', $1, $3);
    }
    | post '[' expr ']'   { $$ = mkidx($1, $3); }
    | post PP             { $$ = mknode('P', $1, 0); }
    | post MM             { $$ = mknode('M', $1, 0); }
    | post '.' IDENT      { $$ = mknode('.', $1, $3); }
    | post ARROW IDENT    {
        /* Desugar ptr->member to (*ptr).member */
        Node *deref = mknode('@', $1, 0);  /* Dereference pointer */
        $$ = mknode('.', deref, $3);       /* Member access */
    }
    | GENERIC '(' expr ',' generic_list ')' {
        /* _Generic(controlling-expr, type1: expr1, ..., default: exprN)
         * Node 'G' stores: l = controlling expr, r = association list
         */
        $$ = mknode('G', $3, $5);
    }
    ;

generic_list: generic_assoc                     { $$ = $1; }
            | generic_assoc ',' generic_list    { $1->r = $3; $$ = $1; }
            ;

generic_assoc: type ':' expr {
        /* Type association: type: expression
         * Node 'g' stores: u.n = type, l = expression, r = next (set later)
         */
        $$ = mknode('g', $3, 0);
        $$->u.n = (int)$1;
    }
    | DEFAULT ':' expr {
        /* Default association: default: expression
         * Use -1 to indicate default
         */
        $$ = mknode('g', $3, 0);
        $$->u.n = -1;
    }
    ;

arg0: arg1
    |               { $$ = 0; }
    ;
arg1: expr          { $$ = mknode(0, $1, 0); }
    | expr ',' arg1 { $$ = mknode(0, $1, $3); }
    ;

%%

/* Public lexer entry: tracks the previously returned token so the
 * identifier directly after struct/union/enum can be resolved in the
 * tag namespace (C keeps tags separate from typedef names). */
int
yylex()
{
	int t;

	/* A function body's closing '}' is the lookahead that reduces (and
	 * emits) the body's last statement, which still reads locals via
	 * varget(); so we can't drop locals the instant we return '}'.
	 * Defer it to the next token request — by then all '}'-triggered
	 * reductions are done.  Clearing here (rather than only at the next
	 * function's init marker) means the prior function's locals are gone
	 * before the next function's parameter-type tokens are lexed, so
	 * var_islocal() can't misfire on a typedef-named param type. */
	if (pending_varclr) {
		varclr();
		pending_varclr = 0;
	}
	/* Retire inner-block renames whose block the lexer has already
	 * closed.  Deferred to here (rather than firing at the '}' itself)
	 * so a rename introduced by a block's *last* statement decl — which
	 * reduces under the '}' lookahead — is still established before this
	 * pop runs, and is then dropped before the next (outer) token is
	 * lexed.  See [[minic-inner-block-scope]]. */
	rename_pop_closed();
	t = yylex_inner();
	if (t == '{')
		brace_depth++;
	else if (t == '}' && brace_depth > 0 && --brace_depth == 0) {
		pending_varclr = 1;
		/* End of a function body: clear any internal-linkage flag so the
		 * NEXT top-level declaration starts exported-by-default.  (The
		 * current function's header was emitted at its ')' lookahead,
		 * before this '}', so the flag was already consumed.) */
		pending_static = 0;
	}
	/* Track a top-level `static` storage class.  Only at brace_depth 0:
	 * a function-local `static` is a mangled file-scope global handled
	 * separately and must not flip the enclosing function's linkage. */
	if (brace_depth == 0) {
		if (t == STATIC)
			pending_static = 1;
		else if (t == ';')
			pending_static = 0;   /* end of a static *object* decl / prototype */
	}
	prevtok = t;
	return t;
}

int
yylex_inner()
{
	struct {
		char *s;
		int t;
	} kwds[] = {
		{ "void", TVOID },
		{ "char", TCHAR },
		{ "short", TSHORT },
		{ "int", TINT },
		{ "long", TLNG },
		{ "unsigned", TUNSIGNED },
		{ "signed", TSIGNED },
		{ "float", TFLOAT },
		{ "double", TDOUBLE },
		{ "const", CONST },
		{ "volatile", VOLATILE },
		{ "_Bool", TBOOL },
		{ "far", TFAR },
		{ "__far", TFAR },
		{ "inline", INLINE },
		{ "static", STATIC },
		{ "extern", EXTERN },
		{ "typedef", TYPEDEF },
		{ "_Static_assert", STATIC_ASSERT },
		{ "_Alignof", ALIGNOF },
		{ "_Alignas", ALIGNAS },
		{ "_Generic", GENERIC },
		{ "struct", STRUCT },
		{ "union", UNION },
		{ "enum", ENUM },
		{ "switch", SWITCH },
		{ "case", CASE },
		{ "default", DEFAULT },
		{ "if", IF },
		{ "else", ELSE },
		{ "for", FOR },
		{ "while", WHILE },
		{ "do", DO },
		{ "return", RETURN },
		{ "break", BREAK },
		{ "continue", CONTINUE },
		{ "goto", GOTO },
		{ "sizeof", SIZEOF },
		{ "asm", ASM },
		{ "__asm__", ASM },
		{ "__asm", ASM },
		{ "__volatile__", VOLATILE },
		{ "__attribute__", ATTRIBUTE },
		{ "__attribute", ATTRIBUTE },
		{ 0, 0 }
	};
	int i, c, c1;
	int suffix_l;  /* set when an integer literal carries an L/l suffix */
	int single_float;  /* set when a float literal carries an f/F suffix */
	unsigned long n;
	char v[NString], *p;

	do {
		c = getchar();
		if (c == '#')
			while ((c = getchar()) != '\n')
				;
		if (c == '/') {
			c1 = getchar();
			if (c1 == '/') {
				/* Single-line comment */
				while ((c = getchar()) != '\n')
					;
			} else if (c1 == '*') {
				/* Block comment */
				int prev = 0;
				while (1) {
					c = getchar();
					if (c == EOF)
						die("unclosed block comment");
					if (c == '\n')
						line++;
					if (prev == '*' && c == '/')
						break;
					prev = c;
				}
				c = ' ';
			} else {
				ungetc(c1, stdin);
			}
		}
		if (c == '\n')
			line++;
	} while (isspace(c));

	if (c == EOF)
		return 0;


	if (isdigit(c) || c == '.') {
		int isfloat = 0;
		suffix_l = 0;
		single_float = 0;
		p = v;

		/* Handle leading dot for numbers like .5 */
		if (c == '.') {
			c = getchar();
			if (c == '.') {
				/* `...` ellipsis (variadic prototype). */
				c = getchar();
				if (c == '.')
					return ELLIPSIS;
				die("unexpected '..' (incomplete ellipsis)");
			}
			if (!isdigit(c)) {
				/* Not a float, just a dot operator */
				ungetc(c, stdin);
				return '.';
			}
			/* Float with a leading dot, e.g. .5 */
			*p++ = '.';
			isfloat = 1;
		}

		n = 0;
		/* Check for hex (0x) or octal (0) - these can't be floats */
		if (c == '0' && !isfloat) {
			c = getchar();
			if (c == 'x' || c == 'X') {
				/* Hexadecimal */
				c = getchar();
				while (isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
					n *= 16;
					if (isdigit(c))
						n += c - '0';
					else if (c >= 'a' && c <= 'f')
						n += c - 'a' + 10;
					else
						n += c - 'A' + 10;
					c = getchar();
				}
				/* Consume integer suffixes: U, L, UL, LU, ULL, LLU, etc. */
				suffix_l = 0;
				for (i = 0; i < 3; i++) {
					if (c == 'l' || c == 'L') { suffix_l = 1; c = getchar(); }
					else if (c == 'u' || c == 'U') c = getchar();
					else break;
				}
				ungetc(c, stdin);
				yylval.n = mknode('N', 0, 0);
				yylval.n->u.n = (int)n;
				yylval.n->nlong = suffix_l || (n > 0xFFFFUL);
				return NUM;
			} else if (c >= '0' && c <= '7') {
				/* Octal */
				while (c >= '0' && c <= '7') {
					n *= 8;
					n += c - '0';
					c = getchar();
				}
				/* Consume integer suffixes: U, L, UL, LU, ULL, LLU, etc. */
				suffix_l = 0;
				for (i = 0; i < 3; i++) {
					if (c == 'l' || c == 'L') { suffix_l = 1; c = getchar(); }
					else if (c == 'u' || c == 'U') c = getchar();
					else break;
				}
				ungetc(c, stdin);
				yylval.n = mknode('N', 0, 0);
				yylval.n->u.n = (int)n;
				yylval.n->nlong = suffix_l || (n > 0xFFFFUL);
				return NUM;
			} else {
				/* Could be 0, 0.5, or 0e10 */
				*p++ = '0';
			}
		}

		/* Parse integer part */
		while (isdigit(c)) {
			if (!isfloat) {
				n *= 10;
				n += c - '0';
			}
			if (p < v + NString - 1)
				*p++ = c;
			c = getchar();
		}

		/* Check for decimal point */
		if (c == '.') {
			isfloat = 1;
			if (p < v + NString - 1)
				*p++ = c;
			c = getchar();
			/* Parse fractional part */
			while (isdigit(c)) {
				if (p < v + NString - 1)
					*p++ = c;
				c = getchar();
			}
		}

		/* Check for exponent */
		if (c == 'e' || c == 'E') {
			isfloat = 1;
			if (p < v + NString - 1)
				*p++ = c;
			c = getchar();
			/* Handle optional sign */
			if (c == '+' || c == '-') {
				if (p < v + NString - 1)
					*p++ = c;
				c = getchar();
			}
			/* Parse exponent digits */
			while (isdigit(c)) {
				if (p < v + NString - 1)
					*p++ = c;
				c = getchar();
			}
		}

		/* Numeric suffix:
		 * - f / F: float
		 * - L / l: only marks long double when paired with a float
		 *   literal (already had a `.` or `e` exponent).  On an integer
		 *   literal, L means long integer (NOT float).
		 * - U / u, LL / ll: integer suffixes, accept and discard.
		 */
		if (c == 'f' || c == 'F') {
			isfloat = 1;
			single_float = 1;  /* `1.5f` is single-precision (Ks), not double */
			c = getchar();  /* Consume float suffix */
		} else if (c == 'l' || c == 'L') {
			if (isfloat) {
				/* `1.0L` / `1.0l` is long double — keep as double. */
				c = getchar();
			} else {
				/* Integer long suffix; consume one or two L/l. */
				suffix_l = 1;
				c = getchar();
				if (c == 'l' || c == 'L')
					c = getchar();
				/* Optional trailing U/u for `LU` etc. */
				if (c == 'u' || c == 'U')
					c = getchar();
			}
		} else if (c == 'u' || c == 'U') {
			c = getchar();
			/* Optional trailing L/l for `UL` etc. — the L still
			 * means LONG (12345UL pushed as 2 words, not 1); this
			 * branch used to consume it without setting suffix_l,
			 * so `%lu` of a UL literal read a garbage high word
			 * (newlibc snprintf_test, §6b). */
			if (c == 'l' || c == 'L') {
				suffix_l = 1;
				c = getchar();
				if (c == 'l' || c == 'L')
					c = getchar();
			}
		}

		ungetc(c, stdin);

		if (isfloat) {
			*p = 0;
			yylval.n = mknode('F', 0, 0);
			strcpy(yylval.n->u.v, v);
			yylval.n->nlong = single_float;  /* 'F': 1 = single (Ks), 0 = double */
			return FNUM;
		} else {
			yylval.n = mknode('N', 0, 0);
			yylval.n->u.n = (int)n;
			yylval.n->nlong = suffix_l || (n > 0xFFFFUL);
			return NUM;
		}
	}

	/* Character literals */
	if (c == '\'') {
		c = getchar();
		if (c == '\\') {
			c = getchar();
			switch (c) {
			case 'n': n = '\n'; break;
			case 't': n = '\t'; break;
			case 'r': n = '\r'; break;
			case 'a': n = '\a'; break;  /* Bell/alert */
			case 'b': n = '\b'; break;  /* Backspace */
			case 'f': n = '\f'; break;  /* Form feed */
			case 'v': n = '\v'; break;  /* Vertical tab */
			case '\\': n = '\\'; break;
			case '\'': n = '\''; break;
			case '\"': n = '\"'; break;
			case 'x': {  /* Hex escape: \xHH */
				n = 0;
				c = getchar();
				while ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
					n *= 16;
					if (c >= '0' && c <= '9')
						n += c - '0';
					else if (c >= 'a' && c <= 'f')
						n += c - 'a' + 10;
					else
						n += c - 'A' + 10;
					c = getchar();
				}
				ungetc(c, stdin);
				break;
			}
			default:
				/* Check for octal escape: \ooo */
				if (c >= '0' && c <= '7') {
					n = c - '0';
					c = getchar();
					if (c >= '0' && c <= '7') {
						n = n * 8 + (c - '0');
						c = getchar();
						if (c >= '0' && c <= '7') {
							n = n * 8 + (c - '0');
						} else {
							ungetc(c, stdin);
						}
					} else {
						ungetc(c, stdin);
					}
				} else {
					/* Unknown escape, treat as literal character */
					n = c;
				}
				break;
			}
		} else {
			n = c;
		}
		c = getchar();
		if (c != '\'')
			die("unclosed character literal");
		yylval.n = mknode('N', 0, 0);
		yylval.n->u.n = (int)n;
		return NUM;
	}

	if (isalpha(c) || c == '_') {
		p = v;
		do {
			if (p == &v[NString-1])
				die("ident too long");
			*p++ = c;
			c = getchar();
		} while (isalpha(c) || c == '_' || isdigit(c));
		*p = 0;
		ungetc(c, stdin);

		/* Check for "long long" */
		if (strcmp(v, "long") == 0) {
			char v2[NString];
			char *p2;
			int saved_c;

			/* Skip whitespace */
			do {
				saved_c = getchar();
			} while (isspace(saved_c) && saved_c != '\n');

			/* Try to read next identifier */
			if (isalpha(saved_c) || saved_c == '_') {
				p2 = v2;
				*p2++ = saved_c;
				while (isalpha(saved_c = getchar()) || saved_c == '_' || isdigit(saved_c)) {
					if (p2 < &v2[NString-1])
						*p2++ = saved_c;
				}
				*p2 = 0;
				ungetc(saved_c, stdin);

				/* Check if it's "long" */
				if (strcmp(v2, "long") == 0)
					return TLNGLNG;

				/* Not "long", need to put back the entire identifier */
				for (i = strlen(v2) - 1; i >= 0; i--)
					ungetc(v2[i], stdin);
			} else {
				ungetc(saved_c, stdin);
			}
		}

		/* Skip storage-class keywords that MiniC treats as no-ops:
		 * - register/auto: deprecated/legacy storage classes
		 * - restrict: pointer aliasing hint (C99), no semantic change for codegen
		 * Recurse to skip whitespace and return the next real token. */
		if (strcmp(v, "register") == 0 || strcmp(v, "auto") == 0 ||
		    strcmp(v, "restrict") == 0 || strcmp(v, "__restrict") == 0 ||
		    strcmp(v, "__restrict__") == 0)
			return yylex_inner();

		for (i=0; kwds[i].s; i++)
			if (strcmp(v, kwds[i].s) == 0) {
				/* A type keyword (int/char/struct/...) starts a
				 * non-array-typedef type; clear any array dim left
				 * over from a previous TNAME so it can't leak into
				 * this declaration. */
				g_td_arraydim = 0;
				g_td_fpid = -1;
				return kwds[i].t;
			}
		yylval.n = mknode('V', 0, 0);
		strcpy(yylval.n->u.v, v);
		/* An identifier right after '.' or '->' is a struct/union member
		 * name (the member namespace), never a type — return IDENT even
		 * when it collides with a typedef, e.g. `h.qstr` / `p->qstr`
		 * where `qstr` is also a typedef. */
		if (prevtok == '.' || prevtok == ARROW)
			return IDENT;
		/* Check if it's a typedef name.  An identifier directly after
		 * struct/union/enum is a tag (separate C namespace) and must
		 * stay IDENT even when a same-named typedef exists, e.g.
		 * `typedef struct Foo Foo; struct Foo { ... };`.  Probe into a
		 * scratch ctyp (not yylval) so the IDENT node value survives if
		 * we fall through. */
		if (prevtok != STRUCT && prevtok != UNION && prevtok != ENUM
		    && !var_islocal(v)) {
			unsigned tnctyp;
			Node *vn = yylval.n;
			if (typhget(v, &tnctyp)) {
				/* If a complete type-specifier was just parsed, the
				 * next identifier begins a declarator (a parameter /
				 * variable / member name), even when it collides with
				 * a typedef name — e.g. `qstr qstr` or `int qstr`.  C
				 * keeps these contexts separate: a typedef name can
				 * only appear as the *first* type token of a
				 * declaration (or after a qualifier), never
				 * immediately after another full type-specifier.
				 * typhget() set g_td_array{dim,elem} from the typedef
				 * entry; clear them since this is a declarator name. */
				if (prevtok == TVOID || prevtok == TCHAR
				    || prevtok == TSHORT || prevtok == TINT
				    || prevtok == TLNG || prevtok == TLNGLNG
				    || prevtok == TUNSIGNED || prevtok == TSIGNED
				    || prevtok == TFLOAT || prevtok == TDOUBLE
				    || prevtok == TBOOL || prevtok == TNAME) {
					g_td_arraydim = 0;
					g_td_arrayelem = 0;
					g_td_fpid = -1;
					yylval.n = vn;
					return IDENT;
				}
				yylval.u = tnctyp;
				return TNAME;
			}
			yylval.n = vn;
		}
		/* Inner-block scope: rewrite a *use* of a source identifier to
		 * its active block-scoped mangled name.  Suppressed when the
		 * previous token is a type-specifier or struct/union/enum (this
		 * IDENT is then a declarator or a tag, handled elsewhere, not a
		 * value use).  Fires only when a rename is active, so files
		 * with no different-typed block-scope collision are unaffected. */
		if (prevtok != TVOID && prevtok != TCHAR && prevtok != TSHORT
		    && prevtok != TINT && prevtok != TLNG && prevtok != TLNGLNG
		    && prevtok != TUNSIGNED && prevtok != TSIGNED
		    && prevtok != TFLOAT && prevtok != TDOUBLE
		    && prevtok != TBOOL && prevtok != TNAME
		    && prevtok != STRUCT && prevtok != UNION
		    && prevtok != ENUM) {
			char *m = rename_lookup(yylval.n->u.v);
			if (m)
				strcpy(yylval.n->u.v, m);
		}
		return IDENT;
	}

	if (c == '"') {
		int esc = 0;
		i = 0;
		n = 32;
		p = alloc(n);
		strcpy(p, "{ b \"");
		for (i=5;; i++) {
			c = getchar();
			if (c == EOF)
				die("unclosed string literal");
			if (i+8 >= n) {
				p = memcpy(alloc(n*2), p, n);
				n *= 2;
			}
			p[i] = c;
			if (esc) {
				/* The previous char was a backslash that started an
				 * escape sequence; this char is the escaped one. */
				esc = 0;
			} else if (c == '\\') {
				esc = 1;
			} else if (c == '"') {
				/* Adjacent string-literal concatenation (C89):
				 * peek past whitespace/newlines for another opening
				 * quote; if found, keep appending into the same
				 * buffer (the closing quote slot at p[i] gets
				 * overwritten by the next string's first char). */
				int d;
				do {
					d = getchar();
					if (d == '\n')
						line++;
				} while (d == ' ' || d == '\t' || d == '\n'
				    || d == '\r' || d == '\f' || d == '\v');
				if (d == '"') {
					i--;  /* for-loop i++ re-targets the quote slot */
					continue;
				}
				ungetc(d, stdin);
				break;
			}
		}
		strcpy(&p[i], "\", b 0 }");
		if (nglo == NGlo)
			die("too many globals");
		ini[nglo] = p;
		yylval.n = mknode('S', 0, 0);
		yylval.n->u.n = nglo++;
		return STR;
	}

	c1 = getchar();

	/* Check for <<= and >>= first (three character operators) */
	if ((c == '<' && c1 == '<') || (c == '>' && c1 == '>')) {
		int c2 = getchar();
		if (c2 == '=') {
			return (c == '<') ? SHLEQ : SHREQ;
		}
		ungetc(c2, stdin);
		/* Fall through to return SHL or SHR below */
	}

#define DI(a, b) a + b*256
	switch (DI(c,c1)) {
	case DI('!','='): return NE;
	case DI('=','='): return EQ;
	case DI('<','='): return LE;
	case DI('>','='): return GE;
	case DI('+','='): return ADDEQ;
	case DI('-','='): return SUBEQ;
	case DI('-','>'): return ARROW;
	case DI('*','='): return MULEQ;
	case DI('/','='): return DIVEQ;
	case DI('%','='): return MODEQ;
	case DI('&','='): return ANDEQ;
	case DI('|','='): return OREQ;
	case DI('^','='): return XOREQ;
	case DI('<','<'): return SHL;
	case DI('>','>'): return SHR;
	case DI('+','+'): return PP;
	case DI('-','-'): return MM;
	case DI('&','&'): return AND;
	case DI('|','|'): return OR;
	}
#undef DI

	ungetc(c1, stdin);

	return c;
}

int
yyerror(char *err)
{
	die("parse error");
	return 0;
}

int
main(int argc, char **argv)
{
	int i;
	static struct { const char *name; int model; } mmodels[] = {
		{ "tiny",    MTiny },
		{ "small",   MSmall },
		{ "medium",  MMedium },
		{ "compact", MCompact },
		{ "large",   MLarge },
		{ "huge",    MHuge },
		{ 0, 0 }
	};

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];
		const char *m = 0;
		if (strcmp(a, "-m") == 0 && i + 1 < argc)
			m = argv[++i];
		else if (strncmp(a, "-m", 2) == 0)
			m = a + 2;
		else if (strncmp(a, "--model=", 8) == 0)
			m = a + 8;
		else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
			fprintf(stderr,
			    "usage: %s [-m <model>] < input.c > output.ssa\n"
			    "  -m <model>   memory model: tiny, small (default),\n"
			    "               medium, compact, large, huge\n",
			    argv[0]);
			return 0;
		} else {
			fprintf(stderr, "%s: unknown argument '%s'\n", argv[0], a);
			return 1;
		}
		if (m) {
			int k;
			for (k = 0; mmodels[k].name; k++)
				if (strcmp(mmodels[k].name, m) == 0) {
					memmodel = mmodels[k].model;
					break;
				}
			if (!mmodels[k].name) {
				fprintf(stderr, "%s: unknown memory model '%s'\n", argv[0], m);
				return 1;
			}
		}
	}

	of = stdout;
	nglo = 1;
	if (yyparse() != 0)
		die("parse error");
	for (i=1; i<nglo; i++) {
		if (glosec[i][0] != 0)
			fprintf(of, "section \"%s\" ", glosec[i]);
		if (gloname[i][0] != 0)
			/* C file-scope data has external linkage unless declared
			 * `static`; `export` makes qbe emit `.globl _name`, which
			 * asm_to_omf.py now treats as authoritative for data
			 * publics (it used to auto-promote every data label) —
			 * §6b.  Anonymous $glo<N> slots (string literals) stay
			 * module-local. */
			fprintf(of, "%sdata $%s = %s\n",
				glostatic[i] ? "" : "export ", gloname[i], ini[i]);
		else
			fprintf(of, "data $glo%d = %s\n", i, ini[i]);
	}
	return 0;
}
