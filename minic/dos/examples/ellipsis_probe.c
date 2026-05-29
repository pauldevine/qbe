/*
 * ellipsis_probe.c -- Phase 1 Step 1 variadic-prototype gate.
 *
 * Pins the `...` ellipsis in function prototypes / parameter lists
 * (see NEXT_SESSION.md / MICROPYTHON_PORT.md).  This was the universal
 * MicroPython blocker after Tier 1 cleared py/obj.h: signatures like
 *   void vstr_printf(vstr_t *, const char *fmt, ...);
 *   int  DEBUG_printf(const char *fmt, ...);
 * parse-errored because minic accepted varargs at the CALL site but
 * not in a prototype's parameter list.
 *
 * minic has no stdarg/va_list mechanism yet (Phase 2 libc), so a
 * variadic callee can only read its FIXED parameters; the extra args
 * are pushed by the caller and cleaned up by it (cdecl).  This probe
 * therefore exercises the full grammar+codegen path that Step 1 ships:
 *
 *   1. Variadic prototype with a NAMED leading param   (int, ...).
 *   2. Variadic prototype with an ABSTRACT leading param (const char *, ...).
 *   3. Bare-ellipsis prototype                          (...).
 *   4. Variadic DEFINITION that reads only its fixed param, called
 *      with extra trailing args -> verifies cdecl caller-cleanup.
 *   5. A variadic fn-ptr typedef parses and is usable as a type.
 *
 * Build:  tools/build-example.sh --model=medium minic/dos/examples/ellipsis_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/ellipsis_probe/ellipsis_probe.exe \
 *             | diff - minic/dos/tests/ellipsis_probe.golden.txt
 *
 * Wired into tools/test-dos.sh under the medium runtime section.
 */

#include <stdio.h>

/* Feature 1: named leading param + ... */
int firstplus(int base, ...);
/* Feature 2: abstract (unnamed) leading param + ... */
int countchar(const char *, ...);
/* Feature 3: bare ellipsis */
int answer(...);
/* Feature 5: variadic fn-ptr typedef (parse-only here) */
typedef int (*printer_t)(const char *fmt, ...);

/* Definitions read only their fixed params; the extra call args are
 * pushed by the caller and discarded (cdecl). */
int firstplus(int base, ...) { return base + 100; }
int countchar(const char *s, ...) { return s ? s[0] : -1; }
int answer(...) { return 42; }

int main()
{
	printf("fp=%d (want 105)\r\n", firstplus(5, 7, 8, 9));
	printf("cc=%d (want 90)\r\n", countchar("Z", 1, 2));   /* 'Z' == 90 */
	printf("an=%d (want 42)\r\n", answer(0, 0, 0));
	/* printf itself is the canonical variadic call -- mixed widths. */
	printf("mix=%d,%d,%d (want 1,2,3)\r\n", 1, 2, 3);
	return 0;
}
