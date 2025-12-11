%{

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	NString = 32,
	NGlo = 256,
	NVar = 512,
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

#define SHORT     (1 << 16)  /* Short flag for types */
#define UNSIGNED  (1 << 17)  /* Unsigned flag for types */
#define FLOAT     (1 << 18)  /* Float flag for types (float=INT|FLOAT, double=LNG|FLOAT) */
#define FAR       (1 << 24)  /* Far pointer flag (32-bit segment:offset) */
#define IDIR(x) (((x) << 3) + PTR)
#define IDIR_FAR(x) ((((x) << 3) + PTR) | FAR)  /* Far pointer to type */
#define FUNC(x) (((x) << 3) + FUN)
#define DREF(x) ((x) >> 3)
#define KIND(x) ((x) & 7)
#define ISUNSIGNED(x) ((x) & UNSIGNED)
#define ISFLOAT(x) ((x) & FLOAT)
#define ISFAR(x) ((x) & FAR)
#define BASETYPE(x) (KIND(x) & ~UNSIGNED)
#define SIZE(x)                                    \
	(KIND(x) == NIL ? (die("void has no size"), 0) : \
	 KIND(x) == CHR ? 1 :  \
	 ((x) & SHORT) ? 2 :  \
	 KIND(x) == INT ? 4 : \
	 KIND(x) == LNG ? 8 : \
	 (KIND(x) == STRUCT_T || KIND(x) == UNION_T) ? structh[DREF(x)].size : \
	 (KIND(x) == PTR && ISFAR(x)) ? 4 : 8)  /* Far pointers are 4 bytes */

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
	} t;
	void *p1, *p2, *p3;
	int val; /* for case values */
	char label[NString]; /* for goto target and label name */
};

int yylex(void), yyerror(char *);
Symb expr(Node *), lval(Node *);
void branch(Node *, int, int);
int stmt(Stmt *, int);

FILE *of;
int line;
int lbl, tmp, nglo;
int enumval; /* Current enum value */
char *ini[NGlo];
struct {
	char v[NString];
	unsigned ctyp;
	int glo;
	int enumconst; /* -2 means it's an enum constant, glo stores the value */
	int isarray; /* 1 if this is an array, 0 if it's a regular variable or pointer */
	int isextern; /* 1 if this is an extern declaration */
} varh[NVar];

/* Typedef table */
enum { NTyp = 128 };
struct {
	char v[NString];
	unsigned ctyp;
} typh[NTyp];

/* Struct/union member */
enum { NMember = 256 };
struct Member {
	char name[NString];
	unsigned ctyp;
	int offset;      /* Byte offset within struct */
	int bitwidth;    /* Bit width (0 = not a bitfield) */
	int bitoffset;   /* Bit offset within the storage unit */
};

