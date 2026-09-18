# Workarounds applied to the Watcom-preprocessed text.  Each is a minic gap
# (G#) or a Watcom-ism (W#), recorded in FINDINGS.
BEGIN { sub ::tsplit { my ($t)=@_; my @p; my $d=0; my $c=''; for my $ch (split //, $t) { $d++ if $ch =~ /[(\[{]/; $d-- if $ch =~ /[)\]}]/; if ($ch eq ',' && $d==0) { push @p, $c; $c=''; next } $c .= $ch } push @p, $c; s/^\s+|\s+$//g for @p; return @p } }
s/\b__interrupt\b//g;   # W3 Watcom __interrupt keyword (triage: stripped; minic spells it __attribute__((interrupt)))
s/\(\s*__far\s*\*/(*/g;   # G21 `void (__far *h)()` qualifier inside the fn-ptr declarator parens
s/^(\s*)(static\s+)?((?:unsigned\s+)?(?:char|int|CHAR|short|long)\s*\**)\s*(\w+)\s*\[([^]]*)\]\s*\[([^]]*)\]/typedef $3 _row_$4\[$6\]; $1$2_row_$4 $4\[$5\]/;   # G12 2-D array `T a[N][M]` (via an array typedef, the aoa path)
s/\bckjptr\((\w+)\)/ckjptr $1/g;   # G20 parenthesized parameter declarator `T (name)`
s/__based\(__segname\("XI"\)\)//;   # W5 Watcom XI init-table record (runtime-specific; needs a crt0 hook in a real port)
s/(\((?:[^()]++|(?1))*\))\s*:>\s*(\((?:[^()]++|(?2))*\))/((void __far *)(((unsigned long)$1 << 16) | (unsigned short)$2))/g;   # W4 Watcom base operator `seg :> off` (MK_FP)
s/^(\s*)\*t = (\(\(fd\) >= 3 && \(fd\) == ttyfd\)) \? (\w+) : (\w+);/$1if ($2) *t = $3; else *t = $4;/;   # G24 struct-typed ?: as an assignment source (ckvictor tcgetattr)
s/\b(void|char|int|unsigned)\s+const\b/const $1/g;   # G23 east-const `void const *`
s/^(\s*extern\s+)void\s*\(\s*\*\s*(\w+)\s*\(([^()]*)\)\s*\)\s*\(\s*\)\s*;/$1void *$2($3);/;   # G2b `void (*f(args))()` fn returning fn-ptr (triage: void *)
s/^typedef void\s+__sigfpe_func\( int, int \);//;   # G1 non-pointer function typedef
s/extern void \( \*signal\( int __sig, void \( \*__func\)\(int\) \) \)\(int\);/extern __sig_func signal( int __sig, __sig_func __func );/;  # G2 fn returning fn-ptr declarator
s/\b__int64\b/long long/g;   # W1 Watcom builtin type
s/^(\s*)extern(\s+struct\s+\w+\s*\*+\s*\w+\s*\()/$1$2/;   # G3 `extern struct T *fn(...)` prototype
if (/^(\s*)extern\s+((?:const\s+|unsigned\s+)*\w+\s*\*+\s*\w+\s*\[[^\]]*\]\s*;)/) { $_ = "$1$2\n"; s/\[\s*\]/[1]/; }   # G4 `extern T *name[N];` (ext_decl '*' IDENT steals the star)
s/\(\s*\*\s*\)\s*\(/"(*_anon".($::an++).")("/ge if $::depth == 0 && !/^\s*\(/;   # G5 abstract fn-ptr parameter `int (*)(char)` in a file-scope prototype
if (/^([^(]*)(\(.*)$/s) { my ($h,$t)=($1,$2); $t =~ s/\*\s*\[\s*\]\s*(?=[,)])/**/g; $t =~ s/(struct\s+\w+)\s*\[\s*\](?=\s*[,)])/$1 */g; $t =~ s/([(,]\s*(?:const\s+|unsigned\s+)*\w+)\s*\[\s*\](?=\s*[,)])/$1 */g; $_ = $h.$t; }   # G6 abstract array params `char *[]`, `struct T[]`, `char []` (only right of the first paren)
s/\blong double\b/double/g;   # G7 `long double` type (== double on this target)
s/^.*__based\(__segname.*$//;   # W2 Watcom __based() alloca helper
if ($::depth > 0 && /^\s*extern\s+[\w\s]*\*\s*\w+\s*\[/) { s/\*\s*(\w+)\s*\[[^\]]*\]/**$1/g; }   # G4b block-scope `extern T *a[], *b[];` (triage-only: pointer, WRONG semantics)
if (/^(\s*extern\s+(?:const\s+|unsigned\s+|struct\s+)*\w+)\s+([^(;]*,[^(;]*);\s*$/) { my ($b,$l)=($1,$2); if ($l =~ /\*\s*\*|\*\s*\w+\s*\[/) { $_ = join(' ', map { "$b $_;" } split /\s*,\s*/, $l) . "\n"; } }   # G4c multi-name extern with `**x` / `*x[]` declarators
$_ = "\n" if $::depth > 0 && /^\s*(?:unsigned\s+)?(?:char|int|long|void|short|CHAR|VOID|FILE)\s*\*+\s*\w+\s*\([^()]*\)\s*;\s*$/;   # G11 block/stmt-scope fn prototype with pointer return (dropped; triage-only)
$_ = "\n" if $::depth > 0 && /^\s*(?:extern\s+)?(?:unsigned\s+|const\s+|static\s+)*(?:char|int|long|void|short|CHAR|VOID|FILE|ULONG|time_t|off_t)\s*\**\s*\w+\s*\([^()]*\)\s*;\s*$/;   # G11b stmt-scope fn prototype (any return, extern or not; dropped; triage-only)
if ($::depth > 0 && /^(\s*)(extern\s+(?:const\s+|unsigned\s+|struct\s+)*\w+)\s+([^(;]*,[^(;]*);\s*$/) { my ($i,$b,$l)=($1,$2,$3); $_ = join(' ', map { "$i$b $_;" } split /\s*,\s*/, $l) . "\n"; }   # G13 stmt-scope multi-name extern `extern int a, b;`
s/(extern\s+(?:const\s+|unsigned\s+)*\w+\s*\*+\s*\w+\s*)\[\s*\](\s*;)/$1\[1\]$2/g if /^extern.*;\s*extern/;   # G4d several `extern T *x[];` on one line (see G4)
s/\bextern\s+((?:const\s+|unsigned\s+)*\w+\s*\*+\s*\w+\s*\[[^\]]*\]\s*;)/$1/g if $::depth == 0;   # G4e  several `extern T *x[..];` per line: drop extern

if (/^\s*(?:unsigned\s+)?:\s*\d+\s*[,;]/) { s/((?:^|,)\s*(?:unsigned\s+)?):(\s*\d+)/$1."_ubf".($::bf++).":$2"/ge; }   # G19 unnamed bitfield `unsigned :16, :16;` (Watcom dos.h INTPACKB)
if ($::depth > 0 && /^\s*extern\s+[\w\s]*?\w+\s*\[\s*\]\s*;\s*$/) { s/(\w+)\s*\[\s*\]/*$1/; }   # G16 stmt-scope `extern char x[];` (triage-only: pointer, WRONG semantics)
if ($::depth > 0 && /^(\s*)((?:const\s+|unsigned\s+|static\s+|register\s+)*(?:char|int|long|short|CHAR|unsigned|FILE|struct\s+\w+))\s+(\**\s*\w+.*?);\s*$/) { my ($i,$b,$l)=($1,$2,$3); my @p = ::tsplit($l); if (@p > 1 && $l =~ /\*|\[/ && $l !~ /"[^"]*,[^"]*"|','/) { $_ = join(' ', map { "$i$b $_;" } @p) . "\n"; } }   # G15 block-scope `char *p = 0, *q = f;` / `const char *p, *a[4];`
{ my $t = $_; $t =~ s/"(?:\\.|[^"\\])*"//g; $t =~ s/'(?:\\.|[^'\\])*'//g; $::depth += ($t =~ tr/{//) - ($t =~ tr/}//); }   # brace depth for the block-scope rules above
