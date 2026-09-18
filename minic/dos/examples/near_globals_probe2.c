/* Second TU of near_globals_probe.c: defines the objects main() reaches as
 * externs, and reads back what main() wrote to them. */
struct small { int a; long b; char c; };
struct bigs { int v[70]; int tail; };

int ext_counter;
struct small ext_sm;
struct bigs ext_big;
char ext_buf[16];

int
bump_ext(void)
{
	ext_counter++;
	return ext_counter;
}

int
ext_sum(void)
{
	return (int)(ext_sm.b % 1000) + ext_big.tail + ext_buf[1];
}
