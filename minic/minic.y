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

#define SHORT     (1 << 7)  /* Short flag for types */
#define UNSIGNED  (1 << 6)  /* Unsigned flag for types */
#define IDIR(x) (((x) << 3) + PTR)
#define FUNC(x) (((x) << 3) + FUN)
#define DREF(x) ((x) >> 3)
#define KIND(x) ((x) & 7)
#define ISUNSIGNED(x) ((x) & UNSIGNED)
#define BASETYPE(x) (KIND(x) & ~UNSIGNED)
#define SIZE(x)                                    \
	(KIND(x) == NIL ? (die("void has no size"), 0) : \
	 KIND(x) == CHR ? 1 :  \
	 ((x) & SHORT) ? 2 :  \
	 KIND(x) == INT ? 4 : \
	 (KIND(x) == STRUCT_T || KIND(x) == UNION_T) ? structh[DREF(x)].size : 8)

typedef struct Node Node;
typedef struct Symb Symb;
typedef struct Stmt Stmt;

struct Symb {
	enum {
		Con,
		Tmp,
		Var,
		Glo,
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
	} t;
	void *p1, *p2, *p3;
	int val; /* for case values */
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
	int offset;
};

/* Struct/union definition table */
enum { NStruct = 64 };
struct {
	char name[NString];
	int isunion;  /* 1 for union, 0 for struct */
	int nmembers;
	struct Member members[16];  /* Max 16 members per struct */
	int size;
} structh[NStruct];
int nstruct = 0;
int curstruct = -1;  /* Index of struct currently being defined */

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
			return;
		}
		if (strcmp(varh[h].v, v) == 0)
			die("double definition");
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

	m = &structh[sidx].members[structh[sidx].nmembers];
	strcpy(m->name, name);
	m->ctyp = ctyp;

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

