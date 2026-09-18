# Sourced: sets CPPFLAGS (array) for preprocessing C-Kermit against OUR
# headers (minic/dos/ckermit/include).  Needs Q, CK, MODEL.  Shared by
# tools/build-ckermit.sh and tools/ckermit/hdrcheck.sh so they cannot drift.
#
# Search order mirrors victorow.mak (-i=victorow -i=victor -i=$WATCOM/h) with
# our header set in Watcom's place.  -undef drops the host compiler's
# predefined macros (__APPLE__, __GNUC__, ...), which would steer C-Kermit's
# platform #ifdefs; the ones wcc predefines for -ml -0 -bt=dos are defined
# instead, except __WATCOMC__ (we are not Watcom) and _INTEGRAL_MAX_BITS
# (only read inside #ifdef COMMENT).  -P (no line markers) is left to callers.
case "$MODEL" in
compact) CK_MODELDEFS=(-D__COMPACT__ -DM_I86CM -D_M_I86CM) ;;
large)   CK_MODELDEFS=(-D__LARGE__ -DM_I86LM -D_M_I86LM) ;;
huge)    CK_MODELDEFS=(-D__HUGE__ -DM_I86HM -D_M_I86HM) ;;
*)       CK_MODELDEFS=() ;;
esac
CPPFLAGS=(-E -undef -nostdinc -isysroot/var/empty -Wno-builtin-macro-redefined
	-D__STDC__=1 -D__STDC_VERSION__=199409L
	-DM_I86 -D_M_I86 -D_M_IX86=0 -D__I86__ -D__DOS__ -D_DOS -DMSDOS "${CK_MODELDEFS[@]}"
	-D__MINIC__
	"-I$CK/victorow" "-I$CK/victor" "-I$Q/minic/dos/ckermit/include" "-I$CK")
