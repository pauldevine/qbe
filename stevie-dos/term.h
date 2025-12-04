/*
 * STEVIE - ST Editor for VI Enthusiasts   ...Tim Thompson...twitch!tjt...
 *
 * Extensive modifications by:  Tony Andrews       onecom!wldrdg!tony
 *
 * DOS port for MiniC/QBE 8086 compiler
 */

/*
 * This file contains the machine dependent escape sequences that
 * the editor needs to perform various operations. Some of the sequences
 * here are optional. Anything not available should be indicated by
 * a null string. In the case of insert/delete line sequences, the
 * editor checks the capability and works around the deficiency, if
 * necessary.
 */

/*
 * The macro names here correspond (more or less) to the actual ANSI names
 */

#ifdef ATARI
#define	T_EL	"\033l"		/* erase the entire current line */
#define	T_IL	"\033L"		/* insert one line */
#define	T_DL	"\033M"		/* delete one line */
#define	T_SC	"\033j"		/* save the cursor position */
#define	T_ED	"\033E"		/* erase display (may optionally home cursor) */
#define	T_RC	"\033k"		/* restore the cursor position */
#define	T_CI	"\033f"		/* invisible cursor (very optional) */
#define	T_CV	"\033e"		/* visible cursor (very optional) */
#endif

#ifdef DOS
/* ANSI.SYS escape sequences for DOS */
#define	T_EL	"\033[2K"	/* erase the entire current line */
#define	T_IL	"\033[L"	/* insert one line */
#define	T_DL	"\033[M"	/* delete one line */
#define	T_SC	"\033[s"	/* save the cursor position */
#define	T_ED	"\033[2J"	/* erase display (clears screen) */
#define	T_RC	"\033[u"	/* restore the cursor position */
#define	T_CI	""		/* invisible cursor (not widely supported) */
#define	T_CV	""		/* visible cursor (not widely supported) */
#endif
