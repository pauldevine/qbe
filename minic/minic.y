%{

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	NString = 128,  /* max identifier length; MicroPython has 49-char names
	                 * (e.g. MP_MAP_LOOKUP_ADD_IF_NOT_FOUND_OR_REMOVE_IF_FOUND)
	                 * and longer generated qstr symbols. Host-only memory. */
	NGlo = 256,
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
#define FAR       (1 << 24)  /* Far pointer flag (32-bit segment:offset) */
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
 * mask, the FAR bit at position 24 shifts to bit 21, and the next DREF
 * call (e.g. in `case '.'` looking up structh[sidx]) explodes into a
 * wild array index → SIGSEGV.  Inner FAR bits encoded inside a nested
 * pointer type sit at position 27 (one IDIR up) and shift correctly to
 * position 24 after DREF, so nested far ptrs (e.g. `int **` in compact)
 * still round-trip.  Reported via stevie alloc.c crashing minic under
 * --model=compact. */
#define DREF(x) (((x) & ~FAR) >> 3)
#define KIND(x) ((x) & 7)
#define ISUNSIGNED(x) ((x) & UNSIGNED)
#define ISFLOAT(x) ((x) & FLOAT)
#define ISFAR(x) ((x) & FAR)
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
	 KIND(x) == CHR ? 1 :  \
	 ((x) & SHORT) ? 2 :  \
	 KIND(x) == INT ? 2 : \
	 KIND(x) == LNG ? 4 : \
	 (KIND(x) == STRUCT_T || KIND(x) == UNION_T) ? structh[DREF(x)].size : \
	 (KIND(x) == PTR && KIND(DREF(x)) == FUN) ? CODEPTR_SZ() : \
	 (KIND(x) == PTR && ISFAR(x)) ? 4 : DATAPTR_SZ())

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
Symb expr(Node *), lval(Node *);
void branch(Node *, int, int);
int stmt(Stmt *, int, int);
void emit_struct_copy(Symb, Symb);

FILE *of;
int line;
int lbl, tmp, nglo;
int enumval; /* Current enum value */
int cur_fn_interrupt; /* 1 if current function has __attribute__((interrupt)) */
int cur_fn_weak;      /* 1 if current function has __attribute__((weak)) */
/* Struct/union return-by-value: when the current function's return type
 * is an aggregate, it is lowered (System-V style) with a hidden first
 * parameter — a caller-allocated pointer to result storage.  The callee
 * copies the returned value through it and returns the pointer; the
 * caller treats the call result as the address of the filled slot.  The
 * hidden pointer is spilled to a fixed-name local slot (%__sret) so the
 * `ret` statement can reload it regardless of which basic block it sits
 * in.  cur_fn_sret marks that the in-progress function uses this ABI;
 * cur_fn_sret_ctyp carries the aggregate ctyp (for the copy size). */
int cur_fn_sret;            /* 1 if current fn returns struct/union by value */
unsigned cur_fn_sret_ctyp;  /* the aggregate ctyp returned by value */
char cur_fn_name[NString];  /* Name of function currently being emitted — used
                             * to mangle function-local statics into file-scope
                             * data globals (`static int x;` in foo() →
                             * `data $_foo_x = ...`). */
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
} varh[NVar];

/* Typedef table.  Sized for real preprocessed TUs (MicroPython headers
 * define well over 128 typedefs); minic is host-compiled so this is cheap. */
enum { NTyp = 512 };
struct {
	char v[NString];
	unsigned ctyp;
	int arraydim;        /* >0 => array typedef (`typedef int jmp_buf[8]`) */
	unsigned arrayelem;  /* element ctyp, valid only when arraydim > 0 */
} typh[NTyp];

/* Set by typhget() (i.e. whenever the lexer resolves a TNAME) to the
 * array dimension/element of an array typedef, so storage-allocating
 * declaration rules (local var, struct member) can size them as arrays
 * rather than as the bare pointer-to-element their ctyp encodes.  Reset
 * to 0 by the lexer on every type keyword and by the `type '*'` pointer
 * rules, so a stale dim never leaks into a following plain declaration. */
