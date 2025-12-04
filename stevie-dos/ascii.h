/*
 * STEVIE - ST Editor for VI Enthusiasts   ...Tim Thompson...twitch!tjt...
 *
 * Extensive modifications by:  Tony Andrews       onecom!wldrdg!tony
 *
 * DOS port for MiniC/QBE - use decimal values instead of octal escapes
 */

/*
 * Definitions of various common control characters
 * Using decimal values for MiniC compatibility
 */

#define	NUL	0       /* '\0' */
#define	BS	8       /* '\010' backspace */
#define	TAB	9       /* '\011' tab */
#define	NL	10      /* '\012' newline */
#define	CR	13      /* '\015' carriage return */
#define	ESC	27      /* '\033' escape */

#define	CTRL(x)	((x) & 0x1f)
