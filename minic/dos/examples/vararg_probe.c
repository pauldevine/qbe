/*
 * vararg_probe.c — minic-compiled <stdarg.h> varargs (va_start / va_arg).
 *
 * Until §2u, minic's <stdarg.h> was a NO-OP stub (`va_arg(ap,T) = *(T*)0`), so
 * EVERY minic-compiled variadic consumer read garbage from address 0.  The
 * milestone hid it: integer printing uses non-vararg paths, and MicroPython's
 * `mp_printf("%q", qst)` (the exception-traceback `File "%q"`) was the first
 * real va_arg consumer — it printed garbage (`File "JOo`) although the qstr
 * value arrived intact.  See [[project-minic-vararg-stub]].
 *
 * §2u implements real varargs: va_list is a pointer walking the caller-pushed
 * stack args, va_start emits the i8086 `vargp` op (SS:bp+vararg_off), va_arg is
 * pointer arithmetic + a (far) load.  This probe exercises it directly — the
 * variadic functions below are minic-compiled; printf (libstub) is only used to
 * report pass/fail.
 *
 * Gated medium (near data: va_list = 2-byte near ptr, DS==SS) + compact + large
 * (far data: va_list = 4-byte far ptr, segment = SS) — the bug bit every model.
 */

#include <stdio.h>
#include <stdarg.h>

/* All-int varargs (each a 2-byte stack word). */
static int va_sum(int count, ...)
{
	va_list ap;
	int i, total = 0;
	va_start(ap, count);
	for (i = 0; i < count; i++)
		total += va_arg(ap, int);
	va_end(ap);
	return total;
}

/* The mp_printf/%q shape: a pointer fixed param (like `fmt'), then a 2-byte
 * value vararg (like a qstr) and an int after it — the exact case that printed
 * garbage on the unfixed compiler. */
static unsigned q_after_ptr(const char *tag, ...)
{
	va_list ap;
	unsigned q;
	int k;
	va_start(ap, tag);
	q = va_arg(ap, unsigned);   /* 2-byte */
	k = va_arg(ap, int);        /* 2-byte */
	va_end(ap);
	return q + (unsigned)k + (unsigned)(tag[0]);
}

/* Mixed widths: int (2), long (4), pointer (4 far / 2 near), int (2).
 * Forces the slot-size advance to be right for each. */
static long mixed(int first, ...)
{
	va_list ap;
	long l;
	int a, b;
	const char *p;
	va_start(ap, first);
	a = va_arg(ap, int);
	l = va_arg(ap, long);
	p = va_arg(ap, const char *);
	b = va_arg(ap, int);
	va_end(ap);
	return (long)first + a + l + (p ? 1000L : 0L) + b;
}

int main(void)
{
	int s;
	unsigned q;
	long m;
	static const char marker = 'A';   /* 0x41 = 65 */

	s = va_sum(4, 10, 20, 30, 40);
	if (s == 100) printf("sum ok\r\n"); else printf("sum FAIL %d\r\n", s);

	/* tag="A" -> tag[0]=65; q=7000; k=11 -> 7000+11+65 = 7076 */
	q = q_after_ptr("A", 7000u, 11);
	if (q == 7076u) printf("q ok\r\n"); else printf("q FAIL %u\r\n", q);

	/* first=1, a=2, l=300000 (>16-bit), p!=NULL (+1000), b=5
	 * -> 1+2+300000+1000+5 = 301008 */
	m = mixed(1, 2, 300000L, "x", 5);
	if (m == 301008L) printf("mixed ok\r\n"); else printf("mixed FAIL %ld\r\n", m);

	return 0;
}
