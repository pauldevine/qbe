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
	INT,
	LNG,
	PTR,
	FUN,
};

#define IDIR(x) (((x) << 3) + PTR)
#define FUNC(x) (((x) << 3) + FUN)
#define DREF(x) ((x) >> 3)
#define KIND(x) ((x) & 7)
#define SIZE(x)                                    \
	(x == NIL ? (die("void has no size"), 0) : \
	 x == INT ? 4 : 8)

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
		Seq,
		Expr,
		Break,
		Continue,
		Ret,
	} t;
	void *p1, *p2, *p3;
};

int yylex(void), yyerror(char *);
Symb expr(Node *), lval(Node *);
void branch(Node *, int, int);

FILE *of;
int line;
int lbl, tmp, nglo;
char *ini[NGlo];
struct {
	char v[NString];
	unsigned ctyp;
	int glo;
} varh[NVar];

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
		if (!varh[h].glo)
			varh[h].v[0] = 0;
}

void
varadd(char *v, int glo, unsigned ctyp)
{
	unsigned h0, h;

	h0 = hash(v);
	h = h0;
	do {
		if (varh[h].v[0] == 0) {
			strcpy(varh[h].v, v);
			varh[h].glo = glo;
			varh[h].ctyp = ctyp;
			return;
		}
		if (strcmp(varh[h].v, v) == 0)
			die("double definition");
		h = (h+1) % NVar;
	} while(h != h0);
	die("too many variables");
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
			if (!varh[h].glo) {
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
	return SIZE(ctyp) == 8 ? 'l' : 'w';
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

	if (l->ctyp == r->ctyp && KIND(l->ctyp) != PTR)
		return l->ctyp;

	if (l->ctyp == LNG && r->ctyp == INT) {
		sext(r);
		return LNG;
	}
	if (l->ctyp == INT && r->ctyp == LNG) {
		sext(l);
		return LNG;
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
	fprintf(of, " =%c load%c ", t, t);
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
		load(sr, s0);
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
		if (s1.ctyp == LNG &&  s0.ctyp == INT)
			sext(&s0);
		if (s0.ctyp != IDIR(NIL) || KIND(s1.ctyp) != PTR)
		if (s1.ctyp != IDIR(NIL) || KIND(s0.ctyp) != PTR)
		if (s1.ctyp != s0.ctyp)
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
		fprintf(of, " %s%s ", otoa[o], ty);
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
	varadd(v, 0, ctyp);
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

%token TVOID TINT TLNG
%token IF ELSE WHILE DO FOR BREAK CONTINUE RETURN

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
%type <n> expr exp0 pref post arg0 arg1 par0 par1

%%

prog: func prog | fdcl prog | idcl prog | ;

fdcl: type IDENT '(' ')' ';'
{
	varadd($2->u.v, 1, FUNC($1));
};

idcl: type IDENT ';'
{
	if ($1 == NIL)
		die("invalid void declaration");
	if (nglo == NGlo)
		die("too many string literals");
	ini[nglo] = alloc(sizeof "{ x 0 }");
	sprintf(ini[nglo], "{ %c 0 }", irtyp($1));
	varadd($2->u.v, nglo++, $1);
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

	varadd($1->u.v, 1, FUNC(INT));
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


dcls: | dcls type IDENT ';'
{
	int s;
	char *v;

	if ($2 == NIL)
		die("invalid void declaration");
	v = $3->u.v;
	s = SIZE($2);
	varadd(v, 0, $2);
	fprintf(of, "\t%%%s =l alloc%d %d\n", v, s, s);
};

type: type '*' { $$ = IDIR($1); }
    | TINT     { $$ = INT; }
    | TLNG     { $$ = LNG; }
    | TVOID    { $$ = NIL; }
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
		{ "int", TINT },
		{ "long", TLNG },
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