char
irtyp(unsigned ctyp)
{
	if (SIZE(ctyp) == 1) return 'b';
	if (ctyp & SHORT) return 'h';
	if (SIZE(ctyp) == 8) return 'l';
	return 'w';
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

void
call(Node *n, Symb *sr)
{
	Node *a;
	char *f;
	unsigned ft;

	f = n->l->u.v;
	if (varget(f)) {
		ft = varget(f)->ctyp;
		if (KIND(ft) != FUN)
			die("invalid call");
	} else
		ft = FUNC(INT);
	sr->ctyp = DREF(ft);
	for (a=n->r; a; a=a->r)
		a->u.s = expr(a->l);
	fprintf(of, "\t");
	psymb(*sr);
	fprintf(of, " =%c call $%s(", irtyp(sr->ctyp), f);
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
		} else if (varh[hash(n->u.v)].isarray) {
			/* Arrays - don't load, the lvalue IS the pointer */
			sr = s0;
		} else {
			/* Regular variables and pointer variables - load value */
			sr.t = Tmp;
			sr.u.n = tmp++;
			load(sr, s0);
			/* Bytes and shorts are extended to words during load */
			if (KIND(sr.ctyp) == CHR || (sr.ctyp & SHORT)) {
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

	case 'S':
		sr.t = Glo;
		sr.u.n = n->u.n;
		sr.ctyp = IDIR(INT);
		break;

	case 'C':
		call(n, &sr);
		break;

	case '@':
		s0 = expr(n->l);
		if (KIND(s0.ctyp) != PTR)
			die("dereference of a non-pointer");
		sr.ctyp = DREF(s0.ctyp);
		load(sr, s0);
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
		}
		break;

	case '~':
		s0 = expr(n->l);
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

	case '=':
		s0 = expr(n->r);
		s1 = lval(n->l);
		sr = s0;
		/* Type conversions for assignment */
		if (KIND(s1.ctyp) == LNG && KIND(s0.ctyp) == INT)
			sext(&s0);
		if (KIND(s1.ctyp) == CHR && KIND(s0.ctyp) == INT) {
			/* Truncate int to char - no explicit conversion needed */
			/* QBE will handle truncation in storeb */
		}
		if (KIND(s1.ctyp) == INT && KIND(s0.ctyp) == CHR) {
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
		/* Allow assignment between signed/unsigned variants */
		if (s1.ctyp != s0.ctyp
		    && !(KIND(s1.ctyp) == CHR && KIND(s0.ctyp) == INT)
		    && !((KIND(s1.ctyp) == KIND(s0.ctyp)) ||
		         ((KIND(s1.ctyp) & ~UNSIGNED) == (KIND(s0.ctyp) & ~UNSIGNED))))
			die("invalid assignment");
		fprintf(of, "\tstore%c ", irtyp(s1.ctyp));
		goto Args;

	case 'p':
	case 'm':
		/* Prefix increment/decrement: ++i, --i */
		o = n->op == 'p' ? '+' : '-';
		sl = lval(n->l);
		s0.t = Tmp;
		s0.u.n = tmp++;
		s0.ctyp = sl.ctyp;
		load(s0, sl);
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
		/* Store new value */
		fprintf(of, "\tstore%c ", irtyp(sl.ctyp));
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
		s0.ctyp = sl.ctyp;
		load(s0, sl);
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
		if (strchr("ne<l", n->op)) {
			sprintf(ty, "%c", irtyp(sr.ctyp));
			sr.ctyp = INT;
		} else
			strcpy(ty, "");
		fprintf(of, "\t");
		psymb(sr);
		fprintf(of, " =%c", irtyp(sr.ctyp));
		/* Use unsigned comparison if either operand is unsigned */
		if (strchr("<l", o) && (ISUNSIGNED(s0.ctyp) || ISUNSIGNED(s1.ctyp))) {
			fprintf(of, " %s%s ", o == '<' ? "cult" : "cule", ty);
		} else {
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
		fprintf(of, "\tstore%c ", irtyp(sl.ctyp));
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
	case '@':
		sr = expr(n->l);
		if (KIND(sr.ctyp) != PTR)
			die("dereference of a non-pointer");
		sr.ctyp = DREF(sr.ctyp);
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
		x = expr(s->p1);
		fprintf(of, "\tret ");
		psymb(x);
		fprintf(of, "\n");
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
%token <n> STR
%token <n> IDENT
%token PP MM LE GE SIZEOF SHL SHR
%token ADDEQ SUBEQ MULEQ DIVEQ MODEQ
%token ANDEQ OREQ XOREQ SHLEQ SHREQ

%token TVOID TCHAR TSHORT TINT TLNG TUNSIGNED
%token IF ELSE WHILE DO FOR BREAK CONTINUE RETURN
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
%type <s> stmt stmts
%type <n> expr exp0 pref post arg0 arg1 par0 par1 initlist
%token <u> TNAME

%%

prog: func prog | fdcl prog | idcl prog | edcl prog | tdcl prog | sdcl prog | ;

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

tdcl: TYPEDEF type IDENT ';'
{
	typhadd($3->u.v, $2);
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
        ;

fdcl: type IDENT '(' ')' ';'
{
	varadd($2->u.v, 1, FUNC($1), 0);
};

idcl: type IDENT ';'
{
	if ($1 == NIL)
		die("invalid void declaration");
	if (nglo == NGlo)
		die("too many string literals");
	ini[nglo] = alloc(sizeof "{ x 0 }");
	sprintf(ini[nglo], "{ %c 0 }", irtyp($1));
	varadd($2->u.v, nglo++, $1, 0);
};

init:
{
	varclr();
	tmp = 0;
};

func: init prot '{' dcls stmts '}'
{
	if (!stmt($5, -1))
		fprintf(of, "\tret 0\n");
	fprintf(of, "}\n\n");
};

prot: IDENT '(' par0 ')'
{
	Symb *s;
	Node *n;
	int t, m;

	varadd($1->u.v, 1, FUNC(INT), 0);
	fprintf(of, "export function w $%s(", $1->u.v);
	n = $3;
	if (n)
		for (;;) {
			s = varget(n->u.v);
			fprintf(of, "%c ", irtyp(s->ctyp));
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
		fprintf(of, "\t%%%s =l alloc%d %d\n", n->u.v, m, m);
		fprintf(of, "\tstore%c %%t%d", irtyp(s->ctyp), t);
		fprintf(of, ", %%%s\n", n->u.v);
	}
};

par0: par1
    |                     { $$ = 0; }
    ;
par1: type IDENT ',' par1 { $$ = param($2->u.v, $1, $4); }
    | type IDENT          { $$ = param($2->u.v, $1, 0); }
    ;


dcls:
    | dcls type IDENT ';'
{
	int s;
	char *v;

	if ($2 == NIL)
		die("invalid void declaration");
	v = $3->u.v;
	s = SIZE($2);
	varadd(v, 0, $2, 0);
	fprintf(of, "\t%%%s =l alloc%d %d\n", v, s, s);
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
	fprintf(of, "\t%%%s =l alloc%d %d\n", v, s, total);
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
	fprintf(of, "\t%%%s =l alloc%d %d\n", v, s, total);

	/* Generate initialization code */
	{
		Node *init = $9;
		int i = 0;
		while (init && i < n) {
			Symb val = expr(init->l);
			fprintf(of, "\t%%t%d =l add ", tmp);
			fprintf(of, "%%%s, %d\n", v, i * s);
			fprintf(of, "\tstore%c ", irtyp($2));
			psymb(val);
			fprintf(of, ", %%t%d\n", tmp);
			tmp++;
			init = init->r;
			i++;
		}
	}
}
    ;

initlist: pref                    { $$ = mknode(0, $1, 0); }
        | pref ',' initlist       { $$ = mknode(0, $1, $3); }
        ;

type: type '*' { $$ = IDIR($1); }
    | TCHAR    { $$ = CHR; }
    | TSHORT   { $$ = INT | SHORT; }
    | TINT     { $$ = INT; }
    | TLNG     { $$ = LNG; }
    | TVOID    { $$ = NIL; }
    | TUNSIGNED TCHAR  { $$ = CHR | UNSIGNED; }
    | TUNSIGNED TSHORT { $$ = INT | SHORT | UNSIGNED; }
    | TUNSIGNED TINT   { $$ = INT | UNSIGNED; }
    | TUNSIGNED TLNG   { $$ = LNG | UNSIGNED; }
    | TUNSIGNED        { $$ = INT | UNSIGNED; }
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
    | expr ';'                       { $$ = mkstmt(Expr, $1, 0, 0); }
    | WHILE '(' expr ')' stmt        { $$ = mkstmt(While, $3, $5, 0); }
    | DO stmt WHILE '(' expr ')' ';' { $$ = mkstmt(DoWhile, $2, $5, 0); }
    | IF '(' expr ')' stmt ELSE stmt { $$ = mkstmt(If, $3, $5, $7); }
    | IF '(' expr ')' stmt           { $$ = mkstmt(If, $3, $5, 0); }
    | FOR '(' exp0 ';' exp0 ';' exp0 ')' stmt
                                     { $$ = mkfor($3, $5, $7, $9); }
    | SWITCH '(' expr ')' stmt       { $$ = mkstmt(Switch, $3, $5, 0); }
    | CASE NUM ':' stmt              { Stmt *s = mkstmt(Case, 0, $4, 0); s->val = $2->u.n; $$ = s; }
    | DEFAULT ':' stmt               { $$ = mkstmt(Default, 0, $3, 0); }
    ;

stmts: stmts stmt { $$ = mkstmt(Seq, $1, $2, 0); }
     |            { $$ = 0; }
     ;

expr: pref
    | expr ',' expr     { $$ = mknode(',', $1, $3); }
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
    ;

post: NUM
    | STR
    | IDENT
    | SIZEOF '(' type ')' { $$ = mknode('N', 0, 0); $$->u.n = SIZE($3); }
    | '(' expr ')'        { $$ = $2; }
    | IDENT '(' arg0 ')'  { $$ = mknode('C', $1, $3); }
    | post '[' expr ']'   { $$ = mkidx($1, $3); }
    | post PP             { $$ = mknode('P', $1, 0); }
    | post MM             { $$ = mknode('M', $1, 0); }
    | post '.' IDENT      { $$ = mknode('.', $1, $3); }
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
		{ "typedef", TYPEDEF },
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
		if (c == '\n')
			line++;
	} while (isspace(c));


	if (c == EOF)
		return 0;


	if (isdigit(c)) {
		n = 0;
		/* Check for hex (0x) or octal (0) */
		if (c == '0') {
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
			} else if (c >= '0' && c <= '7') {
				/* Octal */
				while (c >= '0' && c <= '7') {
					n *= 8;
					n += c - '0';
					c = getchar();
				}
			} else {
				/* Just 0 */
				ungetc(c, stdin);
				yylval.n = mknode('N', 0, 0);
				yylval.n->u.n = 0;
				return NUM;
			}
			ungetc(c, stdin);
			yylval.n = mknode('N', 0, 0);
			yylval.n->u.n = n;
			return NUM;
		}
		/* Decimal */
		do {
			n *= 10;
			n += c-'0';
			c = getchar();
		} while (isdigit(c));
		ungetc(c, stdin);
		yylval.n = mknode('N', 0, 0);
		yylval.n->u.n = n;
		return NUM;
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
			case '\\': n = '\\'; break;
			case '\'': n = '\''; break;
			default: n = c; break;
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

	if (isalpha(c)) {
		p = v;
		do {
			if (p == &v[NString-1])
				die("ident too long");
			*p++ = c;
			c = getchar();
		} while (isalpha(c) || c == '_');
		*p = 0;
		ungetc(c, stdin);
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
