#include <stdio.h>
#include <string.h>

typedef void (*print_strn_t)(void *data, const char *str, size_t len);

struct Print {
	void *data;
	print_strn_t print_strn;
};

struct VStr {
	size_t alloc;
	size_t len;
	char *buf;
	int fixed_buf;
};

struct Sink {
	char buf[64];
	size_t len;
};

static char storage[32];

static void add_strn(void *data, const char *str, size_t len)
{
	struct VStr *v = data;
	if (v->len + len >= v->alloc) {
		len = v->alloc - v->len - 1;
	}
	memmove(v->buf + v->len, str, len);
	v->len += len;
	v->buf[v->len] = '\0';
}

static void sink_write(struct Sink *s, const char *str, size_t len)
{
	size_t i;
	for (i = 0; i < len && s->len + 1 < sizeof(s->buf); i++) {
		s->buf[s->len++] = str[i];
	}
	s->buf[s->len] = '\0';
}

static void call_print(const struct Print *print, const char *str, size_t len)
{
	print->print_strn(print->data, str, len);
}

int main(void)
{
	struct Sink s;
	struct VStr v;
	struct Print print;

	s.len = 0;
	s.buf[0] = '\0';
	sink_write(&s, "123", 3);
	printf("sizeof_member=%s\n", s.buf);

	v.alloc = sizeof(storage);
	v.len = 0;
	v.buf = storage;
	v.fixed_buf = 1;
	v.buf[0] = '\0';
	add_strn(&v, "abc", 3);
	printf("direct=%s\n", v.buf);
	v.len = 0;
	v.buf[0] = '\0';
	print.data = &v;
	print.print_strn = add_strn;
	call_print(&print, "123", 3);
	printf("callback=%s\n", v.buf);
	return 0;
}