int g_td_arraydim = 0;
unsigned g_td_arrayelem = 0;

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
			 * t;}`) is NOT folded here — minic has no inner-block
			 * scoping, so a single shared %name/type would miscompile
			 * the earlier block's uses; that case still errors out. */
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

void
varaddextern(char *v, unsigned ctyp, int isarray)
{
	unsigned h0, h;

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
			return;
		}
		if (strcmp(varh[h].v, v) == 0) {
			/* Allow multiple extern declarations, or extern after definition */
			if (varh[h].isextern || varh[h].glo == 1)
				return;  /* Already declared/defined */
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
	structh[idx].curbfoffset = 0;
	structh[idx].curbfbase = 0;
	structh[idx].forward = 1;
	return idx;
}

void
structaddmember(int sidx, char *name, unsigned ctyp)
{
	int i;
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
	m->bitwidth = 0;    /* Not a bitfield */
	m->bitoffset = 0;
	m->count = 0;       /* Scalar member */
	m->isflex = 0;

	if (structh[sidx].isunion) {
		/* Union: all members at offset 0 */
		m->offset = 0;
		/* Union size is max of member sizes */
		if (SIZE(ctyp) > structh[sidx].size)
			structh[sidx].size = SIZE(ctyp);
	} else {
		/* Struct: members laid out sequentially */
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
	m->bitwidth = 0;
	m->bitoffset = 0;
	m->count = count;
	m->isflex = (count == 0);  /* `T x[];` flexible array: 0 bytes but decays */
	total = SIZE(ctyp) * count;
	if (structh[sidx].isunion) {
		m->offset = 0;
		if (total > structh[sidx].size)
			structh[sidx].size = total;
	} else {
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

	/* Check if this bitfield fits in current storage unit */
	if (structh[sidx].curbfoffset == 0 ||
	    structh[sidx].curbfoffset + width > unitsize) {
		/* Start a new storage unit */
		structh[sidx].curbfbase = structh[sidx].size;
		structh[sidx].curbfoffset = 0;
		structh[sidx].size += unitbytes;
	}

	m = &structh[sidx].members[structh[sidx].nmembers];
	strcpy(m->name, name);
	m->ctyp = ctyp;
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
		/* Struct: add anonymous member size */
		structh[parent_sidx].size += anon_size;
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
			return 1;
		}
		if (typh[h].v[0] == 0)
			return 0;
		h = (h+1) % NTyp;
	} while(h != h0);
	return 0;
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
		if (k == LNG) return 'd';  /* double */
		return 's';  /* float */
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
		if (KIND(ctyp) == LNG) return 'd';  /* double */
		return 's';  /* float */
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
	unsigned h0, h;
	int n;

	if (cur_fn_name[0] == 0)
		die("static local outside function context");
	n = snprintf(mangled, sizeof mangled, "_%s_%s", cur_fn_name, name);
	if (n < 0 || n >= (int)sizeof mangled)
		die("static-local mangled name too long");
	if (nglo == NGlo)
		die("too many globals");
	ini[nglo] = alloc(strlen(init_buf) + 1);
	strcpy(ini[nglo], init_buf);
	strcpy(gloname[nglo], mangled);
	varadd(name, nglo, sym_ctyp, isarray);
	h0 = hash(name);
	h = h0;
	do {
		if (strcmp(varh[h].v, name) == 0) {
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

void
sext(Symb *s)
{
	fprintf(of, "\t%%t%d =l extsw ", tmp);
	psymb(*s);
	fprintf(of, "\n");
	s->t = Tmp;
	s->ctyp = LNG;
	s->u.n = tmp++;
}

unsigned
prom(int op, Symb *l, Symb *r)
{
	Symb *t;
	int sz;

	/* Floating-point promotion: if either operand is float/double, promote both */
	if (ISFLOAT(l->ctyp) || ISFLOAT(r->ctyp)) {
		unsigned target_type = (LNG | FLOAT);  /* default to double */

		/* If both are float, result is float; otherwise double */
		if (ISFLOAT(l->ctyp) && ISFLOAT(r->ctyp)) {
			if (KIND(l->ctyp) == INT && KIND(r->ctyp) == INT)
				target_type = INT | FLOAT;  /* both float */
			else
				target_type = LNG | FLOAT;  /* at least one double */
		}

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
		} else if (l->ctyp != target_type) {
			/* Convert float to double or vice versa */
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
		} else if (r->ctyp != target_type) {
			/* Convert float to double or vice versa */
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

	/* Promote char to int for comparisons (both operands must be int or larger) */
	if (strchr("ne<l", op) && KIND(l->ctyp) == CHR && KIND(r->ctyp) == CHR) {
		fprintf(of, "\t%%t%d =w extsb ", tmp);
		psymb(*l);
		fprintf(of, "\n");
		l->t = Tmp;
		l->ctyp = INT;
		l->u.n = tmp++;
		fprintf(of, "\t%%t%d =w extsb ", tmp);
		psymb(*r);
		fprintf(of, "\n");
		r->t = Tmp;
		r->ctyp = INT;
		r->u.n = tmp++;
		return INT;
	}

	if (l->ctyp == r->ctyp && KIND(l->ctyp) != PTR)
		return l->ctyp;

	/* Promote char to int */
	if (KIND(l->ctyp) == CHR && KIND(r->ctyp) != CHR) {
		/* Extend char to int */
		fprintf(of, "\t%%t%d =w extsb ", tmp);
		psymb(*l);
		fprintf(of, "\n");
		l->t = Tmp;
		l->ctyp = INT;
		l->u.n = tmp++;
	}
	if (KIND(r->ctyp) == CHR && KIND(l->ctyp) != CHR) {
		fprintf(of, "\t%%t%d =w extsb ", tmp);
		psymb(*r);
		fprintf(of, "\n");
		r->t = Tmp;
		r->ctyp = INT;
		r->u.n = tmp++;
	}

	/* Promote int to long (handles both signed and unsigned) */
	if (KIND(l->ctyp) == LNG && KIND(r->ctyp) == INT) {
		sext(r);
		/* Return unsigned long if l is unsigned, else signed long */
		return ISUNSIGNED(l->ctyp) ? (LNG | UNSIGNED) : LNG;
	}
	if (KIND(l->ctyp) == INT && KIND(r->ctyp) == LNG) {
		sext(l);
		/* Return unsigned long if r is unsigned, else signed long */
		return ISUNSIGNED(r->ctyp) ? (LNG | UNSIGNED) : LNG;
	}

	/* Pointer subtraction yields ptrdiff_t (long): handle BEFORE the
	 * same-kind early return so the result type is long, not pointer. */
	if (op == '-' && KIND(l->ctyp) == PTR && KIND(r->ctyp) == PTR) {
		if (l->ctyp != r->ctyp)
			die("non-homogeneous pointers in substraction");
		return LNG;
	}

	/* Handle unsigned type promotion */
	if (KIND(l->ctyp) == KIND(r->ctyp)) {
		/* Same base type, possibly different signedness */
		/* Promote to unsigned if either is unsigned */
		if (ISUNSIGNED(l->ctyp) || ISUNSIGNED(r->ctyp))
			return KIND(l->ctyp) | UNSIGNED;
		return l->ctyp;
	}

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
		if (pt == 'l' && irtyp(r->ctyp) != 'l')
			sext(r);
		fprintf(of, "\t%%t%d =%c mul %d, ", tmp, pt, sz);
		psymb(*r);
		fprintf(of, "\n");
		r->u.n = tmp++;
		r->ctyp = (pt == 'l') ? LNG : INT;
	}
	return l->ctyp;
}

void
load(Symb d, Symb s)
{
	char t;

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
	} else {
		fprintf(of, " =w loadfw ");  /* Word (16-bit) load through far ptr */
	}
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
	} else {
		fprintf(of, "storefw ");  /* Word (16-bit) store through far ptr */
	}
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
	int src_far = ISFAR(src.ctyp);
	int dst_far = ISFAR(dst.ctyp);
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
		psymb(val);
		fprintf(of, ", ");
		psymb(off_addr);
		fprintf(of, "\n");
	}
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
		"memcpy", "memcmp", "memset",
		"intdos", "int86", "segread",
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

void
call(Node *n, Symb *sr)
{
	Node *a;
	char *f;
	unsigned ft;
	Symb *sv;

	f = n->l->u.v;
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
			sr->ctyp = DREF(fptr_type);     /* return_type */
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

			/* Evaluate all arguments */
			for (a=n->r; a; a=a->r)
				a->u.s = expr(a->l);

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
			for (a=n->r; a; a=a->r) {
				fprintf(of, "%c ", irtyp_ret(a->u.s.ctyp));
				psymb(a->u.s);
				fprintf(of, ", ");
			}
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
	int sret = (KIND(sr->ctyp) == STRUCT_T || KIND(sr->ctyp) == UNION_T);
	unsigned aggr = sr->ctyp;
	int sret_slot = 0;
	if (sret)
		sret_slot = alloc_sret_slot(aggr);
	for (a=n->r; a; a=a->r)
		a->u.s = expr(a->l);
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
	for (a=n->r; a; a=a->r) {
		fprintf(of, "%c ", irtyp_ret(a->u.s.ctyp));
		psymb(a->u.s);
		fprintf(of, ", ");
	}
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
		['<'] = "cslt",  /* meeeeh, wrong for pointers! */
		['l'] = "csle",
		['e'] = "ceq",
		['n'] = "cne",
	};
	Symb sr, s0, s1, sl;
	int o, l;
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
		fprintf(of, "@l%d\n", l+2);
		fprintf(of, "\tjmp @l%d\n", l+4);
		/* False branch */
		fprintf(of, "@l%d\n", l+1);
		s1 = expr(n->r->r);
		fprintf(of, "\tjmp @l%d\n", l+3);
		fprintf(of, "@l%d\n", l+3);
		fprintf(of, "\tjmp @l%d\n", l+4);
		/* Merge */
		fprintf(of, "@l%d\n", l+4);
		if (s0.ctyp != s1.ctyp) {
			if (s0.ctyp == LNG && s1.ctyp == INT)
				sr.ctyp = LNG;
			else if (s0.ctyp == INT && s1.ctyp == LNG)
				sr.ctyp = LNG;
			else
				sr.ctyp = s0.ctyp;
		} else
			sr.ctyp = s0.ctyp;
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
		sr.ctyp = INT;
		break;

	case 'F':
		/* Floating-point literal */
		/* For now, default to double; we can make this smarter later */
		sr.t = Tmp;
		sr.u.n = tmp++;
		sr.ctyp = LNG | FLOAT;  /* double */
		fprintf(of, "\t");
		psymb(sr);
		fprintf(of, " =d copy d_%s\n", n->u.v);
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

				/* Zero-initialize first */
				for (int j = 0; j < s; j += 4) {
					if (j == 0)
						fprintf(of, "\tstorew 0, %%_clit%d\n", clitnum);
					else {
						fprintf(of, "\t%%t%d =w add %%_clit%d, %d\n", tmp, clitnum, j);
						fprintf(of, "\tstorew 0, %%t%d\n", tmp);
						tmp++;
					}
				}

				/* Initialize members from initlist with designator support */
				while (init) {
					Node *item = init->l;
					int midx;
					struct Member *m;
					Symb val;

					if (item->op == 'D') {
						/* Designated field initializer: .field = value */
						midx = structfindmember(sidx, item->r->u.v);
						if (midx < 0)
							die("unknown member in designated initializer");
						m = &structh[sidx].members[midx];
						val = expr(item->l);
						i = midx + 1;  /* Continue from after this member */
					} else {
						/* Sequential initializer */
						if (i >= structh[sidx].nmembers)
							die("too many initializers for struct");
						m = &structh[sidx].members[i];
						val = expr(item);
						i++;
					}

					/* Compute member address and store */
					if (m->offset > 0) {
						fprintf(of, "\t%%t%d =w add %%_clit%d, %d\n", tmp, clitnum, m->offset);
						fprintf(of, "\tstore%c ", irtyp(m->ctyp));
						psymb(val);
						fprintf(of, ", %%t%d\n", tmp);
						tmp++;
					} else {
						fprintf(of, "\tstore%c ", irtyp(m->ctyp));
						psymb(val);
						fprintf(of, ", %%_clit%d\n", clitnum);
					}

					init = init->r;
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

			/* Evaluate function pointer expression */
			fptr = expr(n->l);

			/* Check it's a function pointer */
			if (KIND(fptr.ctyp) != PTR || KIND(DREF(fptr.ctyp)) != FUN)
				die("invalid indirect call - not a function pointer");

			/* Get return type */
			fptr_type = DREF(fptr.ctyp);  /* FUN(return_type) */
			sr.ctyp = DREF(fptr_type);     /* return_type */
			sret = (KIND(sr.ctyp) == STRUCT_T || KIND(sr.ctyp) == UNION_T);
			aggr = sr.ctyp;

			/* Struct/union return-by-value: alloc result storage and
			 * pass its address as the hidden first argument. */
			if (sret)
				sret_slot = alloc_sret_slot(aggr);

			/* Evaluate all arguments */
			for (a=n->r; a; a=a->r)
				a->u.s = expr(a->l);

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
			for (a=n->r; a; a=a->r) {
				fprintf(of, "%c ", irtyp_ret(a->u.s.ctyp));
				psymb(a->u.s);
				fprintf(of, ", ");
			}
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
		/* Check if dereferencing a far pointer */
		if (ISFAR(s0.ctyp)) {
			loadfar(sr, s0);
		} else {
			load(sr, s0);
		}
		break;

	case 'A':
		sr = lval(n->l);
		sr.ctyp = IDIR(sr.ctyp);
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

			/* Compute member address: struct_addr + offset.  Under far-
			 * data models (compact/large/huge), when s0 came through a
			 * far-pointer deref it carries the FAR bit and the address
			 * is Kl (4-byte seg:off).  The add must then be `=l add` so
			 * we don't truncate to 16 bits.  Mirror s0's FAR-ness onto
			 * addr so downstream load/store picks the right width. */
			{
				char klass = ISFAR(s0.ctyp) ? 'l' : 'w';
				unsigned ptyp = ISFAR(s0.ctyp) ? IDIR_FAR(m->ctyp)
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

			/* Load value from member address */
			sr.t = Tmp;
			sr.u.n = tmp++;
			sr.ctyp = m->ctyp;
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
			fprintf(of, " =%c %s ", irtyp(sr.ctyp),
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
			 * truncated by `=w copy`; when widening int -> long we must
			 * sign-extend with `extsw`. */
			char dst = irtyp_ret(sr.ctyp);
			char src = irtyp_ret(s0.ctyp);
			fprintf(of, "\t");
			psymb(sr);
			if (dst == 'l' && src == 'w') {
				fprintf(of, " =l extsw ");
			} else {
				fprintf(of, " =%c copy ", dst);
			}
			psymb(s0);
			fprintf(of, "\n");
		}
		break;

	case '=':
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
					char klass = ISFAR(s_struct.ctyp) ? 'l' : 'w';
					unsigned ptyp = ISFAR(s_struct.ctyp)
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

					/* Load current storage unit value */
					oldval.t = Tmp;
					oldval.u.n = tmp++;
					oldval.ctyp = m->ctyp;
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

					/* Store back */
					fprintf(of, "\tstore%c ", irtyp(m->ctyp));
					psymb(merged);
					fprintf(of, ", ");
					psymb(addr);
					fprintf(of, "\n");

					sr = s0;  /* Assignment returns the assigned value */
					break;
				}
			}
		}

		s0 = expr(n->r);
		s1 = lval(n->l);
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
		} else if (ISFLOAT(s1.ctyp) && ISFLOAT(s0.ctyp) && s1.ctyp != s0.ctyp) {
			/* Convert between float and double */
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
			/* Extend char to int */
			fprintf(of, "\t%%t%d =w extsb ", tmp);
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
		/* Allow assignment between signed/unsigned variants and float types */
		if ((s1.ctyp & ~FAR) != (s0.ctyp & ~FAR)
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
		if (ISFAR(s1.ctyp) && KIND(s1.ctyp) != PTR && KIND(s1.ctyp) != FUN) {
			char t = irtyp(s1.ctyp);
			if (t == 'b')
				fprintf(of, "\tstorefb ");
			else if (t == 'h')
				fprintf(of, "\tstorefh ");
			else if (t == 'l')
				fprintf(of, "\tstorefl ");
			else
				fprintf(of, "\tstorefw ");
		} else {
			fprintf(of, "\tstore%c ", irtyp(s1.ctyp));
		}
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
		if (ISFAR(sl.ctyp) && KIND(sl.ctyp) != PTR && KIND(sl.ctyp) != FUN) {
			loadfar(s0, sl);
		} else {
			load(s0, sl);
		}
		s1.t = Con;
		s1.u.n = 1;
		s1.ctyp = INT;
		/* Compute new value */
		sr.ctyp = prom(o, &s0, &s1);
		if (!huge_ptr_binop(o, sr, s0, s1)) {
			fprintf(of, "\t");
			psymb(sr);
			fprintf(of, " =%c %s ", irtyp(sr.ctyp), o == '+' ? "add" : "sub");
			psymb(s0);
			fprintf(of, ", ");
			psymb(s1);
			fprintf(of, "\n");
		}
		/* Store new value (handle far pointer) */
		if (ISFAR(sl.ctyp) && KIND(sl.ctyp) != PTR && KIND(sl.ctyp) != FUN) {
			char t = irtyp(sl.ctyp);
			if (t == 'b')
				fprintf(of, "\tstorefb ");
			else if (t == 'h')
				fprintf(of, "\tstorefh ");
			else if (t == 'l')
				fprintf(of, "\tstorefl ");
			else
				fprintf(of, "\tstorefw ");
		} else {
			fprintf(of, "\tstore%c ", irtyp(sl.ctyp));
		}
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
		if (ISFAR(sl.ctyp) && KIND(sl.ctyp) != PTR && KIND(sl.ctyp) != FUN) {
			loadfar(s0, sl);
		} else {
			load(s0, sl);
		}
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
		fprintf(of, " =%c", irtyp(sr.ctyp));
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
		} else if (strchr("<l", o) && (ISUNSIGNED(s0.ctyp) || ISUNSIGNED(s1.ctyp))) {
			/* Unsigned integer comparison */
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
		if (ISFAR(sl.ctyp) && KIND(sl.ctyp) != PTR && KIND(sl.ctyp) != FUN) {
			char t = irtyp(sl.ctyp);
			if (t == 'b')
				fprintf(of, "\tstorefb ");
			else if (t == 'h')
				fprintf(of, "\tstorefh ");
			else if (t == 'l')
				fprintf(of, "\tstorefl ");
			else
				fprintf(of, "\tstorefw ");
		} else {
			fprintf(of, "\tstore%c ", irtyp(sl.ctyp));
		}
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
		if (!varget(n->u.v))
			die("undefined variable");
		sr = *varget(n->u.v);
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

				/* Zero-initialize first */
				for (int j = 0; j < s; j += 4) {
					if (j == 0)
						fprintf(of, "\tstorew 0, %%_clit%d\n", clitnum);
					else {
						fprintf(of, "\t%%t%d =w add %%_clit%d, %d\n", tmp, clitnum, j);
						fprintf(of, "\tstorew 0, %%t%d\n", tmp);
						tmp++;
					}
				}

				/* Initialize members from initlist with designator support */
				while (init) {
					Node *item = init->l;
					int midx;
					struct Member *m;
					Symb val;

					if (item->op == 'D') {
						/* Designated field initializer: .field = value */
						midx = structfindmember(sidx, item->r->u.v);
						if (midx < 0)
							die("unknown member in designated initializer");
						m = &structh[sidx].members[midx];
						val = expr(item->l);
						i = midx + 1;
					} else {
						/* Sequential initializer */
						if (i >= structh[sidx].nmembers)
							die("too many initializers for struct");
						m = &structh[sidx].members[i];
						val = expr(item);
						i++;
					}

					if (m->offset > 0) {
						fprintf(of, "\t%%t%d =w add %%_clit%d, %d\n", tmp, clitnum, m->offset);
						fprintf(of, "\tstore%c ", irtyp(m->ctyp));
						psymb(val);
						fprintf(of, ", %%t%d\n", tmp);
						tmp++;
					} else {
						fprintf(of, "\tstore%c ", irtyp(m->ctyp));
						psymb(val);
						fprintf(of, ", %%_clit%d\n", clitnum);
					}

					init = init->r;
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

			/* Compute member address: struct_addr + offset.  Under far-
			 * data, when s0 came through a far-ptr deref it carries the
			 * FAR bit and the address is Kl.  Propagate FAR onto sr so
			 * the assignment site (which checks ISFAR(sl.ctyp)) routes
			 * through storefar instead of plain store. */
			klass = ISFAR(s0.ctyp) ? 'l' : 'w';
			far_flag = ISFAR(s0.ctyp) ? FAR : 0;
			if (m->offset > 0) {
				sr.t = Tmp;
				sr.u.n = tmp++;
				sr.ctyp = m->ctyp | far_flag;
				fprintf(of, "\t");
				psymb(sr);
				fprintf(of, " =%c add ", klass);
				psymb(s0);
				fprintf(of, ", %d\n", m->offset);
			} else {
				/* Offset 0, just use struct address */
				sr = s0;
				sr.ctyp = m->ctyp | far_flag;
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
		if (r1 && !contains_case_label((Stmt*)s->p2))
			return r1;
		int r2 = genswitchbody((Stmt*)s->p2, brk, cont, cases, caselbl, ncase);
		/* When p2 contains a case label it provides a fresh re-entry
		 * point: even if p1 terminated the prior basic block, control
		 * can resume at the case label and fall through past p2.  The
		 * combined termination state of this Seq is therefore r2 alone
		 * — propagating r1 would let an earlier case body's `break`
		 * poison enclosing Seqs and suppress sibling stmts that
		 * legitimately follow the case label. */
		if (contains_case_label((Stmt*)s->p2))
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
		fprintf(of, "\tjmp @user_%s\n", s->label);
		return 1;
	case Label:
		fprintf(of, "@user_%s\n", s->label);
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
		if (a->noutputs == 0 && a->ninputs == 0) {
			/* Simple inline assembly - emit directly */
			fprintf(of, "\tasm \"%s\"\n", a->code);
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
							/* Local variable - use stack-relative addressing */
							dst += sprintf(dst, "[bp-%%%s]", s.u.v);
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
								/* Local variable - use stack-relative addressing */
								dst += sprintf(dst, "[bp-%%%s]", s.u.v);
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
			fprintf(of, "\tasm \"%s\"\n", processed);
		}
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
	n->l = l;
	n->r = r;
	return n;
}

Node *
mkidx(Node *a, Node *i)
{
	Node *n;

	n = mknode('+', a, i);
	n = mknode('@', n, 0);
	return n;
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
	varadd(v, 0, ctyp, 0);
	strcpy(n->u.v, v);
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

	if (!init || init->op != '{')
		die("array member requires a braced initializer");
	for (ln = init->l; ln; ln = ln->r) {
		Node *item = ln->l;
		Node *val = item;
		if (item->op == 'd') {         /* [k] = v */
			int k = const_eval(item->r);
			if (k < n)
				die("out-of-order array designator unsupported");
			agg_zfill((k - n) * elemsz, buf, bl, first);
			n = k;
			val = item->l;
		} else if (item->op == 'D') {
			die("field designator in array initializer");
		}
		if (n >= cnt)
			die("too many initializers for array member");
		agg_emit_value(elemty, 0, val, buf, bl, first);
		n++;
	}
	if (n < cnt)
		agg_zfill((cnt - n) * elemsz, buf, bl, first);
}

void
agg_emit_struct(int sidx, int isunion, Node *agg, char *buf, int *bl, int *first)
{
	int cursor = 0, memidx = 0;
	int structsize = structh[sidx].size;
	Node *ln;

	if (!agg || agg->op != '{')
		die("struct/union initializer must be braced");
	for (ln = agg->l; ln; ln = ln->r) {
		Node *item = ln->l;
		Node *val;
		struct Member *m;
		int msize;

		if (item->op == 'D') {         /* .field = val */
			int k, found = -1;
			for (k = 0; k < structh[sidx].nmembers; k++)
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
		if (memidx >= structh[sidx].nmembers)
			die("too many initializers for struct");
		m = &structh[sidx].members[memidx];
		if (m->bitwidth) {
			/* Pack a run of bitfield members that share one storage
			 * unit (same m->offset) into a single scalar data item.
			 * Each loop iteration consumes one initializer item
			 * (sequential or `.field =` designated); accumulate and
			 * flush when the next member leaves the unit (different
			 * offset / not a bitfield / end). */
			unsigned bfctyp = m->ctyp;
			int bfunit = SIZE(bfctyp);
			int bfbase = m->offset;
			unsigned long accum = 0;
			unsigned long unitmask =
			    bfunit >= 8 ? ~0UL : ((1UL << (bfunit * 8)) - 1);

			if (!isunion) {
				if (bfbase < cursor)
					die("out-of-order designated initializer unsupported");
				agg_zfill(bfbase - cursor, buf, bl, first);
				cursor = bfbase;
			}
			for (;;) {
				unsigned long fv, fmask;
				Node *fval = agg_unwrap_scalar(val);
				Node *nitem;
				struct Member *nm;
				int nidx;

				fv = fval ? (unsigned long)const_eval(fval) : 0;
				fmask = m->bitwidth >= 64 ? ~0UL
				    : ((1UL << m->bitwidth) - 1);
				accum |= (fv & fmask) << m->bitoffset;
				memidx++;
				if (isunion || !ln->r)
					break;
				/* peek the next initializer item */
				nitem = ln->r->l;
				if (nitem->op == 'D') {
					int k;
					nidx = -1;
					for (k = 0; k < structh[sidx].nmembers; k++)
						if (strcmp(structh[sidx].members[k].name,
						           nitem->r->u.v) == 0) {
							nidx = k;
							break;
						}
					if (nidx < 0)
						die("unknown member in designated initializer");
				} else if (nitem->op == 'd') {
					break;
				} else {
					nidx = memidx;
					if (nidx >= structh[sidx].nmembers)
						break;
				}
				nm = &structh[sidx].members[nidx];
				if (!nm->bitwidth || nm->offset != bfbase)
					break;
				ln = ln->r;
				m = nm;
				memidx = nidx;
				val = nitem->op == 'D' ? nitem->l : nitem;
			}
			agg_sep(buf, bl, first);
			*bl += sprintf(buf + *bl, " %c %lu",
			    irtyp(bfctyp), accum & unitmask);
			cursor += bfunit;
			if (isunion)
				break;
			continue;
		}
		if (!isunion) {
			if (m->offset < cursor)
				die("out-of-order designated initializer unsupported");
			agg_zfill(m->offset - cursor, buf, bl, first);
			cursor = m->offset;
		}
		agg_emit_value(m->ctyp, m->count, val, buf, bl, first);
		msize = m->count ? SIZE(m->ctyp) * m->count : SIZE(m->ctyp);
		cursor += msize;
		memidx++;
		if (isunion)
			break;                 /* one initialized member */
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
	int bl = 0, first = 1;

	if (ctyp == NIL)
		die("invalid void declaration");
	if (nglo == NGlo)
		die("too many globals");
	bl = sprintf(buf, "align %d {", iralign(ctyp));
	if (KIND(ctyp) == STRUCT_T || KIND(ctyp) == UNION_T)
		agg_emit_struct(DREF(ctyp), KIND(ctyp) == UNION_T,
		                agg, buf, &bl, &first);
	else
		agg_emit_scalar(ctyp, agg, buf, &bl, &first);
	if (first)                             /* nothing emitted: keep block legal */
		bl += sprintf(buf + bl, " z 1");
	bl += sprintf(buf + bl, " }");
	ini[nglo] = alloc(bl + 1);
	strcpy(ini[nglo], buf);
	strcpy(gloname[nglo], name);
	varadd(name, nglo++, ctyp, 0);
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
	fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign(ctyp), s);
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
 *   'G' = *IDENT()      (function returning pointer to base) */
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

/* Same as emit_local_multi_decl but every declarator (including the
 * first) is in `list`.  Used when the first declarator is decorated
 * (`[N]`, `*`, `()`, etc.). */
void
emit_local_multi_decl_full(unsigned base, Node *list)
{
	Node *n, *next;
	int i;
	unsigned t;
	char *v;

	if (base == NIL)
		die("invalid void declaration");
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
			int total = count * SIZE(elem);
			varadd(v, 0, IDIR(elem), 1);
			fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign(elem), total);
			continue;
		}
		t = (n->op == 'P' || n->op == 'A') ? IDIR(base) : base;
		varadd(v, 0, t, n->op == 'A' ? 1 : 0);
		fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign(t), SIZE(t));
		if (KIND(t) == STRUCT_T || KIND(t) == UNION_T) {
			int sz = SIZE(t);
			for (i = 0; i < sz; i += 4) {
				if (i == 0)
					fprintf(of, "\tstorew 0, %%%s\n", v);
				else {
					fprintf(of, "\t%%_zinit%d =w add %%%s, %d\n", tmp, v, i);
					fprintf(of, "\tstorew 0, %%_zinit%d\n", tmp);
				}
				tmp++;
			}
		}
		(void)next;
	}
}

/* Emit a multi-name local declaration: `type IDENT, ext_decllist;`.
 * Each declarator carries its own decoration in `op`.  Used for the
 * many K&R patterns such as `char *s1, *s2;` and the mixed
 * `char *initstr, *getenv();` (proto + var). */
void
emit_local_multi_decl(unsigned base, char *first, Node *rest)
{
	int s, i;
	Node *n;
	unsigned t;
	char *v;

	if (base == NIL)
		die("invalid void declaration");
	s = SIZE(base);
	v = first;
	varadd(v, 0, base, 0);
	fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign(base), s);
	if (KIND(base) == STRUCT_T || KIND(base) == UNION_T)
		for (i = 0; i < s; i += 4) {
			if (i == 0)
				fprintf(of, "\tstorew 0, %%%s\n", v);
			else {
				fprintf(of, "\t%%_zinit%d =w add %%%s, %d\n", tmp, v, i);
				fprintf(of, "\tstorew 0, %%_zinit%d\n", tmp);
			}
			tmp++;
		}
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
			varadd(v, 0, IDIR(ebase), 1);
			fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign(ebase), total);
			continue;
		}
		if (n->op == 'P') {
			t = IDIR(ebase);
			varadd(v, 0, t, 0);
			fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign(t), SIZE(t));
			if (n->l) {
				Node *id = mknode('V', 0, 0);
				strcpy(id->u.v, v);
				expr(mknode('=', id, n->l));
			}
			continue;
		}
		/* Plain or [N] declarator: peel one * off the absorbed base
		 * so `char *p, c;` makes c a `char` (standard C semantics).
		 * `[N]` similarly lands at element-of-base. */
		t = (n->op == 'A') ? IDIR(ebase) : ebase;
		varadd(v, 0, t, n->op == 'A' ? 1 : 0);
		fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign(t), SIZE(t));
		if (KIND(t) == STRUCT_T || KIND(t) == UNION_T) {
			int sz = SIZE(t);
			for (i = 0; i < sz; i += 4) {
				if (i == 0)
					fprintf(of, "\tstorew 0, %%%s\n", v);
				else {
					fprintf(of, "\t%%_zinit%d =w add %%%s, %d\n", tmp, v, i);
					fprintf(of, "\tstorew 0, %%_zinit%d\n", tmp);
				}
				tmp++;
			}
		}
		if (n->op == 0 && n->l) {
			Node *id = mknode('V', 0, 0);
			strcpy(id->u.v, v);
			expr(mknode('=', id, n->l));
		}
	}
}

/* Emit a K&R-style function header.  Called from the prot_knr action
 * for definitions like `foo(a, b) int a; char *b; { ... }`.  `params`
 * is a Node chain of bare parameter names built by kr_idlist; each
 * was registered (via kr_param_dcls' kr_namelist actions) with its
 * declared type.  Anything not declared defaults to int.  Return type
 * is implicitly int. */
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
	strncpy(cur_fn_name, fname, NString - 1);
	cur_fn_name[NString - 1] = 0;
	varadd(fname, 1, FUNC(INT), 0);
	fprintf(of, "export function w $%s(", fname);
	n = params;
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
	for (t = 0, n = params; n; t++, n = n->r) {
		s = varget(n->u.v);
		m = SIZE(s->ctyp);
		fprintf(of, "\t%%%s =%c alloc%d %d\n", n->u.v, ALLOC_T(), iralign(s->ctyp), m);
		fprintf(of, "\tstore%c %%t%d", irtyp(s->ctyp), t);
		fprintf(of, ", %%%s\n", n->u.v);
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

	strncpy(cur_fn_name, fname, NString - 1);
	cur_fn_name[NString - 1] = 0;
	varadd(fname, 1, FUNC(curfntyp), 0);

	/* Struct/union return-by-value: hidden first pointer parameter +
	 * pointer return (see cur_fn_sret notes near the top of the file). */
	cur_fn_sret = (KIND(curfntyp) == STRUCT_T || KIND(curfntyp) == UNION_T);
	cur_fn_sret_ctyp = curfntyp;

	if (cur_fn_sret)
		fprintf(of, "export function %c $%s(", DATAPTR_T(), fname);
	else if (curfntyp == NIL)
		fprintf(of, "export function $%s(", fname);
	else
		fprintf(of, "export function %c $%s(", irtyp_ret(curfntyp), fname);
	if (cur_fn_sret) {
		fprintf(of, "%c %%t%d", DATAPTR_T(), tmp++);
		if (params)
			fprintf(of, ", ");
	}
	n = params;
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
	if (cur_fn_sret) {
		fprintf(of, "\t%%__sret =%c alloc4 %d\n", ALLOC_T(), DATAPTR_SZ());
		fprintf(of, "\tstore%c %%t0, %%__sret\n", DATAPTR_T());
	}
	for (t = (cur_fn_sret ? 1 : 0), n = params; n; t++, n = n->r) {
		s = varget(n->u.v);
		m = SIZE(s->ctyp);
		fprintf(of, "\t%%%s =%c alloc%d %d\n", n->u.v, ALLOC_T(), iralign(s->ctyp), m);
		fprintf(of, "\tstore%c %%t%d", irtyp(s->ctyp), t);
		fprintf(of, ", %%%s\n", n->u.v);
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
%type <n> expr exp0 pref post arg0 arg1 par0 par1 fptpar0 fptpar1 initlist inititem generic_list generic_assoc idlist kr_idlist kr_namelist kr_name sm_more_names ext_decllist ext_decl comma_expr comma_exp0 init_decllist init_decl gaggr gilist gitem gival
%type <n> asmoutputs asmoutputlist asmoutput asminputs asminputlist asminput asmclobbers asmclobberlist
%token <u> TNAME

%%

prog: | prog kfunc | prog attr_kfunc | prog typed_decl | prog attr_typed_decl | prog edcl | prog tdcl | prog sdcl | prog static_assert_dcl | prog externdcl ;

attr_kfunc: attrspec storageopt inlineopt init_attr prot_knr '{' dcls stmts '}'
{
	if (cur_fn_interrupt) {
		/* Interrupt handler - emit iret instead of ret */
		if (!stmt($8, -1, -1))
			fprintf(of, "\tasm \"iret\"\n");
		else
			fprintf(of, "\tasm \"iret\"\n");
	} else {
		if (!stmt($8, -1, -1))
			fprintf(of, "\tret 0\n");
	}
	fprintf(of, "}\n\n");
};

attr_typed_decl: attrspec type_and_ident_noattr typed_decl_rest
{
	/* __attribute__((xxx)) type ident ... - attributes already set by attrspec */
};

attrspec: ATTRIBUTE '(' '(' attrreset attrlist ')' ')';

type_and_ident_noattr: type IDENT
{
	parsed_type = $1;
	strcpy(parsed_ident, $2->u.v);
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
         | EXTERN type IDENT '[' NUM ']' ';'
{
	/* Extern array with size - register as pointer */
	if ($2 == NIL)
		die("invalid void extern array");
	varaddextern($3->u.v, IDIR($2), 1);
}
         | EXTERN STRUCT IDENT IDENT ';'
{
	/* Extern struct variable: extern struct foo bar; */
	int idx = structfind($3->u.v);
	if (idx < 0)
		die("unknown struct type");
	unsigned styp = (idx << 3) + STRUCT_T;
	varaddextern($4->u.v, styp, 0);
}
         | EXTERN STRUCT IDENT IDENT '[' ']' ';'
{
	/* Extern struct array without size: extern struct foo bar[]; */
	int idx = structfind($3->u.v);
	if (idx < 0)
		die("unknown struct type");
	unsigned styp = (idx << 3) + STRUCT_T;
	varaddextern($4->u.v, IDIR(styp), 1);
}
         | EXTERN STRUCT IDENT '*' IDENT ';'
{
	/* Extern struct pointer: extern struct foo *bar; */
	int idx = structfind($3->u.v);
	if (idx < 0)
		die("unknown struct type");
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
	 * MiniC just records the return type. */
	if ($2 == NIL)
		varadd($3->u.v, 1, FUNC(NIL), 0);
	else
		varadd($3->u.v, 1, FUNC($2), 0);
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
		} else if (n->op == 'A') {
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
		varaddextern(n->u.v, t, n->op == 'A' ? 1 : 0);
	}
}
         ;

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
	/* Create typedef to the struct */
	int idx = curstruct;
	curstruct = -1;
	typhadd($4->u.v, (idx << 3) + STRUCT_T);
}
             ;

typedefstructstart: STRUCT '{'
{
	/* Anonymous struct typedef */
	char anonname[32];
	sprintf(anonname, "__typedef_anon_s_%d", typedefanoncount++);
	curstruct = structadd(anonname, 0);
}
                  | STRUCT IDENT '{'
{
	/* Tagged struct typedef */
	curstruct = structadd($2->u.v, 0);
}
                  ;

typedefunion: typedefunionstart smembers '}' IDENT ';'
{
	/* Create typedef to the union */
	int idx = curstruct;
	curstruct = -1;
	typhadd($4->u.v, (idx << 3) + UNION_T);
}
            ;

typedefunionstart: UNION '{'
{
	/* Anonymous union typedef */
	char anonname[32];
	sprintf(anonname, "__typedef_anon_u_%d", typedefanoncount++);
	curstruct = structadd(anonname, 1);
}
                 | UNION IDENT '{'
{
	/* Tagged union typedef */
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
	else
		structaddmember(curstruct, $3->u.v, $2);
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
	curstruct = structstk[--structstksp];
	hoistanonymous(curstruct, idx);
}
         | nested_s_begin smembers '}' IDENT ';'
{
	/* Named nested struct member: `struct { ... } name;`. */
	int idx = curstruct;
	curstruct = structstk[--structstksp];
	structaddmember(curstruct, $4->u.v, (idx << 3) + STRUCT_T);
}
         | nested_u_begin smembers '}' ';'
{
	/* Anonymous nested union: hoist its members into the parent. */
	int idx = curstruct;
	curstruct = structstk[--structstksp];
	hoistanonymous(curstruct, idx);
}
         | nested_u_begin smembers '}' IDENT ';'
{
	/* Named nested union member: `union { ... } name;`. */
	int idx = curstruct;
	curstruct = structstk[--structstksp];
	structaddmember(curstruct, $4->u.v, (idx << 3) + UNION_T);
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
	/* `static` storage class: parse-only no-op for now (single-TU compilation).
	 * Affects linkage in real C; MiniC emits all symbols equivalently. */
}
          | INLINE type_and_ident typed_decl_rest
{
	/* `inline` is a hint; MiniC emits the function normally. */
}
          | STATIC INLINE type_and_ident typed_decl_rest
{
	/* `static inline` — same treatment. */
}
          | INLINE STATIC type_and_ident typed_decl_rest
{
	/* `inline static` — same treatment. */
}
          | STATIC attrspec type_and_ident_noattr typed_decl_rest
{
	/* `static __attribute__((xxx)) type ident ...` — MicroPython spells
	 * `static __attribute__((noreturn)) void f(...)`.  attrspec set the
	 * attribute flags; type_and_ident_noattr does NOT reset them. */
};

type_and_ident: type IDENT
{
	cur_fn_interrupt = 0;
	cur_fn_weak = 0;
	parsed_type = $1;
	strcpy(parsed_ident, $2->u.v);
}
              | type attropt IDENT
{
	/* type __attribute__((xxx)) ident - attributes already set by attropt */
	parsed_type = $1;
	strcpy(parsed_ident, $3->u.v);
};

typed_decl_rest: ansi_func_proto '{' dcls stmts '}'
{
	/* ANSI function body */
	if (cur_fn_interrupt) {
		/* Interrupt handler - emit iret */
		if (!stmt($4, -1, -1))
			fprintf(of, "\tasm \"iret\"\n");
		else
			fprintf(of, "\tasm \"iret\"\n");
	} else {
		if (!stmt($4, -1, -1)) {
			if (curfntyp == NIL)
				fprintf(of, "\tret\n");
			else
				fprintf(of, "\tret 0\n");
		}
	}
	fprintf(of, "}\n\n");
}
               | ansi_proto_register ';'
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
	if (nglo == NGlo)
		die("too many string literals");
	emit_zero_init(buf, parsed_type);
	ini[nglo] = alloc(strlen(buf) + 1);
	strcpy(ini[nglo], buf);
	strcpy(gloname[nglo], parsed_ident);
	varadd(parsed_ident, nglo++, parsed_type, 0);
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
               | '=' '(' NUM ')' ';'             { emit_global_int_init($3->u.n); }
               | '=' '-' NUM ';'                 { emit_global_int_init(-$3->u.n); }
               | '=' '(' '-' NUM ')' ';'         { emit_global_int_init(-$4->u.n); }
               | '=' gaggr ';'                   { emit_global_aggregate(parsed_type, parsed_ident, $2); }
               | '[' NUM ']' ';'
{
	/* Global array of basic type: emit a zero-filled data block.
	 * QBE syntax: `data $name = align N { z TOTAL_BYTES }`. */
	char buf[64];
	int elemsz, total;
	if (parsed_type == NIL)
		die("invalid void array");
	if (nglo == NGlo)
		die("too many globals");
	elemsz = SIZE(parsed_type);
	total = elemsz * $2->u.n;
	sprintf(buf, "align %d { z %d }", iralign(parsed_type), total);
	ini[nglo] = alloc(strlen(buf) + 1);
	strcpy(ini[nglo], buf);
	strcpy(gloname[nglo], parsed_ident);
	maybe_mark_huge_global(nglo, parsed_ident, total);
	/* Register as pointer to element type with array flag set. */
	varadd(parsed_ident, nglo++, IDIR(parsed_type), 1);
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
	if (parsed_type == NIL)
		die("invalid void declaration");
	/* First name: emit as plain global. */
	if (nglo == NGlo)
		die("too many globals");
	emit_zero_init(buf, parsed_type);
	ini[nglo] = alloc(strlen(buf) + 1);
	strcpy(ini[nglo], buf);
	strcpy(gloname[nglo], parsed_ident);
	varadd(parsed_ident, nglo++, parsed_type, 0);
	for (n = $2; n; n = n->r) {
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
		} else {
			varadd(n->u.v, 1, FUNC(parsed_type), 0);
		}
	}
}
               | '[' ']' '=' '{' sai_init_clear sai_list opt_trailing_comma '}' ';'
{
	/* TYP NAME[] = { ... };  For struct types, walk the struct
	 * members.  For pointer types, emit one `l $gloN`/`l 0` per
	 * item.  Used for `struct charinfo chars[] = {...}` and for
	 * `static char *msgs[] = {"a","b","c"}` (handled by the static
	 * block-scope rule below). */
	if (KIND(parsed_type) == STRUCT_T)
		emit_struct_array_data(DREF(parsed_type), parsed_ident);
	else if (KIND(parsed_type) == PTR)
		emit_pointer_array_data(parsed_type, parsed_ident);
	else
		emit_scalar_array_data(parsed_type, parsed_ident);
}
               ;

sai_init_clear: { sai_clear(); };

opt_trailing_comma: | ',';

sai_list: sai_item
        | sai_list ',' sai_item
        ;

sai_item: expr                 { sai_add_expr($1); }
        | '{' sai_list opt_trailing_comma '}' { }
        ;

ansi_proto_register: '(' init_ansi par0 ')'
{
	/* Prototype-only registration: register function type, no IR emission. */
	curfntyp = parsed_type;
	varadd(parsed_ident, 1, FUNC(curfntyp), 0);
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
	strncpy(cur_fn_name, parsed_ident, NString - 1);
	cur_fn_name[NString - 1] = 0;
	varadd(parsed_ident, 1, FUNC(curfntyp), 0);

	/* Struct/union return-by-value: lower to a hidden first pointer
	 * parameter (caller-allocated result storage) plus a pointer
	 * return.  See the cur_fn_sret notes near the top of the file. */
	cur_fn_sret = (KIND(curfntyp) == STRUCT_T || KIND(curfntyp) == UNION_T);
	cur_fn_sret_ctyp = curfntyp;

	if (cur_fn_sret)
		fprintf(of, "export function %c $%s(", DATAPTR_T(), parsed_ident);
	else if (curfntyp == NIL)
		fprintf(of, "export function $%s(", parsed_ident);
	else
		fprintf(of, "export function %c $%s(", irtyp_ret(curfntyp), parsed_ident);
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
	if (cur_fn_sret) {
		/* Spill the hidden pointer to a fixed-name slot so `ret` can
		 * reload it from whichever basic block it lands in. */
		fprintf(of, "\t%%__sret =%c alloc4 %d\n", ALLOC_T(), DATAPTR_SZ());
		fprintf(of, "\tstore%c %%t0, %%__sret\n", DATAPTR_T());
	}
	for (t = (cur_fn_sret ? 1 : 0), n=$3; n; t++, n=n->r) {
		s = varget(n->u.v);
		m = SIZE(s->ctyp);
		fprintf(of, "\t%%%s =%c alloc%d %d\n", n->u.v, ALLOC_T(), iralign(s->ctyp), m);
		fprintf(of, "\tstore%c %%t%d", irtyp(s->ctyp), t);
		fprintf(of, ", %%%s\n", n->u.v);
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
	if (cur_fn_interrupt) {
		/* Interrupt handler - emit iret instead of ret */
		if (!stmt($8, -1, -1))
			fprintf(of, "\tasm \"iret\"\n");
		else
			fprintf(of, "\tasm \"iret\"\n");
	} else {
		if (!stmt($8, -1, -1))
			fprintf(of, "\tret 0\n");
	}
	fprintf(of, "}\n\n");
};

prot_knr: IDENT '(' par0 ')'
{
	Symb *s;
	Node *n;
	int t, m;

	curfntyp = INT;
	strncpy(cur_fn_name, $1->u.v, NString - 1);
	cur_fn_name[NString - 1] = 0;
	varadd($1->u.v, 1, FUNC(INT), 0);
	fprintf(of, "export function w $%s(", $1->u.v);
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
		fprintf(of, "\t%%%s =%c alloc%d %d\n", n->u.v, ALLOC_T(), iralign(s->ctyp), m);
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
    | type ',' par1       { $$ = abstract_param($1, $3); }
    | type                { $$ = abstract_param($1, 0); }
    | ELLIPSIS            { $$ = 0; /* variadic marker: ... proto only, no IR */ }
    | type '(' '*' IDENT ')' '(' fptpar0 ')' ',' par1 {
        /* Function pointer parameter: int (*callback)(int, int), ... */
        unsigned fptr_type = IDIR(FUNC($1));
        $$ = param($4->u.v, fptr_type, $10);
    }
    | type '(' '*' IDENT ')' '(' fptpar0 ')' {
        /* Function pointer parameter: int (*callback)(int, int) */
        unsigned fptr_type = IDIR(FUNC($1));
        $$ = param($4->u.v, fptr_type, 0);
    }
    ;

fptpar0: fptpar1
       |                  { $$ = 0; }
       ;
fptpar1: type ',' fptpar1        { $$ = 0; }
       | type                    { $$ = 0; }
       | type IDENT ',' fptpar1  { $$ = 0; }
       | type IDENT              { $$ = 0; }
       | ELLIPSIS                { $$ = 0; }
       ;

dcls:
    | dcls type IDENT ';'
{
	int s, i;
	char *v;

	if ($2 == NIL)
		die("invalid void declaration");
	v = $3->u.v;
	if (g_td_arraydim > 0) {
		/* Array-typedef local (`jmp_buf env;`): allocate the whole
		 * array and register it as an array so it decays to a
		 * pointer-to-element on use. */
		int total = SIZE(g_td_arrayelem) * g_td_arraydim;
		varadd(v, 0, IDIR(g_td_arrayelem), 1);
		fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(),
			iralign(g_td_arrayelem), total);
	} else {
	s = SIZE($2);
	varadd(v, 0, $2, 0);
	fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign($2), s);

	/* Zero-initialize struct/union for bitfield support */
	if (KIND($2) == STRUCT_T || KIND($2) == UNION_T) {
		/* Store 0 to each word of the struct */
		for (i = 0; i < s; i += 4) {
			if (i == 0)
				fprintf(of, "\tstorew 0, %%%s\n", v);
			else
				fprintf(of, "\t%%_zinit%d =w add %%%s, %d\n\tstorew 0, %%_zinit%d\n", tmp, v, i, tmp);
			tmp++;
		}
	}
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
	v = $3->u.v;
	s = SIZE($2);
	varadd(v, 0, $2, 0);
	fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign($2), s);
	/* Evaluate initializer as `IDENT = expr` */
	init_node = mknode('=', $3, $5);
	expr(init_node);
}
    | dcls type IDENT ',' ext_decllist ';' { emit_local_multi_decl($2, $3->u.v, $5); }
    | dcls type IDENT '[' expr ']' ',' ext_decllist ';'
{
	Node *first = kr_array_node($3->u.v, const_eval($5));
	first->r = $8;
	emit_local_multi_decl_full($2, first);
}
    | dcls type IDENT '(' ')' ',' ext_decllist ';'
{
	/* `T name1(), name2, ...;` — first declarator is K&R proto.
	 * Build a 'F' node and chain. */
	Node *first = kr_name_node($3->u.v, 'F');
	first->r = $7;
	emit_local_multi_decl_full($2, first);
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
	emit_zero_init(buf, $3);
	emit_static_local($4->u.v, $3, 0, buf);
}
    | dcls STATIC type IDENT '=' expr ';' { emit_static_local_init($3, $4, $6); }
    | dcls STATIC type IDENT '[' ']' '=' '{' sai_init_clear sai_list opt_trailing_comma '}' ';'
{
	emit_static_pointer_array($3, $4->u.v);
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
		else if (n->op == 'A' || n->op == 'P')
			t = IDIR($3);
		else
			t = $3;
		varaddextern(n->u.v, t, n->op == 'A' ? 1 : 0);
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
	v = $3->u.v;
	n = const_eval($5);  /* array size */
	s = SIZE($2);  /* element size */
	total = s * n;
	varadd(v, 0, IDIR($2), 1);  /* Store as pointer to element type - IS AN ARRAY */
	var_set_arraybytes(v, total);
	fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign($2), total);
}
    | dcls type IDENT '[' expr ']' '=' '{' initlist '}' ';'
{
	/* Array declaration with initialization.  Dimension is a
	 * constant-expression (const_eval folds sizeof / arithmetic). */
	int s, n, total;
	char *v;

	if ($2 == NIL)
		die("invalid void array");
	v = $3->u.v;
	n = const_eval($5);  /* array size */
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
    | dcls type '(' '*' IDENT ')' '(' fptpar0 ')' ';'
{
	/* Function pointer declaration: int (*fptr)(int, int); */
	char *v;
	unsigned fptr_type;

	/* void-returning function pointer (`void (*fp)(...)`) is legal. */
	v = $5->u.v;
	fptr_type = IDIR(FUNC($2));  /* Pointer to function returning type */
	varadd(v, 0, fptr_type, 0);  /* Not an array */
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
	emit_local_multi_decl_full($2, $11);
}
    | dcls STATIC_ASSERT '(' expr ',' STR ')' ';'
{
	/* _Static_assert in local scope (general const-expr condition). */
	if (constfoldable($4) && const_eval($4) == 0)
		die("static assertion failed");
}
    ;

idlist: IDENT                  { $$ = $1; $$->r = 0; }
      | IDENT ',' idlist       { $$ = $1; $$->r = $3; }
      ;

inititem: pref                        { $$ = $1; }
        | '.' IDENT '=' pref          { $$ = mknode('D', $4, $2); }
        | '[' NUM ']' '=' pref        { $$ = mknode('d', $5, $2); }
        ;

initlist: inititem                    { $$ = mknode(0, $1, 0); }
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

type: type TFAR '*'                  { $$ = IDIR_FAR($1); g_td_arraydim = 0; }
        | type '*' TFAR              { $$ = IDIR_FAR($1); g_td_arraydim = 0; }
        | type '*'                   { $$ = ($1 & FAR) ? IDIR_FAR($1 & ~FAR) : IDIR($1); g_td_arraydim = 0; }
        | type '*' CONST             { $$ = ($1 & FAR) ? IDIR_FAR($1 & ~FAR) : IDIR($1); g_td_arraydim = 0; }
        | type '*' VOLATILE          { $$ = ($1 & FAR) ? IDIR_FAR($1 & ~FAR) : IDIR($1); g_td_arraydim = 0; }
        | TFAR type                  { $$ = $2 | FAR; }
        | TCHAR                      { $$ = CHR; }
    | TSHORT                     { $$ = INT | SHORT; }
    | TINT     { $$ = INT; }
    | TLNG     { $$ = LNG; }
    | TLNGLNG  { $$ = LNG; /* long long aliases to 32-bit long on i8086 */ }
    | TBOOL    { $$ = CHR | UNSIGNED; }
    | TFLOAT   { $$ = INT | FLOAT; }
    | TDOUBLE  { $$ = LNG | FLOAT; }
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
    | CONST TUNSIGNED TCHAR    { $$ = CHR | UNSIGNED; }
    | CONST TUNSIGNED TSHORT   { $$ = INT | SHORT | UNSIGNED; }
    | CONST TUNSIGNED TINT     { $$ = INT | UNSIGNED; }
    | CONST TUNSIGNED TLNG     { $$ = LNG | UNSIGNED; }
    | CONST TUNSIGNED TLNGLNG  { $$ = LNG | UNSIGNED; }
    | CONST TUNSIGNED          { $$ = INT | UNSIGNED; }
    | VOLATILE TVOID        { $$ = NIL; /* volatile void (e.g. volatile void *) */ }
    | VOLATILE TCHAR        { $$ = CHR; }
    | VOLATILE TSHORT       { $$ = INT | SHORT; }
    | VOLATILE TINT         { $$ = INT; }
    | VOLATILE TLNG         { $$ = LNG; }
    | VOLATILE TLNGLNG      { $$ = LNG; }
    | VOLATILE TUNSIGNED TCHAR    { $$ = CHR | UNSIGNED; }
    | VOLATILE TUNSIGNED TSHORT   { $$ = INT | SHORT | UNSIGNED; }
    | VOLATILE TUNSIGNED TINT     { $$ = INT | UNSIGNED; }
    | VOLATILE TUNSIGNED TLNG     { $$ = LNG | UNSIGNED; }
    | VOLATILE TUNSIGNED TLNGLNG  { $$ = LNG | UNSIGNED; }
    | VOLATILE TUNSIGNED          { $$ = INT | UNSIGNED; }
    | CONST TNAME    { $$ = $2; }
    | VOLATILE TNAME { $$ = $2; }
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
    | VOLATILE STRUCT IDENT {
        int idx = structfind($3->u.v);
        if (idx < 0)
            idx = structadd_forward($3->u.v, 0);
        $$ = (idx << 3) + STRUCT_T;
    }
    | CONST UNION IDENT {
        int idx = structfind($3->u.v);
        if (idx < 0)
            idx = structadd_forward($3->u.v, 1);
        $$ = (idx << 3) + UNION_T;
    }
    | VOLATILE UNION IDENT {
        int idx = structfind($3->u.v);
        if (idx < 0)
            idx = structadd_forward($3->u.v, 1);
        $$ = (idx << 3) + UNION_T;
    }
    | ENUM IDENT           { $$ = INT; /* enum Tag: an enumeration value is an int */ }
    | CONST ENUM IDENT     { $$ = INT; }
    | VOLATILE ENUM IDENT  { $$ = INT; }
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
    | type IDENT ';'                 {
        int s;
        char *v;
        if ($1 == NIL)
            die("invalid void declaration");
        v = $2->u.v;
        s = SIZE($1);
        varadd(v, 0, $1, 0);
        fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign($1), s);
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
        v = $2->u.v;
        s = SIZE($1);
        varadd(v, 0, $1, 0);
        fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign($1), s);
        init_node = mknode('=', $2, $4);
        $$ = mkstmt(Expr, init_node, 0, 0);
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
        v = $2->u.v;
        n = const_eval($4);
        s = SIZE($1);
        total = s * n;
        varadd(v, 0, IDIR($1), 1);
        var_set_arraybytes(v, total);
        fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign($1), total);
        $$ = 0;
    }
    | type IDENT ',' ext_decllist ';' {
        /* Block-scoped multi-variable decl with full per-declarator
         * decoration support (`*`, `[]`, `[N]`, `()`).  Reuses the
         * same helper as the dcls-context multi-decl rule. */
        emit_local_multi_decl($1, $2->u.v, $4);
        $$ = 0;
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
    | FOR '(' type IDENT '=' expr ';' exp0 ';' exp0 ')' stmt
                                     {
        int s;
        char *v;
        Node *init_expr;
        if ($3 == NIL)
            die("invalid void declaration");
        v = $4->u.v;
        s = SIZE($3);
        varadd(v, 0, $3, 0);
        fprintf(of, "\t%%%s =%c alloc%d %d\n", v, ALLOC_T(), iralign($3), s);
        init_expr = mknode('=', $4, $6);
        $$ = mkfor(init_expr, $8, $10, $12);
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
    | SIZEOF '(' IDENT ')' {
        Symb *vs = varget($3->u.v);
        int ab = var_arraybytes($3->u.v);
        $$ = mknode('N', 0, 0);
        if (!vs)
            die("sizeof on undeclared identifier");
        /* For an array variable, report the whole-array byte size
         * (varh stores arrays as pointer-to-element, so SIZE(ctyp)
         * would otherwise give the pointer size). */
        $$->u.n = ab > 0 ? ab : SIZE(vs->ctyp);
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
	t = yylex_inner();
	if (t == '{')
		brace_depth++;
	else if (t == '}' && brace_depth > 0 && --brace_depth == 0)
		pending_varclr = 1;
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
				for (i = 0; i < 3; i++) {
					if (c == 'u' || c == 'U' || c == 'l' || c == 'L')
						c = getchar();
					else
						break;
				}
				ungetc(c, stdin);
				yylval.n = mknode('N', 0, 0);
				yylval.n->u.n = (int)n;
				return NUM;
			} else if (c >= '0' && c <= '7') {
				/* Octal */
				while (c >= '0' && c <= '7') {
					n *= 8;
					n += c - '0';
					c = getchar();
				}
				/* Consume integer suffixes: U, L, UL, LU, ULL, LLU, etc. */
				for (i = 0; i < 3; i++) {
					if (c == 'u' || c == 'U' || c == 'l' || c == 'L')
						c = getchar();
					else
						break;
				}
				ungetc(c, stdin);
				yylval.n = mknode('N', 0, 0);
				yylval.n->u.n = (int)n;
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
			c = getchar();  /* Consume float suffix */
		} else if (c == 'l' || c == 'L') {
			if (isfloat) {
				/* `1.0L` / `1.0l` is long double — keep as double. */
				c = getchar();
			} else {
				/* Integer long suffix; consume one or two L/l. */
				c = getchar();
				if (c == 'l' || c == 'L')
					c = getchar();
				/* Optional trailing U/u for `LU` etc. */
				if (c == 'u' || c == 'U')
					c = getchar();
			}
		} else if (c == 'u' || c == 'U') {
			c = getchar();
			/* Optional trailing L/l for `UL` etc. */
			if (c == 'l' || c == 'L') {
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
			return FNUM;
		} else {
			yylval.n = mknode('N', 0, 0);
			yylval.n->u.n = (int)n;
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
					yylval.n = vn;
					return IDENT;
				}
				yylval.u = tnctyp;
				return TNAME;
			}
			yylval.n = vn;
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
			fprintf(of, "data $%s = %s\n", gloname[i], ini[i]);
		else
			fprintf(of, "data $glo%d = %s\n", i, ini[i]);
	}
	return 0;
}
