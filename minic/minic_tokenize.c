/* minic_tokenize.c - Tokenizer for MiniC preprocessor
 * Adapted from chibicc (MIT License) by Rui Ueyama
 * https://github.com/rui314/chibicc
 *
 * Simplifications from chibicc:
 * - No UTF-16/UTF-32 string literals (DOS is ASCII)
 * - Simpler Unicode handling
 * - Removed some GNU extensions
 */

#include "minic_token.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>

//
// Global state
//

static File *current_file;
File **input_files;
int num_input_files;

static bool at_bol;      // At beginning of line
static bool has_space;   // Has preceding space

//
// Utility functions
//

static bool startswith(char *p, char *q) {
    return strncmp(p, q, strlen(q)) == 0;
}

static bool is_ident1(char c) {
    return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '_';
}

static bool is_ident2(char c) {
    return is_ident1(c) || ('0' <= c && c <= '9');
}

static int from_hex(char c) {
    if ('0' <= c && c <= '9')
        return c - '0';
    if ('a' <= c && c <= 'f')
        return c - 'a' + 10;
    if ('A' <= c && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

//
// Token creation
//

static Token *new_token(TokenKind kind, char *start, char *end) {
    Token *tok = calloc_checked(1, sizeof(Token));
    tok->kind = kind;
    tok->loc = start;
    tok->len = end - start;
    tok->file = current_file;
    tok->filename = current_file->name;
    tok->at_bol = at_bol;
    tok->has_space = has_space;

    at_bol = false;
    has_space = false;
    return tok;
}

Token *copy_token(Token *tok) {
    Token *t = calloc_checked(1, sizeof(Token));
    *t = *tok;
    t->next = NULL;
    return t;
}

//
// Token comparison
//

bool equal(Token *tok, char *op) {
    return memcmp(tok->loc, op, tok->len) == 0 && op[tok->len] == '\0';
}

Token *skip(Token *tok, char *op) {
    if (!equal(tok, op))
        error_tok(tok, "expected '%s'", op);
    return tok->next;
}

bool consume(Token **rest, Token *tok, char *str) {
    if (equal(tok, str)) {
        *rest = tok->next;
        return true;
    }
    *rest = tok;
    return false;
}

//
// Identifier parsing
//

static int read_ident(char *start) {
    char *p = start;
    if (!is_ident1(*p))
        return 0;

    do {
        p++;
    } while (is_ident2(*p));

    return p - start;
}

//
// Punctuator (operator) parsing
//

static int read_punct(char *p) {
    // Three-character operators
    static char *kw3[] = {
        "<<=", ">>=", "...", NULL
    };
    for (int i = 0; kw3[i]; i++)
        if (startswith(p, kw3[i]))
            return 3;

    // Two-character operators
    static char *kw2[] = {
        "==", "!=", "<=", ">=", "->", "+=", "-=", "*=", "/=",
        "++", "--", "%=", "&=", "|=", "^=", "&&", "||", "<<", ">>",
        "##", NULL
    };
    for (int i = 0; kw2[i]; i++)
        if (startswith(p, kw2[i]))
            return 2;

    // Single-character operators
    return ispunct(*p) ? 1 : 0;
}

//
// Escape sequence parsing
//

static int read_escaped_char(char **new_pos, char *p) {
    if ('0' <= *p && *p <= '7') {
        // Octal escape sequence
        int c = *p++ - '0';
        if ('0' <= *p && *p <= '7') {
            c = (c << 3) + (*p++ - '0');
            if ('0' <= *p && *p <= '7')
                c = (c << 3) + (*p++ - '0');
        }
        *new_pos = p;
        return c;
    }

    if (*p == 'x') {
        // Hexadecimal escape sequence
        p++;
        if (!isxdigit(*p))
            error_at(p, "invalid hex escape sequence");

        int c = 0;
        for (; isxdigit(*p); p++)
            c = (c << 4) + from_hex(*p);
        *new_pos = p;
        return c;
    }

    *new_pos = p + 1;

    // Standard escape sequences
    switch (*p) {
    case 'a': return '\a';
    case 'b': return '\b';
    case 't': return '\t';
    case 'n': return '\n';
    case 'v': return '\v';
    case 'f': return '\f';
    case 'r': return '\r';
    case 'e': return 27;  // GNU extension
    default: return *p;
    }
}

//
// String literal parsing
//

static char *string_literal_end(char *p) {
    char *start = p;
    for (; *p != '"'; p++) {
        if (*p == '\0')
            error_at(start, "unclosed string literal");
        if (*p == '\\')
            p++;
    }
    return p;
}

static Token *read_string_literal(char *start) {
    char *end = string_literal_end(start + 1);
    char *buf = malloc_checked(end - start);
    int len = 0;

    for (char *p = start + 1; p < end;) {
        if (*p == '\\')
            buf[len++] = read_escaped_char(&p, p + 1);
        else
            buf[len++] = *p++;
    }

    buf[len] = '\0';

    Token *tok = new_token(TK_STR, start, end + 1);
    tok->str = buf;
    return tok;
}

//
// Character literal parsing
//

static Token *read_char_literal(char *start) {
    char *p = start + 1;
    if (*p == '\0')
        error_at(start, "unclosed character literal");

    int c;
    if (*p == '\\')
        c = read_escaped_char(&p, p + 1);
    else
        c = *p++;

    if (*p != '\'')
        error_at(start, "character literal too long");
    p++;

    Token *tok = new_token(TK_NUM, start, p);
    tok->val = c;
    return tok;
}

//
// Preprocessing number conversion
//

static bool convert_pp_int(Token *tok) {
    char *p = tok->loc;

    // Handle binary literals (0b...)
    if (startswith(p, "0b") || startswith(p, "0B")) {
        tok->val = strtoul(p + 2, NULL, 2);
        return true;
    }

    // Handle octal/hex/decimal
    tok->val = strtoul(p, &p, 0);

    // Handle integer suffixes (u, l, ul, ll, etc.)
    bool is_unsigned = false;
    bool is_long = false;
    bool is_longlong = false;

    if (*p == 'U' || *p == 'u') {
        is_unsigned = true;
        p++;
    }

    if (*p == 'L' || *p == 'l') {
        is_long = true;
        p++;
        if (*p == 'L' || *p == 'l') {
            is_longlong = true;
            p++;
        }
    }

    if (*p == 'U' || *p == 'u') {
        is_unsigned = true;
        p++;
    }

    // For MiniC, we support int and long
    // (Note: long is 32-bit, no 64-bit long long on DOS)
    return p == tok->loc + tok->len;
}

static void convert_pp_number(Token *tok) {
    char *p = tok->loc;

    // Try integer first
    if (convert_pp_int(tok))
        return;

    // Must be floating-point
    char *end;
    tok->fval = strtod(p, &end);

    // Handle float suffixes (f, l)
    if (*end == 'f' || *end == 'F') {
        end++;
    } else if (*end == 'l' || *end == 'L') {
        end++;
    }

    if (end != tok->loc + tok->len)
        error_tok(tok, "invalid numeric constant");
}

void convert_pp_tokens(Token *tok) {
    for (Token *t = tok; t->kind != TK_EOF; t = t->next) {
        if (t->kind == TK_PP_NUM)
            convert_pp_number(t);
    }
}

//
// Line number assignment
//

static void add_line_numbers(Token *tok) {
    char *p = current_file->contents;
    int n = 1;

    do {
        if (p == tok->loc) {
            tok->line_no = n;
            tok = tok->next;
        }
        if (*p == '\n')
            n++;
    } while (*p++);
}

//
// Main tokenization function
//

Token *tokenize(File *file) {
    current_file = file;

    char *p = file->contents;
    Token head = {};
    Token *cur = &head;

    at_bol = true;
    has_space = false;

    while (*p) {
        // Skip line comments (//)
        if (startswith(p, "//")) {
            p += 2;
            while (*p != '\n' && *p != '\0')
                p++;
            has_space = true;
            continue;
        }

        // Skip block comments (/* ... */)
        if (startswith(p, "/*")) {
            char *q = strstr(p + 2, "*/");
            if (!q)
                error_at(p, "unclosed block comment");
            p = q + 2;
            has_space = true;
            continue;
        }

        // Skip newline
        if (*p == '\n') {
            p++;
            at_bol = true;
            has_space = false;
            continue;
        }

        // Skip whitespace
        if (isspace(*p)) {
            p++;
            has_space = true;
            continue;
        }

        // Numeric literal (including .5 floats)
        if (isdigit(*p) || (*p == '.' && isdigit(p[1]))) {
            char *q = p++;

            // Read the entire preprocessing number
            for (;;) {
                if (p[0] && p[1] && strchr("eEpP", p[0]) && strchr("+-", p[1]))
                    p += 2;  // Exponent with sign
                else if (isalnum(*p) || *p == '.')
                    p++;
                else
                    break;
            }

            cur = cur->next = new_token(TK_PP_NUM, q, p);
            continue;
        }

        // String literal
        if (*p == '"') {
            cur = cur->next = read_string_literal(p);
            p += cur->len;
            continue;
        }

        // Character literal
        if (*p == '\'') {
            cur = cur->next = read_char_literal(p);
            p += cur->len;
            continue;
        }

        // Identifier or keyword
        int ident_len = read_ident(p);
        if (ident_len) {
            cur = cur->next = new_token(TK_IDENT, p, p + ident_len);
            p += cur->len;
            continue;
        }

        // Punctuators (operators)
        int punct_len = read_punct(p);
        if (punct_len) {
            cur = cur->next = new_token(TK_PUNCT, p, p + punct_len);
            p += cur->len;
            continue;
        }

        error_at(p, "invalid token");
    }

    cur = cur->next = new_token(TK_EOF, p, p);
    add_line_numbers(head.next);
    return head.next;
}

//
// File reading
//

static char *read_file(char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp)
        return NULL;

    // Get file size
    if (fseek(fp, 0, SEEK_END) == -1)
        error("fseek failed: %s", strerror(errno));
    size_t size = ftell(fp);
    if (fseek(fp, 0, SEEK_SET) == -1)
        error("fseek failed: %s", strerror(errno));

    // Read entire file
    char *buf = malloc_checked(size + 2);
    size_t n = fread(buf, 1, size, fp);
    if (n != size)
        error("fread failed: %s", strerror(errno));

    // Ensure null termination with two nulls (for lookahead)
    buf[size] = '\0';
    buf[size + 1] = '\0';

    fclose(fp);
    return buf;
}

File *new_file(char *name, int file_no, char *contents) {
    File *file = calloc_checked(1, sizeof(File));
    file->name = name;
    file->file_no = file_no;
    file->contents = contents;
    file->display_name = name;
    return file;
}

Token *tokenize_file(char *path) {
    char *contents = read_file(path);
    if (!contents)
        error("cannot open %s: %s", path, strerror(errno));

    File *file = new_file(path, num_input_files + 1, contents);
    return tokenize(file);
}

Token *tokenize_string(char *str) {
    File *file = new_file("<string>", 0, str);
    return tokenize(file);
}
