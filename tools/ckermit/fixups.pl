# Workarounds applied to the Watcom-preprocessed text.  Each was a minic gap
# (G#, all fixed in minic by item 2) or is a Watcom-ism (W#, see FINDINGS).
s/\b__interrupt\b//g;   # W3 Watcom __interrupt keyword (triage: stripped; minic spells it __attribute__((interrupt)))
s/__based\(__segname\("XI"\)\)//;   # W5 Watcom XI init-table record (runtime-specific; needs a crt0 hook in a real port)
s/(\((?:[^()]++|(?1))*\))\s*:>\s*(\((?:[^()]++|(?2))*\))/((void __far *)(((unsigned long)$1 << 16) | (unsigned short)$2))/g;   # W4 Watcom base operator `seg :> off` (MK_FP)
s/\b__int64\b/long long/g;   # W1 Watcom builtin type
s/^.*__based\(__segname.*$//;   # W2 Watcom __based() alloca helper

