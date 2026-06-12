/*
 * static_data_def.c -- second TU for static_data_probe.c (§6b).
 * Defines the SAME static names with different values; see the probe
 * header for what each check proves.
 */

static int dir_table = 200;

extern int shared_global;

static int bump(void)
{
	static int counter = 30;
	counter++;
	return counter;
}

int def_table_value(void)
{
	return dir_table;
}

int def_read_shared(void)
{
	return shared_global + 1;
}

int def_bump_twice(void)
{
	bump();
	return bump();
}
