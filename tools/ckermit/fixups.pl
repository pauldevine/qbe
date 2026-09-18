# Workarounds for Open Watcom language extensions in the C-Kermit SOURCES
# (W#), applied by build-ckermit.sh (our headers) and sweep.sh (Watcom's).
# Every minic-gap rule (G#) was removed by item 2.  Rules that only matched
# text coming from Watcom's own headers (W1 __int64, W2 __based alloca, W4
# `seg :> off`) moved into sweep.sh with item 3.
s/\b__interrupt\b//g;   # W3 Watcom __interrupt keyword (ckvictor.c's C ISR; minic spells it __attribute__((interrupt)) -- item 5a)
s/__based\(__segname\("XI"\)\)//;   # W5 Watcom XI init-table record (runtime-specific; needs a crt0 hook -- item 5c)
s/\b__near\b//g;   # W6 object-level __near (ckvictor.c v9k_rxbuf & co. must stay in DGROUP for the ISR) -- item 5b