/* Struct/union definition table */
enum { NStruct = 64 };
struct {
	char name[NString];
	int isunion;  /* 1 for union, 0 for struct */
	int nmembers;
	struct Member members[16];  /* Max 16 members per struct */
	int size;
	int curbfoffset;  /* Current bit offset for bitfield packing */
	int curbfbase;    /* Byte offset of current bitfield storage unit */
} structh[NStruct];
int nstruct = 0;
int curstruct = -1;  /* Index of struct currently being defined */
int parentstruct = -1;  /* Parent struct for anonymous members */
int typedefanoncount = 0;  /* Counter for anonymous typedef structs/unions */
int clit = 0;  /* Counter for compound literal temporaries */
unsigned curfntyp = INT;  /* Current function return type (defaults to INT for K&R style) */
unsigned parsed_type = INT;  /* Stores type parsed in typed_decl for later use */
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
		if (!varh[h].glo && !varh[h].enumconst)
			varh[h].v[0] = 0;
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
			return;
		}
		if (strcmp(varh[h].v, v) == 0) {
			/* Allow definition after extern declaration */
			if (varh[h].isextern && glo > 0) {
				varh[h].glo = glo;  /* Update to actual glo value */
				varh[h].isextern = 0;  /* Now it's a real definition */
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

int
structadd(char *name, int isunion)
{
	int idx;

	if (nstruct >= NStruct)
		die("too many struct/union definitions");

	idx = structfind(name);
	if (idx >= 0)
		die("struct/union already defined");

	idx = nstruct++;
	strcpy(structh[idx].name, name);
	structh[idx].isunion = isunion;
	structh[idx].nmembers = 0;
	structh[idx].size = 0;
	structh[idx].curbfoffset = 0;  /* No bitfield in progress */
	structh[idx].curbfbase = 0;
	return idx;
}

void
structaddmember(int sidx, char *name, unsigned ctyp)
{
	int i;
	struct Member *m;

	if (structh[sidx].nmembers >= 16)
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

/* Add a bitfield member to a struct */
void
structaddbitfield(int sidx, char *name, unsigned ctyp, int width)
{
	int i;
	struct Member *m;
	int unitsize;      /* Size of storage unit in bits */
	int unitbytes;     /* Size of storage unit in bytes */

	if (structh[sidx].nmembers >= 16)
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

	h0 = hash(v);
	h = h0;
	do {
		if (typh[h].v[0] == 0) {
			strcpy(typh[h].v, v);
			typh[h].ctyp = ctyp;
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

	h0 = hash(v);
	h = h0;
	do {
		if (strcmp(typh[h].v, v) == 0) {
			*ctyp = typh[h].ctyp;
			return 1;
		}
		if (typh[h].v[0] == 0)
			return 0;
		h = (h+1) % NTyp;
	} while(h != h0);
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

	default:
		die("unsupported operation in constant expression");
		return 0;
	}
}

char
irtyp(unsigned ctyp)
{
	/* Check pointer/function types first - they are always 'l' (64-bit for QBE) */
	/* This must come before ISFLOAT check because type composition can */
	/* accidentally set the FLOAT bit */
	/* Note: Far pointers are 32-bit (segment:offset) but we use 'l' in QBE IL */
	/* The i8086 backend handles the actual size difference */
	if (KIND(ctyp) == PTR || KIND(ctyp) == FUN)
		return 'l';
	if (ISFLOAT(ctyp)) {
		if (KIND(ctyp) == LNG) return 'd';  /* double */
		return 's';  /* float */
	}
	if (SIZE(ctyp) == 1) return 'b';
	if (ctyp & SHORT) return 'h';
	if (SIZE(ctyp) == 8) return 'l';
	return 'w';
}

/* QBE return type - for function return types which must be w, l, s, d */
char
irtyp_ret(unsigned ctyp)
{
	if (KIND(ctyp) == PTR || KIND(ctyp) == FUN)
		return 'l';
	if (ISFLOAT(ctyp)) {
		if (KIND(ctyp) == LNG) return 'd';  /* double */
		return 's';  /* float */
	}
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
		return LNG;
	}

Scale:
	sz = SIZE(DREF(l->ctyp));
	if (r->t == Con)
		r->u.n *= sz;
	else {
		if (irtyp(r->ctyp) != 'l')
			sext(r);
		fprintf(of, "\t%%t%d =l mul %d, ", tmp, sz);
		psymb(*r);
		fprintf(of, "\n");
		r->u.n = tmp++;
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

	/* Far pointer loads - use loadfb/loadfh/loadfw */
	if (t == 'b') {
		fprintf(of, " =w loadfb ");
	} else if (t == 'h') {
		fprintf(of, " =w loadfh ");
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
	/* Far pointer stores - use storefb/storefh/storefw */
	if (t == 'b') {
		fprintf(of, "storefb ");
	} else if (t == 'h') {
		fprintf(of, "storefh ");
	} else {
		fprintf(of, "storefw ");  /* Word (16-bit) store through far ptr */
	}
	psymb(d);  /* value to store */
	fprintf(of, ", ");
	psymb(s);  /* far pointer address */
	fprintf(of, "\n");
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
			sr->ctyp = DREF(fptr_type);     /* return_type */

			/* Load the function pointer value */
			fptr.t = Tmp;
			fptr.u.n = tmp++;
			fptr.ctyp = ft;
			load(fptr, *sv);

			/* Evaluate all arguments */
			for (a=n->r; a; a=a->r)
				a->u.s = expr(a->l);

			/* Generate indirect call */
			if (sr->ctyp == NIL) {
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
				fprintf(of, "%c ", irtyp(a->u.s.ctyp));
				psymb(a->u.s);
				fprintf(of, ", ");
			}
			fprintf(of, "...)\n");
			return;
		}
		if (KIND(ft) != FUN)
			die("invalid call");
	} else
		ft = FUNC(INT);
	sr->ctyp = DREF(ft);
	for (a=n->r; a; a=a->r)
		a->u.s = expr(a->l);
	if (sr->ctyp == NIL) {
		/* Void function - no return value */
		fprintf(of, "\tcall $%s(", f);
	} else {
		fprintf(of, "\t");
		psymb(*sr);
		fprintf(of, " =%c call $%s(", irtyp_ret(sr->ctyp), f);
	}
	for (a=n->r; a; a=a->r) {
		fprintf(of, "%c ", irtyp(a->u.s.ctyp));
		psymb(a->u.s);
		fprintf(of, ", ");
	}
	fprintf(of, "...)\n");
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
		/* Ternary operator: cond ? true_expr : false_expr */
		l = lbl;
		lbl += 3;
		/* Evaluate condition */
		s0 = expr(n->l);
		fprintf(of, "\tjnz ");
		psymb(s0);
		fprintf(of, ", @l%d, @l%d\n", l, l+1);
		/* True branch */
		fprintf(of, "@l%d\n", l);
		s0 = expr(n->r->l);  /* true expression */
		fprintf(of, "\tjmp @l%d\n", l+2);
		/* False branch */
		fprintf(of, "@l%d\n", l+1);
		s1 = expr(n->r->r);  /* false expression */
		fprintf(of, "\tjmp @l%d\n", l+2);
		/* Merge */
		fprintf(of, "@l%d\n", l+2);
		/* Type promotion */
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
		fprintf(of, " =%c phi @l%d ", irtyp(sr.ctyp), l);
		psymb(s0);
		fprintf(of, ", @l%d ", l+1);
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
			fprintf(of, " =l copy $%s\n", n->u.v);
		} else if (varh[hash(n->u.v)].isarray) {
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
			fprintf(of, "\t%%_clit%d =l alloc%d %d\n", clitnum, iralign(ctyp), s);

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
						fprintf(of, "\t%%t%d =l add %%_clit%d, %d\n", tmp, clitnum, j);
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
						fprintf(of, "\t%%t%d =l add %%_clit%d, %d\n", tmp, clitnum, m->offset);
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
						fprintf(of, "\t%%t%d =l add %%_clit%d, %d\n", tmp, clitnum, i * elems);
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
				fprintf(of, " =l copy %%_clit%d\n", clitnum);
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

			/* Evaluate function pointer expression */
			fptr = expr(n->l);

			/* Check it's a function pointer */
			if (KIND(fptr.ctyp) != PTR || KIND(DREF(fptr.ctyp)) != FUN)
				die("invalid indirect call - not a function pointer");

			/* Get return type */
			fptr_type = DREF(fptr.ctyp);  /* FUN(return_type) */
			sr.ctyp = DREF(fptr_type);     /* return_type */

			/* Evaluate all arguments */
			for (a=n->r; a; a=a->r)
				a->u.s = expr(a->l);

			/* Generate indirect call */
			fprintf(of, "\t");
			psymb(sr);
			fprintf(of, " =%c call ", irtyp_ret(sr.ctyp));
			psymb(fptr);
			fprintf(of, "(");
			for (a=n->r; a; a=a->r) {
				fprintf(of, "%c ", irtyp(a->u.s.ctyp));
				psymb(a->u.s);
				fprintf(of, ", ");
			}
			fprintf(of, "...)\n");
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

			/* Compute member address: struct_addr + offset */
			if (m->offset > 0) {
				addr.t = Tmp;
				addr.u.n = tmp++;
				addr.ctyp = IDIR(m->ctyp);
				fprintf(of, "\t");
				psymb(addr);
				fprintf(of, " =l add ");
				psymb(s0);
				fprintf(of, ", %d\n", m->offset);
			} else {
				/* Offset 0, just use struct address */
				addr = s0;
				addr.ctyp = IDIR(m->ctyp);
			}

			/* Load value from member address */
			sr.t = Tmp;
			sr.u.n = tmp++;
			sr.ctyp = m->ctyp;
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
		fprintf(of, " =w ceq%c ", irtyp(s0.ctyp));
		psymb(s0);
		fprintf(of, ", 0\n");
		break;

	case 'K':
		/* Cast expression: n->u.n is target type, n->l is expression */
		s0 = expr(n->l);
		sr.ctyp = n->u.n;
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
			/* Integer/pointer casts - just copy (bitwise) */
			fprintf(of, "\t");
			psymb(sr);
			fprintf(of, " =%c copy ", irtyp(sr.ctyp));
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

					/* Get the storage unit address */
					if (m->offset > 0) {
						addr.t = Tmp;
						addr.u.n = tmp++;
						addr.ctyp = IDIR(m->ctyp);
						fprintf(of, "\t");
						psymb(addr);
						fprintf(of, " =l add ");
						psymb(s_struct);
						fprintf(of, ", %d\n", m->offset);
					} else {
						addr = s_struct;
						addr.ctyp = IDIR(m->ctyp);
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
		} else if (KIND(s1.ctyp) == LNG && KIND(s0.ctyp) == INT && !ISFLOAT(s1.ctyp)) {
			sext(&s0);
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
		/* Allow assignment between signed/unsigned variants and float types */
		if ((s1.ctyp & ~FAR) != (s0.ctyp & ~FAR)
		    && !(KIND(s1.ctyp) == CHR && KIND(s0.ctyp) == INT)
		    && !((KIND(s1.ctyp) == KIND(s0.ctyp)) ||
		         ((KIND(s1.ctyp) & ~UNSIGNED) == (KIND(s0.ctyp) & ~UNSIGNED))
		         || ((KIND(s1.ctyp) & ~FLOAT) == (KIND(s0.ctyp) & ~FLOAT))))
			die("invalid assignment");
		/* Check if storing through a far pointer */
		if (ISFAR(s1.ctyp)) {
			char t = irtyp(s1.ctyp);
			if (t == 'b')
				fprintf(of, "\tstorefb ");
			else if (t == 'h')
				fprintf(of, "\tstorefh ");
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
		s0.ctyp = sl.ctyp & ~FAR;  /* Remove FAR for value type */
		/* Load current value (handle far pointer) */
		if (ISFAR(sl.ctyp)) {
			loadfar(s0, sl);
		} else {
			load(s0, sl);
		}
		s1.t = Con;
		s1.u.n = 1;
		s1.ctyp = INT;
		/* Compute new value */
		sr.ctyp = prom(o, &s0, &s1);
		fprintf(of, "\t");
		psymb(sr);
		fprintf(of, " =%c %s ", irtyp(sr.ctyp), o == '+' ? "add" : "sub");
		psymb(s0);
		fprintf(of, ", ");
		psymb(s1);
		fprintf(of, "\n");
		/* Store new value (handle far pointer) */
		if (ISFAR(sl.ctyp)) {
			char t = irtyp(sl.ctyp);
			if (t == 'b')
				fprintf(of, "\tstorefb ");
			else if (t == 'h')
				fprintf(of, "\tstorefh ");
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
		s0.ctyp = sl.ctyp & ~FAR;  /* Remove FAR for value type */
		/* Load current value (handle far pointer) */
		if (ISFAR(sl.ctyp)) {
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
			sprintf(ty, "%c", irtyp(sr.ctyp));
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
		fprintf(of, "\t%%t%d =l div ", tmp);
		psymb(sr);
		fprintf(of, ", %d\n", SIZE(DREF(s0.ctyp)));
		sr.u.n = tmp++;
	}
	if (n->op == 'P' || n->op == 'M') {
		/* Store new value (handle far pointer) */
		if (ISFAR(sl.ctyp)) {
			char t = irtyp(sl.ctyp);
			if (t == 'b')
				fprintf(of, "\tstorefb ");
			else if (t == 'h')
				fprintf(of, "\tstorefh ");
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
			fprintf(of, "\t%%_clit%d =l alloc%d %d\n", clitnum, iralign(ctyp), s);

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
						fprintf(of, "\t%%t%d =l add %%_clit%d, %d\n", tmp, clitnum, j);
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
						fprintf(of, "\t%%t%d =l add %%_clit%d, %d\n", tmp, clitnum, m->offset);
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
			fprintf(of, " =l copy %%_clit%d\n", clitnum);
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

			/* Compute member address: struct_addr + offset */
			if (m->offset > 0) {
				sr.t = Tmp;
				sr.u.n = tmp++;
				sr.ctyp = m->ctyp;  /* lval returns the type, not IDIR */
				fprintf(of, "\t");
				psymb(sr);
				fprintf(of, " =l add ");
				psymb(s0);
				fprintf(of, ", %d\n", m->offset);
			} else {
				/* Offset 0, just use struct address */
				sr = s0;
				sr.ctyp = m->ctyp;  /* lval returns the type, not IDIR */
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
		s = expr(n); /* TODO: insert comparison to 0 with proper type */
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
	}
}

int genswitchbody(Stmt *s, int brk, Stmt **cases, int *caselbl, int ncase);

int
genswitch(Symb val, Stmt *body, int brk)
{
	Stmt *cases[64];
	int ncase, defidx, i;
	int caselbl[64];

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
	genswitchbody(body, brk, cases, caselbl, ncase);

	return 0;
}

int
genswitchbody(Stmt *s, int brk, Stmt **cases, int *caselbl, int ncase)
{
	int i;

	if (!s)
		return 0;

	if (s->t == Seq) {
		int r1 = genswitchbody((Stmt*)s->p1, brk, cases, caselbl, ncase);
		int r2 = genswitchbody((Stmt*)s->p2, brk, cases, caselbl, ncase);
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
			return genswitchbody((Stmt*)s->p2, brk, cases, caselbl, ncase);
		return 0;
	} else {
		/* Regular statement - process normally */
		return stmt(s, brk);
	}
}

int
stmt(Stmt *s, int b)
{
	int l, r;
	Symb x;

	if (!s)
		return 0;

	switch (s->t) {
	case Ret:
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
		if (b < 0)
			die("continue not in loop");
		/* Use b-1 for continue target (loop start) */
		fprintf(of, "\tjmp @l%d\n", b-1);
		return 1;
	case Expr:
		expr(s->p1);
		return 0;
	case Seq:
		return stmt(s->p1, b) || stmt(s->p2, b);
	case If:
		l = lbl;
		lbl += 3;
		branch(s->p1, l, l+1);
		fprintf(of, "@l%d\n", l);
		if (!(r=stmt(s->p2, b)))
		if (s->p3)
			fprintf(of, "\tjmp @l%d\n", l+2);
		fprintf(of, "@l%d\n", l+1);
		if (s->p3)
		if (!(r &= stmt(s->p3, b)))
			fprintf(of, "@l%d\n", l+2);
		return s->p3 && r;
	case While:
		l = lbl;
		lbl += 3;
		fprintf(of, "@l%d\n", l);
		branch(s->p1, l+1, l+2);
		fprintf(of, "@l%d\n", l+1);
		/* Pass l for continue (will use l-1=loop start), l+2 for break */
		if (!stmt(s->p2, l+2))
			fprintf(of, "\tjmp @l%d\n", l);
		fprintf(of, "@l%d\n", l+2);
		return 0;
	case DoWhile:
		l = lbl;
		lbl += 3;
		fprintf(of, "@l%d\n", l);
		/* Pass l+1 for continue (test label), l+2 for break */
		if (!stmt(s->p1, l+2))
			fprintf(of, "\tjmp @l%d\n", l+1);
		fprintf(of, "@l%d\n", l+1);
		branch(s->p2, l, l+2);
		fprintf(of, "@l%d\n", l+2);
		return 0;
	case Switch:
		x = expr(s->p1);
		l = lbl++;
		genswitch(x, (Stmt*)s->p2, l);
		fprintf(of, "@l%d\n", l);
		return 0;
	case Case:
	case Default:
		/* These are handled within genswitch */
		if (s->p2)
			stmt((Stmt*)s->p2, b);
		return 0;
	case Goto:
		fprintf(of, "\tjmp @user_%s\n", s->label);
		return 1;
	case Label:
		fprintf(of, "@user_%s\n", s->label);
		return stmt(s->p1, b);
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

Stmt *
mkfor(Node *ini, Node *tst, Node *inc, Stmt *s)
{
	Stmt *s1, *s2;

	if (ini)
		s1 = mkstmt(Expr, ini, 0, 0);
	else
		s1 = 0;
	if (inc) {
		s2 = mkstmt(Expr, inc, 0, 0);
		s2 = mkstmt(Seq, s, s2, 0);
	} else
		s2 = s;
	if (!tst) {
		tst = mknode('N', 0, 0);
		tst->u.n = 1;
	}
	s2 = mkstmt(While, tst, s2, 0);
	if (s1)
		return mkstmt(Seq, s1, s2, 0);
	else
		return s2;
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
%token PP MM LE GE SIZEOF SHL SHR ARROW
%token ADDEQ SUBEQ MULEQ DIVEQ MODEQ
%token ANDEQ OREQ XOREQ SHLEQ SHREQ

%token TVOID TCHAR TSHORT TINT TLNG TLNGLNG TUNSIGNED TFLOAT TDOUBLE CONST VOLATILE TBOOL TFAR
%token IF ELSE WHILE DO FOR BREAK CONTINUE RETURN GOTO
%token ENUM SWITCH CASE DEFAULT TYPEDEF TNAME STRUCT UNION
%token INLINE STATIC EXTERN STATIC_ASSERT ALIGNOF ALIGNAS GENERIC

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
%type <s> stmt stmts
%type <n> expr exp0 pref post arg0 arg1 par0 par1 fptpar0 fptpar1 initlist inititem generic_list generic_assoc
%token <u> TNAME

%%

prog: kfunc prog | typed_decl prog | edcl prog | tdcl prog | sdcl prog | static_assert_dcl prog | externdcl prog | ;

edcl: enumstart enums '}' ';'
    ;

enumstart: ENUM IDENT '{'  { enumval = 0; }
         | ENUM '{'         { enumval = 0; }
         ;

enums: enum
     | enums ',' enum
     ;

enum: IDENT
{
	varadd($1->u.v, enumval, INT, 0);
	varh[hash($1->u.v)].enumconst = 1;
	enumval++;
}
    | IDENT '=' NUM
{
	enumval = $3->u.n;
	varadd($1->u.v, enumval, INT, 0);
	varh[hash($1->u.v)].enumconst = 1;
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
         ;

tdcl: TYPEDEF type IDENT ';'
{
	typhadd($3->u.v, $2);
}
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

static_assert_dcl: STATIC_ASSERT '(' NUM ',' STR ')' ';'
{
	/* _Static_assert(constant-expression, string-literal); */
	if ($3->u.n == 0) {
		/* Assertion failed */
		die("static assertion failed");
	}
	/* Assertion passed - no code generated */
}
    ;

sdcl: structstart smembers '}' ';'
{
	curstruct = -1;  /* Done defining this struct */
}
    ;

structstart: STRUCT IDENT '{'  { curstruct = structadd($2->u.v, 0); }
           | UNION IDENT '{'    { curstruct = structadd($2->u.v, 1); }
           ;

smembers:
        | smembers type IDENT ';'
{
	structaddmember(curstruct, $3->u.v, $2);
}
        | smembers type IDENT ':' NUM ';'
{
	structaddbitfield(curstruct, $3->u.v, $2, $5->u.n);
}
        | smembers anonstruct
        | smembers anonunion
        ;

anonstruct: anon_s_begin anonmembers anon_s_end
          ;

anonunion: anon_u_begin anonmembers anon_u_end
         ;

anon_s_begin: STRUCT '{'
{
	parentstruct = curstruct;
	curstruct = structadd("__anon_s", 0);
}
            ;

anon_u_begin: UNION '{'
{
	parentstruct = curstruct;
	curstruct = structadd("__anon_u", 1);
}
            ;

anon_s_end: '}' ';'
{
	int idx = curstruct;
	curstruct = parentstruct;
	parentstruct = -1;
	hoistanonymous(curstruct, idx);
}
          ;

anon_u_end: '}' ';'
{
	int idx = curstruct;
	curstruct = parentstruct;
	parentstruct = -1;
	hoistanonymous(curstruct, idx);
}
          ;

anonmembers:
        | anonmembers type IDENT ';'
{
	structaddmember(curstruct, $3->u.v, $2);
}
        | anonmembers type IDENT ':' NUM ';'
{
	structaddbitfield(curstruct, $3->u.v, $2, $5->u.n);
}
        ;

typed_decl: type_and_ident typed_decl_rest
{
	/* type_and_ident saves to globals, typed_decl_rest uses them */
};

type_and_ident: type IDENT
{
	parsed_type = $1;
	strcpy(parsed_ident, $2->u.v);
};

typed_decl_rest: ansi_func_proto '{' dcls stmts '}'
{
	/* ANSI function body */
	if (!stmt($4, -1)) {
		if (curfntyp == NIL)
			fprintf(of, "\tret\n");
		else
			fprintf(of, "\tret 0\n");
	}
	fprintf(of, "}\n\n");
}
               | '(' ')' ';'
{
	/* Forward declaration - no parameters */
	varadd(parsed_ident, 1, FUNC(parsed_type), 0);
}
               | ';'
{
	/* Global variable */
	if (parsed_type == NIL)
		die("invalid void declaration");
	if (nglo == NGlo)
		die("too many string literals");
	ini[nglo] = alloc(sizeof "{ x 0 }");
	sprintf(ini[nglo], "{ %c 0 }", irtyp(parsed_type));
	varadd(parsed_ident, nglo++, parsed_type, 0);
}
               ;

ansi_func_proto: '(' init_ansi par0 ')'
{
	Symb *s;
	Node *n;
	int t, m;

	curfntyp = parsed_type;
	varadd(parsed_ident, 1, FUNC(curfntyp), 0);
	if (curfntyp == NIL)
		fprintf(of, "export function $%s(", parsed_ident);
	else
		fprintf(of, "export function %c $%s(", irtyp_ret(curfntyp), parsed_ident);
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
		fprintf(of, "\t%%%s =l alloc%d %d\n", n->u.v, iralign(s->ctyp), m);
		fprintf(of, "\tstore%c %%t%d", irtyp(s->ctyp), t);
		fprintf(of, ", %%%s\n", n->u.v);
	}
};

init_ansi:
{
	varclr();
	tmp = 0;
	clit = 0;
};

init:
{
	varclr();
	tmp = 0;
	clit = 0;
};

inlineopt: INLINE
         |
         ;

storageopt: STATIC
          | EXTERN
          |
          ;

kfunc: storageopt inlineopt init prot_knr '{' dcls stmts '}'
{
	if (!stmt($7, -1))
		fprintf(of, "\tret 0\n");
	fprintf(of, "}\n\n");
};

prot_knr: IDENT '(' par0 ')'
{
	Symb *s;
	Node *n;
	int t, m;

	curfntyp = INT;
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
		fprintf(of, "\t%%%s =l alloc%d %d\n", n->u.v, iralign(s->ctyp), m);
		fprintf(of, "\tstore%c %%t%d", irtyp(s->ctyp), t);
		fprintf(of, ", %%%s\n", n->u.v);
	}
};

par0: par1
    | TVOID               { $$ = 0; }
    |                     { $$ = 0; }
    ;
par1: type IDENT ',' par1 { $$ = param($2->u.v, $1, $4); }
    | type IDENT          { $$ = param($2->u.v, $1, 0); }
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
fptpar1: type ',' fptpar1 { $$ = 0; }
       | type             { $$ = 0; }
       ;

dcls:
    | dcls type IDENT ';'
{
	int s, i;
	char *v;

	if ($2 == NIL)
		die("invalid void declaration");
	v = $3->u.v;
	s = SIZE($2);
	varadd(v, 0, $2, 0);
	fprintf(of, "\t%%%s =l alloc%d %d\n", v, iralign($2), s);

	/* Zero-initialize struct/union for bitfield support */
	if (KIND($2) == STRUCT_T || KIND($2) == UNION_T) {
		/* Store 0 to each word of the struct */
		for (i = 0; i < s; i += 4) {
			if (i == 0)
				fprintf(of, "\tstorew 0, %%%s\n", v);
			else
				fprintf(of, "\t%%_zinit%d =l add %%%s, %d\n\tstorew 0, %%_zinit%d\n", tmp, v, i, tmp);
			tmp++;
		}
	}
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
	fprintf(of, "\t%%%s =l alloc%d %d\n", v, align, s);
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
	fprintf(of, "\t%%%s =l alloc%d %d\n", v, align, s);
}
    | dcls STATIC type IDENT ';'
{
	int s;
	char *v;

	if ($3 == NIL)
		die("invalid void declaration");
	v = $4->u.v;
	s = SIZE($3);
	varadd(v, 0, $3, 0);
	fprintf(of, "\t%%%s =l alloc%d %d\n", v, iralign($3), s);
}
    | dcls EXTERN type IDENT ';'
{
	int s;
	char *v;

	if ($3 == NIL)
		die("invalid void declaration");
	v = $4->u.v;
	s = SIZE($3);
	varadd(v, 0, $3, 0);
	fprintf(of, "\t%%%s =l alloc%d %d\n", v, iralign($3), s);
}
    | dcls type IDENT '[' NUM ']' ';'
{
	/* Array declaration without initialization */
	int s, n, total;
	char *v;

	if ($2 == NIL)
		die("invalid void array");
	v = $3->u.v;
	n = $5->u.n;  /* array size */
	s = SIZE($2);  /* element size */
	total = s * n;
	varadd(v, 0, IDIR($2), 1);  /* Store as pointer to element type - IS AN ARRAY */
	fprintf(of, "\t%%%s =l alloc%d %d\n", v, iralign($2), total);
}
    | dcls type IDENT '[' NUM ']' '=' '{' initlist '}' ';'
{
	/* Array declaration with initialization */
	int s, n, total;
	char *v;

	if ($2 == NIL)
		die("invalid void array");
	v = $3->u.v;
	n = $5->u.n;  /* array size */
	s = SIZE($2);  /* element size */
	total = s * n;
	varadd(v, 0, IDIR($2), 1);  /* Store as pointer to element type - IS AN ARRAY */
	fprintf(of, "\t%%%s =l alloc%d %d\n", v, iralign($2), total);

	/* Zero-initialize the array first */
	{
		int j;
		for (j = 0; j < n; j++) {
			fprintf(of, "\t%%t%d =l add ", tmp);
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

			fprintf(of, "\t%%t%d =l add ", tmp);
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

	if ($2 == NIL)
		die("invalid void function pointer");
	v = $5->u.v;
	fptr_type = IDIR(FUNC($2));  /* Pointer to function returning type */
	varadd(v, 0, fptr_type, 0);  /* Not an array */
	fprintf(of, "\t%%%s =l alloc8 8\n", v);  /* Pointers are 8 bytes */
}
    | dcls STATIC_ASSERT '(' NUM ',' STR ')' ';'
{
	/* _Static_assert in local scope */
	if ($4->u.n == 0) {
		die("static assertion failed");
	}
}
    ;

inititem: pref                        { $$ = $1; }
        | '.' IDENT '=' pref          { $$ = mknode('D', $4, $2); }
        | '[' NUM ']' '=' pref        { $$ = mknode('d', $5, $2); }
        ;

initlist: inititem                    { $$ = mknode(0, $1, 0); }
        | inititem ',' initlist       { $$ = mknode(0, $1, $3); }
        ;

type: type TFAR '*'                  { $$ = IDIR_FAR($1); }
        | type '*' TFAR              { $$ = IDIR_FAR($1); }
        | type '*'                   { $$ = IDIR($1); }
        | TCHAR                      { $$ = CHR; }
        | TSHORT                     { $$ = INT | SHORT; }
    | TINT     { $$ = INT; }
    | TLNG     { $$ = LNG; }
    | TFLOAT   { $$ = INT | FLOAT; }
    | TDOUBLE  { $$ = LNG | FLOAT; }
    | TVOID    { $$ = NIL; }
    | TUNSIGNED TCHAR    { $$ = CHR | UNSIGNED; }
    | TUNSIGNED TSHORT   { $$ = INT | SHORT | UNSIGNED; }
    | TUNSIGNED TINT     { $$ = INT | UNSIGNED; }
    | TUNSIGNED TLNG     { $$ = LNG | UNSIGNED; }
    | TUNSIGNED TLNGLNG  { $$ = LNG | UNSIGNED; }
    | TUNSIGNED          { $$ = INT | UNSIGNED; }
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
    | STRUCT IDENT {
        int idx = structfind($2->u.v);
        if (idx < 0)
            die("undefined struct");
        $$ = (idx << 3) + STRUCT_T;
    }
    | UNION IDENT {
        int idx = structfind($2->u.v);
        if (idx < 0)
            die("undefined union");
        $$ = (idx << 3) + UNION_T;
    }
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
        fprintf(of, "\t%%%s =l alloc%d %d\n", v, iralign($1), s);
        $$ = 0;
    }
    | STATIC type IDENT ';'          {
        int s;
        char *v;
        if ($2 == NIL)
            die("invalid void declaration");
        v = $3->u.v;
        s = SIZE($2);
        varadd(v, 0, $2, 0);
        fprintf(of, "\t%%%s =l alloc%d %d\n", v, iralign($2), s);
        $$ = 0;
    }
    | EXTERN type IDENT ';'          {
        int s;
        char *v;
        if ($2 == NIL)
            die("invalid void declaration");
        v = $3->u.v;
        s = SIZE($2);
        varadd(v, 0, $2, 0);
        fprintf(of, "\t%%%s =l alloc%d %d\n", v, iralign($2), s);
        $$ = 0;
    }
    | STATIC_ASSERT '(' NUM ',' STR ')' ';' {
        /* _Static_assert in statement scope */
        if ($3->u.n == 0) {
            die("static assertion failed");
        }
        $$ = 0;
    }
    | expr ';'                       { $$ = mkstmt(Expr, $1, 0, 0); }
    | WHILE '(' expr ')' stmt        { $$ = mkstmt(While, $3, $5, 0); }
    | DO stmt WHILE '(' expr ')' ';' { $$ = mkstmt(DoWhile, $2, $5, 0); }
    | IF '(' expr ')' stmt ELSE stmt { $$ = mkstmt(If, $3, $5, $7); }
    | IF '(' expr ')' stmt           { $$ = mkstmt(If, $3, $5, 0); }
    | FOR '(' exp0 ';' exp0 ';' exp0 ')' stmt
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
        fprintf(of, "\t%%%s =l alloc%d %d\n", v, iralign($3), s);
        init_expr = mknode('=', $4, $6);
        $$ = mkfor(init_expr, $8, $10, $12);
    }
    | SWITCH '(' expr ')' stmt       { $$ = mkstmt(Switch, $3, $5, 0); }
    | CASE pref ':' stmt             { Stmt *s = mkstmt(Case, 0, $4, 0); s->val = const_eval($2); $$ = s; }
    | DEFAULT ':' stmt               { $$ = mkstmt(Default, 0, $3, 0); }
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
    | '(' expr ')'        { $$ = $2; }
    | IDENT '(' arg0 ')'  { $$ = mknode('C', $1, $3); }
    | '(' '*' post ')' '(' arg0 ')'  { $$ = mknode('I', $3, $6); }
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

int
yylex()
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
		{ 0, 0 }
	};
	int i, c, c1, n;
	char v[NString], *p;

	do {
		c = getchar();
		if (c == '#')
			while ((c = getchar()) != '\n')
				;
		if (c == '/') {
			c1 = getchar();
			if (c1 == '*') {
				/* C-style comment */
				c = getchar();
				for (;;) {
					if (c == EOF)
						die("unclosed comment");
					if (c == '\n')
						line++;
					if (c == '*' && (c = getchar()) == '/') {
						c = ' ';  /* Replace comment with space */
						break;
					}
					c = getchar();
				}
			} else if (c1 == '/') {
				/* C++-style comment */
				while ((c = getchar()) != '\n')
					;
			} else {
				/* Not a comment, put back the second character */
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
			*p++ = c;
			c = getchar();
			if (!isdigit(c)) {
				/* Not a float, just a dot operator */
				ungetc(c, stdin);
				return '.';
			}
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
				ungetc(c, stdin);
				yylval.n = mknode('N', 0, 0);
				yylval.n->u.n = n;
				return NUM;
			} else if (c >= '0' && c <= '7') {
				/* Octal */
				while (c >= '0' && c <= '7') {
					n *= 8;
					n += c - '0';
					c = getchar();
				}
				ungetc(c, stdin);
				yylval.n = mknode('N', 0, 0);
				yylval.n->u.n = n;
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

		/* Check for float/double suffix (f/F for float, l/L for long double - treat as double) */
		if (c == 'f' || c == 'F' || c == 'l' || c == 'L') {
			isfloat = 1;
			c = getchar();  /* Consume suffix but don't store it */
		}

		ungetc(c, stdin);

		if (isfloat) {
			*p = 0;
			yylval.n = mknode('F', 0, 0);
			strcpy(yylval.n->u.v, v);
			return FNUM;
		} else {
			yylval.n = mknode('N', 0, 0);
			yylval.n->u.n = n;
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
			case '0': n = '\0'; break;
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
		yylval.n->u.n = n;
		return NUM;
	}

	if (isalpha(c) || c == '_') {
		p = v;
		do {
			if (p == &v[NString-1])
				die("ident too long");
			*p++ = c;
			c = getchar();
		} while (isalpha(c) || isdigit(c) || c == '_');
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

		for (i=0; kwds[i].s; i++)
			if (strcmp(v, kwds[i].s) == 0)
				return kwds[i].t;
		yylval.n = mknode('V', 0, 0);
		strcpy(yylval.n->u.v, v);
		/* Check if it's a typedef name */
		if (typhget(v, &yylval.u))
			return TNAME;
		return IDENT;
	}

	if (c == '"') {
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
			if (c == '"' && p[i-1]!='\\')
				break;
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
main()
{
	int i;

	of = stdout;
	nglo = 1;
	if (yyparse() != 0)
		die("parse error");
	for (i=1; i<nglo; i++)
		fprintf(of, "data $glo%d = %s\n", i, ini[i]);
	return 0;
}
