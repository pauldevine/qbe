/* minic_preprocess.c - C preprocessor for MiniC
 * Adapted from chibicc (MIT License) by Rui Ueyama
 * https://github.com/rui314/chibicc
 *
 * Week 3: File inclusion (#include)
 * Week 4: Simple macros (#define, #undef)
 * Week 5: Conditionals (#ifdef, #if)
 * Week 6-7: Advanced macros
 */

#include "minic_token.h"
#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <sys/stat.h>
#include <time.h>

//
// Global state
//

static HashMap macros;                  // Macro definitions
static StringArray include_paths;       // -I paths
static HashMap pragma_once;             // #pragma once tracking
static HashMap include_guards;          // Include guard optimization
static CondIncl *cond_incl;             // Conditional inclusion stack

//
// Utility functions
//

static bool is_hash(Token *tok) {
    return tok->at_bol && equal(tok, "#");
}

static Token *skip_line(Token *tok) {
    if (tok->at_bol)
        return tok;
    warn_tok(tok, "extra token");
    while (!tok->at_bol && tok->kind != TK_EOF)
        tok = tok->next;
    return tok;
}

static Token *new_eof(Token *tok) {
    Token *t = copy_token(tok);
    t->kind = TK_EOF;
    t->len = 0;
    return t;
}

// Copy all tokens until next newline, terminate with EOF
static Token *copy_line(Token **rest, Token *tok) {
    Token head = {};
    Token *cur = &head;

    for (; !tok->at_bol && tok->kind != TK_EOF; tok = tok->next)
        cur = cur->next = copy_token(tok);

    cur->next = new_eof(tok);
    *rest = tok;
    return head.next;
}

// Create a string token
static Token *new_str_token(char *str, Token *tmpl) {
    // Quote the string
    int bufsize = 3;
    for (int i = 0; str[i]; i++) {
        if (str[i] == '\\' || str[i] == '"')
            bufsize++;
        bufsize++;
    }

    char *buf = malloc_checked(bufsize);
    char *p = buf;
    *p++ = '"';
    for (int i = 0; str[i]; i++) {
        if (str[i] == '\\' || str[i] == '"')
            *p++ = '\\';
        *p++ = str[i];
    }
    *p++ = '"';
    *p++ = '\0';

    return tokenize(new_file(tmpl->file->name, tmpl->file->file_no, buf));
}

// Create a number token
static Token *new_num_token(long val, Token *tmpl) {
    char *buf = format("%ld\n", val);
    return tokenize(new_file(tmpl->file->name, tmpl->file->file_no, buf));
}

// Append tok2 to the end of tok1
static Token *append(Token *tok1, Token *tok2) {
    if (tok1->kind == TK_EOF)
        return tok2;

    Token head = {};
    Token *cur = &head;

    for (; tok1->kind != TK_EOF; tok1 = tok1->next)
        cur = cur->next = copy_token(tok1);
    cur->next = tok2;
    return head.next;
}

// Join tokens between 'start' and 'end' into a single string
static char *join_tokens(Token *start, Token *end) {
    // Calculate length
    int len = end->loc - start->loc;
    char *buf = malloc_checked(len + 1);

    // Copy characters
    int pos = 0;
    for (Token *tok = start; tok != end; tok = tok->next) {
        if (pos > 0 && tok->has_space)
            buf[pos++] = ' ';
        strncpy(buf + pos, tok->loc, tok->len);
        pos += tok->len;
    }
    buf[pos] = '\0';
    return buf;
}

// Stringize tokens for # operator
// Converts macro argument tokens to a string literal
static Token *stringize(Token *hash, Token *arg) {
    // Calculate buffer size needed
    int len = 1;  // For opening quote
    for (Token *t = arg; t->kind != TK_EOF; t = t->next) {
        if (t != arg && t->has_space)
            len++;  // Space between tokens
        // For string literals, we need to escape quotes and backslashes
        if (t->kind == TK_STR) {
            for (int i = 0; i < t->len; i++) {
                if (t->loc[i] == '"' || t->loc[i] == '\\')
                    len++;
                len++;
            }
        } else {
            len += t->len;
        }
    }
    len++;  // Closing quote
    len++;  // Null terminator

    char *buf = malloc_checked(len);
    char *p = buf;
    *p++ = '"';

    for (Token *t = arg; t->kind != TK_EOF; t = t->next) {
        if (t != arg && t->has_space)
            *p++ = ' ';

        // For string literals, escape quotes and backslashes within
        if (t->kind == TK_STR) {
            for (int i = 0; i < t->len; i++) {
                if (t->loc[i] == '"' || t->loc[i] == '\\')
                    *p++ = '\\';
                *p++ = t->loc[i];
            }
        } else {
            memcpy(p, t->loc, t->len);
            p += t->len;
        }
    }

    *p++ = '"';
    *p = '\0';

    return tokenize(new_file(hash->file->name, hash->file->file_no, buf));
}

// Paste two tokens together for ## operator
static Token *paste(Token *lhs, Token *rhs) {
    // Concatenate token text
    int len = lhs->len + rhs->len;
    char *buf = malloc_checked(len + 2);  // +2 for newline and null
    memcpy(buf, lhs->loc, lhs->len);
    memcpy(buf + lhs->len, rhs->loc, rhs->len);
    buf[len] = '\n';
    buf[len + 1] = '\0';

    // Tokenize the concatenated string
    Token *tok = tokenize(new_file(lhs->file->name, lhs->file->file_no, buf));
    if (tok->next->kind != TK_EOF)
        error_tok(lhs, "pasting \"%.*s\" and \"%.*s\" does not produce a valid token",
                  lhs->len, lhs->loc, rhs->len, rhs->loc);
    return tok;
}

//
// Hideset functions (for macro recursion prevention)
// Based on Prossor algorithm
//

static Hideset *new_hideset(char *name) {
    Hideset *hs = calloc_checked(1, sizeof(Hideset));
    hs->name = name;
    return hs;
}

static Hideset *hideset_union(Hideset *hs1, Hideset *hs2) {
    Hideset head = {};
    Hideset *cur = &head;

    for (; hs1; hs1 = hs1->next)
        cur = cur->next = new_hideset(hs1->name);
    for (; hs2; hs2 = hs2->next)
        cur = cur->next = new_hideset(hs2->name);
    return head.next;
}

static bool hideset_contains(Hideset *hs, char *s, int len) {
    for (; hs; hs = hs->next)
        if (strlen(hs->name) == len && !strncmp(hs->name, s, len))
            return true;
    return false;
}

static Token *add_hideset(Token *tok, Hideset *hs) {
    Token head = {};
    Token *cur = &head;

    for (; tok->kind != TK_EOF; tok = tok->next) {
        Token *t = copy_token(tok);
        t->hideset = hideset_union(t->hideset, hs);
        cur = cur->next = t;
    }
    cur->next = tok;
    return head.next;
}

//
// Macro management
//

static Macro *find_macro(Token *tok) {
    if (tok->kind != TK_IDENT)
        return NULL;
    return hashmap_get2(&macros, tok->loc, tok->len);
}

static Macro *add_macro(char *name, bool is_objlike, Token *body) {
    Macro *m = calloc_checked(1, sizeof(Macro));
    m->name = name;
    m->is_objlike = is_objlike;
    m->body = body;
    hashmap_put(&macros, name, m);
    return m;
}

static void read_macro_definition(Token **rest, Token *tok) {
    if (tok->kind != TK_IDENT)
        error_tok(tok, "macro name must be an identifier");

    char *name = strndup_checked(tok->loc, tok->len);
    tok = tok->next;

    // Check if function-like macro (no space before '(')
    if (!tok->has_space && equal(tok, "(")) {
        // Function-like macro - parse parameters
        tok = tok->next;

        MacroParam head = {};
        MacroParam *cur = &head;
        char *va_args_name = NULL;

        while (!equal(tok, ")")) {
            if (cur != &head) {
                tok = skip(tok, ",");
            }

            // Check for variadic parameter
            if (equal(tok, "...")) {
                va_args_name = "__VA_ARGS__";
                tok = tok->next;
                break;
            }

            if (tok->kind != TK_IDENT)
                error_tok(tok, "expected parameter name");

            MacroParam *param = calloc_checked(1, sizeof(MacroParam));
            param->name = strndup_checked(tok->loc, tok->len);
            cur = cur->next = param;
            tok = tok->next;

            // Named variadic parameter: PARAM...
            if (equal(tok, "...")) {
                va_args_name = param->name;
                tok = tok->next;
                break;
            }
        }

        // Skip the closing ')'
        tok = skip(tok, ")");

        Macro *m = add_macro(name, false, copy_line(rest, tok));
        m->params = head.next;
        m->va_args_name = va_args_name;
        return;
    }

    // Object-like macro
    add_macro(name, true, copy_line(rest, tok));
}

//
// Macro expansion
//

// Read a macro argument (sequence of tokens between commas or parens)
static MacroArg *read_macro_arg_one(Token **rest, Token *tok, bool read_rest) {
    Token head = {};
    Token *cur = &head;
    int level = 0;

    while (true) {
        if (level == 0 && equal(tok, ")"))
            break;
        if (level == 0 && !read_rest && equal(tok, ","))
            break;

        if (tok->kind == TK_EOF)
            error_tok(tok, "premature end of input");

        if (equal(tok, "("))
            level++;
        else if (equal(tok, ")"))
            level--;

        cur = cur->next = copy_token(tok);
        tok = tok->next;
    }

    cur->next = new_eof(tok);

    MacroArg *arg = calloc_checked(1, sizeof(MacroArg));
    arg->tok = head.next;
    *rest = tok;
    return arg;
}

// Read macro arguments
static MacroArg *read_macro_args(Token **rest, Token *tok, MacroParam *params, char *va_args_name) {
    Token *start = tok;
    tok = tok->next->next;  // Skip macro name and '('

    MacroArg head = {};
    MacroArg *cur = &head;

    MacroParam *pp = params;

    // Read named parameters
    while (!equal(tok, ")")) {
        if (cur != &head)
            tok = skip(tok, ",");

        // If we've read all named parameters and have variadic, break to read varargs
        if (!pp && va_args_name)
            break;

        MacroArg *arg = read_macro_arg_one(&tok, tok, false);
        if (pp) {
            arg->name = pp->name;
            pp = pp->next;
        }
        cur = cur->next = arg;
    }

    // Handle variadic arguments
    if (va_args_name) {
        MacroArg *arg;
        if (equal(tok, ")")) {
            // No variadic args - create empty arg
            arg = calloc_checked(1, sizeof(MacroArg));
            arg->tok = new_eof(tok);
        } else {
            // Read all remaining args as variadic
            arg = read_macro_arg_one(&tok, tok, true);
        }
        arg->name = va_args_name;
        arg->is_va_args = true;
        cur = cur->next = arg;
    }

    tok = skip(tok, ")");
    *rest = tok;
    return head.next;
}

// Find argument by name
static MacroArg *find_arg(MacroArg *args, Token *tok) {
    for (MacroArg *ap = args; ap; ap = ap->next) {
        if (strlen(ap->name) == tok->len && !strncmp(ap->name, tok->loc, tok->len))
            return ap;
    }
    return NULL;
}

// Substitute macro parameters with arguments
// Handles # (stringification) and ## (token pasting) operators
static Token *subst(Token *tok, MacroArg *args) {
    Token head = {};
    Token *cur = &head;

    while (tok->kind != TK_EOF) {
        // Handle # stringification operator
        // #param -> "param_value"
        if (equal(tok, "#")) {
            MacroArg *arg = find_arg(args, tok->next);
            if (arg) {
                Token *str_tok = stringize(tok, arg->tok);
                cur = cur->next = str_tok;
                tok = tok->next->next;  // Skip # and param name
                continue;
            }
        }

        // Handle ## token pasting operator
        // x ## y -> xy
        // Also handles chained: x ## y ## z -> xyz
        if (equal(tok->next, "##")) {
            // Get left-hand side token
            Token *lhs;
            if (tok->kind == TK_IDENT) {
                MacroArg *arg = find_arg(args, tok);
                if (arg && arg->tok->kind != TK_EOF) {
                    // Parameter - use last token of argument
                    lhs = arg->tok;
                    while (lhs->next->kind != TK_EOF)
                        lhs = lhs->next;
                    lhs = copy_token(lhs);
                } else if (arg) {
                    // Empty argument - skip lhs entirely
                    tok = tok->next->next;  // Skip param and ##
                    continue;
                } else {
                    lhs = copy_token(tok);
                }
            } else {
                lhs = copy_token(tok);
            }

            tok = tok->next->next;  // Skip lhs and ##

            // Handle chained ## operators
            while (true) {
                // Get right-hand side token
                Token *rhs;
                if (tok->kind == TK_IDENT) {
                    MacroArg *arg = find_arg(args, tok);
                    if (arg && arg->tok->kind != TK_EOF) {
                        // Parameter - use first token of argument
                        rhs = copy_token(arg->tok);
                    } else if (arg) {
                        // Empty argument - use lhs as result
                        break;
                    } else {
                        rhs = copy_token(tok);
                    }
                } else {
                    rhs = copy_token(tok);
                }

                // Paste lhs and rhs together
                lhs = paste(lhs, rhs);
                tok = tok->next;  // Skip rhs

                // Check for another ##
                if (!equal(tok, "##"))
                    break;
                tok = tok->next;  // Skip ##
            }

            cur = cur->next = lhs;
            continue;
        }

        // Check if this token is a parameter (normal substitution)
        if (tok->kind == TK_IDENT) {
            MacroArg *arg = find_arg(args, tok);
            if (arg) {
                // Substitute with argument tokens
                for (Token *t = arg->tok; t->kind != TK_EOF; t = t->next) {
                    cur = cur->next = copy_token(t);
                }
                tok = tok->next;
                continue;
            }
        }

        // Not a parameter or operator - copy as-is
        cur = cur->next = copy_token(tok);
        tok = tok->next;
    }

    cur->next = tok;
    return head.next;
}

static bool expand_macro(Token **rest, Token *tok) {
    // Check if in hideset (prevent recursion)
    if (hideset_contains(tok->hideset, tok->loc, tok->len))
        return false;

    Macro *m = find_macro(tok);
    if (!m)
        return false;

    // Built-in dynamic macros (__LINE__, etc.)
    if (m->handler) {
        *rest = m->handler(tok);
        (*rest)->next = tok->next;
        return true;
    }

    // Object-like macro expansion
    if (m->is_objlike) {
        Hideset *hs = hideset_union(tok->hideset, new_hideset(m->name));
        Token *body = add_hideset(m->body, hs);

        // Mark origin for error messages
        for (Token *t = body; t->kind != TK_EOF; t = t->next)
            t->origin = tok;

        *rest = append(body, tok->next);
        (*rest)->at_bol = tok->at_bol;
        (*rest)->has_space = tok->has_space;
        return true;
    }

    // Function-like macro expansion
    // Must be followed by '('
    if (!equal(tok->next, "("))
        return false;

    // Read arguments
    Token *macro_token = tok;
    MacroArg *args = read_macro_args(&tok, tok, m->params, m->va_args_name);

    // Substitute parameters with arguments in macro body
    Token *body = subst(m->body, args);

    // Create hideset to prevent recursion
    Hideset *hs = hideset_union(macro_token->hideset, new_hideset(m->name));
    body = add_hideset(body, hs);

    // Mark origin for error messages
    for (Token *t = body; t->kind != TK_EOF; t = t->next)
        t->origin = macro_token;

    *rest = append(body, tok);
    (*rest)->at_bol = macro_token->at_bol;
    (*rest)->has_space = macro_token->has_space;
    return true;
}

//
// Conditional inclusion (#if, #ifdef, #ifndef)
//

static CondIncl *push_cond_incl(Token *tok, bool included) {
    CondIncl *ci = calloc_checked(1, sizeof(CondIncl));
    ci->next = cond_incl;
    ci->ctx = IN_THEN;
    ci->tok = tok;
    ci->included = included;
    cond_incl = ci;
    return ci;
}

static CondIncl *pop_cond_incl(void) {
    if (!cond_incl)
        error("stray #endif");
    CondIncl *ci = cond_incl;
    cond_incl = cond_incl->next;
    return ci;
}

// Skip tokens until we hit #elif, #else, or #endif at the same nesting level
static Token *skip_cond_incl(Token *tok) {
    int level = 0;
    for (; tok->kind != TK_EOF; tok = tok->next) {
        if (!is_hash(tok))
            continue;

        Token *hash = tok;
        tok = tok->next;

        if (equal(tok, "if") || equal(tok, "ifdef") || equal(tok, "ifndef")) {
            level++;
            continue;
        }

        if (level == 0 && (equal(tok, "elif") || equal(tok, "else") || equal(tok, "endif")))
            return hash;

        if (equal(tok, "endif")) {
            level--;
            continue;
        }
    }
    return tok;
}

// Evaluate constant expression for #if
// For now, just support defined(MACRO) and integer constants
static long eval_const_expr(Token **rest, Token *tok);

// Expand macros in #if expression, but leave "defined" operator alone
static Token *preprocess_if_expr(Token *tok) {
    Token head = {};
    Token *cur = &head;

    while (tok->kind != TK_EOF) {
        // Don't expand "defined" operator
        if (equal(tok, "defined")) {
            cur = cur->next = copy_token(tok);
            tok = tok->next;

            // Copy the macro name and optional parentheses
            if (equal(tok, "(")) {
                cur = cur->next = copy_token(tok);
                tok = tok->next;
            }
            if (tok->kind == TK_IDENT) {
                cur = cur->next = copy_token(tok);
                tok = tok->next;
            }
            if (equal(tok, ")")) {
                cur = cur->next = copy_token(tok);
                tok = tok->next;
            }
            continue;
        }

        // Try to expand macro
        Token *expanded;
        if (expand_macro(&expanded, tok)) {
            // Macro was expanded - continue processing from expanded tokens
            // expand_macro returns body + tok->next, so we process from there
            tok = expanded;
            continue;
        }

        // No expansion - copy token as-is
        cur = cur->next = copy_token(tok);
        tok = tok->next;
    }

    cur->next = new_eof(tok);
    return head.next;
}

static long eval_defined(Token **rest, Token *tok) {
    Token *start = tok;
    bool has_paren = consume(&tok, tok, "(");

    if (tok->kind != TK_IDENT)
        error_tok(start, "macro name must be an identifier");

    Macro *m = find_macro(tok);
    tok = tok->next;

    if (has_paren) {
        if (!consume(&tok, tok, ")"))
            error_tok(start, "missing closing parenthesis");
    }

    *rest = tok;
    return m ? 1 : 0;
}

// Primary expression: number, defined(), or ( expr )
static long eval_primary(Token **rest, Token *tok) {
    if (consume(&tok, tok, "(")) {
        long val = eval_const_expr(&tok, tok);
        *rest = skip(tok, ")");
        return val;
    }

    if (equal(tok, "defined"))
        return eval_defined(rest, tok->next);

    if (tok->kind == TK_NUM) {
        *rest = tok->next;
        return tok->val;
    }

    if (tok->kind == TK_PP_NUM) {
        *rest = tok->next;
        // Parse the number from the token text
        char *end;
        long val = strtol(tok->loc, &end, 0);  // Base 0 handles decimal, hex, octal
        return val;
    }

    // Undefined macro expands to 0 in #if
    if (tok->kind == TK_IDENT) {
        *rest = tok->next;
        return 0;
    }

    error_tok(tok, "invalid expression");
    return 0;
}

// Unary: +, -, !, ~
static long eval_unary(Token **rest, Token *tok) {
    if (equal(tok, "+"))
        return eval_unary(rest, tok->next);
    if (equal(tok, "-"))
        return -eval_unary(rest, tok->next);
    if (equal(tok, "!"))
        return !eval_unary(rest, tok->next);
    if (equal(tok, "~"))
        return ~eval_unary(rest, tok->next);
    return eval_primary(rest, tok);
}

// Multiplicative: *, /, %
static long eval_mul(Token **rest, Token *tok) {
    long val = eval_unary(&tok, tok);
    for (;;) {
        Token *start = tok;
        if (equal(tok, "*")) {
            val *= eval_unary(&tok, tok->next);
            continue;
        }
        if (equal(tok, "/")) {
            long rhs = eval_unary(&tok, tok->next);
            if (rhs == 0)
                error_tok(start, "division by zero");
            val /= rhs;
            continue;
        }
        if (equal(tok, "%")) {
            long rhs = eval_unary(&tok, tok->next);
            if (rhs == 0)
                error_tok(start, "modulo by zero");
            val %= rhs;
            continue;
        }
        *rest = tok;
        return val;
    }
}

// Additive: +, -
static long eval_add(Token **rest, Token *tok) {
    long val = eval_mul(&tok, tok);
    for (;;) {
        if (equal(tok, "+")) {
            val += eval_mul(&tok, tok->next);
            continue;
        }
        if (equal(tok, "-")) {
            val -= eval_mul(&tok, tok->next);
            continue;
        }
        *rest = tok;
        return val;
    }
}

// Shift: <<, >>
static long eval_shift(Token **rest, Token *tok) {
    long val = eval_add(&tok, tok);
    for (;;) {
        if (equal(tok, "<<")) {
            val <<= eval_add(&tok, tok->next);
            continue;
        }
        if (equal(tok, ">>")) {
            val >>= eval_add(&tok, tok->next);
            continue;
        }
        *rest = tok;
        return val;
    }
}

// Relational: <, <=, >, >=
static long eval_rel(Token **rest, Token *tok) {
    long val = eval_shift(&tok, tok);
    for (;;) {
        if (equal(tok, "<")) {
            val = val < eval_shift(&tok, tok->next);
            continue;
        }
        if (equal(tok, "<=")) {
            val = val <= eval_shift(&tok, tok->next);
            continue;
        }
        if (equal(tok, ">")) {
            val = val > eval_shift(&tok, tok->next);
            continue;
        }
        if (equal(tok, ">=")) {
            val = val >= eval_shift(&tok, tok->next);
            continue;
        }
        *rest = tok;
        return val;
    }
}

// Equality: ==, !=
static long eval_eq(Token **rest, Token *tok) {
    long val = eval_rel(&tok, tok);
    for (;;) {
        if (equal(tok, "==")) {
            val = val == eval_rel(&tok, tok->next);
            continue;
        }
        if (equal(tok, "!=")) {
            val = val != eval_rel(&tok, tok->next);
            continue;
        }
        *rest = tok;
        return val;
    }
}

// Bitwise AND: &
static long eval_bitand(Token **rest, Token *tok) {
    long val = eval_eq(&tok, tok);
    while (equal(tok, "&"))
        val &= eval_eq(&tok, tok->next);
    *rest = tok;
    return val;
}

// Bitwise XOR: ^
static long eval_bitxor(Token **rest, Token *tok) {
    long val = eval_bitand(&tok, tok);
    while (equal(tok, "^"))
        val ^= eval_bitand(&tok, tok->next);
    *rest = tok;
    return val;
}

// Bitwise OR: |
static long eval_bitor(Token **rest, Token *tok) {
    long val = eval_bitxor(&tok, tok);
    while (equal(tok, "|"))
        val |= eval_bitxor(&tok, tok->next);
    *rest = tok;
    return val;
}

// Logical AND: &&
static long eval_logand(Token **rest, Token *tok) {
    long val = eval_bitor(&tok, tok);
    while (equal(tok, "&&")) {
        Token *start = tok;
        long rhs = eval_bitor(&tok, tok->next);
        val = val && rhs;
    }
    *rest = tok;
    return val;
}

// Logical OR: ||
static long eval_logor(Token **rest, Token *tok) {
    long val = eval_logand(&tok, tok);
    while (equal(tok, "||")) {
        Token *start = tok;
        long rhs = eval_logand(&tok, tok->next);
        val = val || rhs;
    }
    *rest = tok;
    return val;
}

// Ternary: ? :
static long eval_ternary(Token **rest, Token *tok) {
    long val = eval_logor(&tok, tok);
    if (!equal(tok, "?")) {
        *rest = tok;
        return val;
    }

    long then_val = eval_const_expr(&tok, tok->next);
    tok = skip(tok, ":");
    long else_val = eval_const_expr(&tok, tok);
    *rest = tok;
    return val ? then_val : else_val;
}

// Top-level constant expression evaluator
static long eval_const_expr(Token **rest, Token *tok) {
    return eval_ternary(rest, tok);
}

//
// Include path management
//

void add_include_path(char *path) {
    strarray_push(&include_paths, path);
}

static bool file_exists(char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

char *search_include_paths(char *filename) {
    if (filename[0] == '/')
        return filename;

    // Search include paths
    for (int i = 0; i < include_paths.len; i++) {
        char *path = format("%s/%s", include_paths.data[i], filename);
        if (file_exists(path))
            return path;
    }

    return NULL;
}

//
// Include guard detection (optimization)
//
// Detects the pattern:
//   #ifndef FOO_H
//   #define FOO_H
//   ...
//   #endif
//
// This allows us to skip re-reading files we've already included
//

static char *detect_include_guard(Token *tok) {
    // Must start with #ifndef
    if (!is_hash(tok) || !equal(tok->next, "ifndef"))
        return NULL;

    tok = tok->next->next;

    if (tok->kind != TK_IDENT)
        return NULL;

    char *macro = strndup_checked(tok->loc, tok->len);
    tok = tok->next;

    // Must be followed by #define with same name
    if (!is_hash(tok) || !equal(tok->next, "define") || !equal(tok->next->next, macro))
        return NULL;

    // Scan to end looking for #endif
    while (tok->kind != TK_EOF) {
        if (!is_hash(tok)) {
            tok = tok->next;
            continue;
        }

        // Found matching #endif at end of file
        if (equal(tok->next, "endif") && tok->next->next->kind == TK_EOF)
            return macro;

        tok = tok->next;
    }

    return NULL;
}

//
// File inclusion
//

static Token *include_file(Token *tok, char *path, Token *filename_tok) {
    // Check for #pragma once
    if (hashmap_get(&pragma_once, path))
        return tok;

    // Check include guard cache
    char *guard_name = hashmap_get(&include_guards, path);
    if (guard_name && hashmap_get(&macros, guard_name))
        return tok;  // Already defined, skip file

    // Read and tokenize the file
    Token *tok2 = tokenize_file(path);
    if (!tok2)
        error_tok(filename_tok, "%s: %s", path, strerror(errno));

    // Detect include guard for future optimization
    guard_name = detect_include_guard(tok2);
    if (guard_name)
        hashmap_put(&include_guards, path, guard_name);

    // Append included tokens to current stream
    return append(tok2, tok);
}

//
// Read #include argument
//

static char *read_include_filename(Token **rest, Token *tok, bool *is_dquote) {
    // Pattern 1: #include "foo.h"
    if (tok->kind == TK_STR) {
        // Don't interpret escape sequences in include filenames
        // (e.g., "C:\foo" has backslash-f, not formfeed)
        *is_dquote = true;
        *rest = skip_line(tok->next);
        return strndup_checked(tok->loc + 1, tok->len - 2);
    }

    // Pattern 2: #include <foo.h>
    if (equal(tok, "<")) {
        Token *start = tok;

        // Find closing ">"
        for (; !equal(tok, ">"); tok = tok->next) {
            if (tok->at_bol || tok->kind == TK_EOF)
                error_tok(tok, "expected '>'");
        }

        *is_dquote = false;
        *rest = skip_line(tok->next);
        return join_tokens(start->next, tok);
    }

    // Pattern 3: #include FOO (macro expansion)
    // TODO: Implement in Week 4 when we have macro expansion
    if (tok->kind == TK_IDENT) {
        error_tok(tok, "macro expansion in #include not yet supported");
    }

    error_tok(tok, "expected a filename");
    return NULL;
}

//
// Main preprocessing function
//

// Forward declaration (will expand in later weeks)
static Token *preprocess2(Token *tok);

Token *preprocess(Token *tok) {
    static bool initialized = false;
    if (!initialized) {
        init_macros();
        initialized = true;
    }

    tok = preprocess2(tok);

    // Check for unclosed #if/#ifdef/#ifndef
    if (cond_incl)
        error_tok(cond_incl->tok, "unterminated conditional directive");

    convert_pp_tokens(tok);
    return tok;
}

static Token *preprocess2(Token *tok) {
    Token head = {};
    Token *cur = &head;

    while (tok->kind != TK_EOF) {
        // Macro expansion
        if (expand_macro(&tok, tok))
            continue;

        // Pass through if not a directive
        if (!is_hash(tok)) {
            cur = cur->next = tok;
            tok = tok->next;
            continue;
        }

        Token *start = tok;
        tok = tok->next;

        //
        // #include directive
        //
        if (equal(tok, "include")) {
            bool is_dquote;
            char *filename = read_include_filename(&tok, tok->next, &is_dquote);

            // Try relative path first for "..." includes
            if (filename[0] != '/' && is_dquote) {
                char *dir = dirname(strdup(start->file->name));
                char *path = format("%s/%s", dir, filename);
                if (file_exists(path)) {
                    tok = include_file(tok, path, start->next);
                    continue;
                }
            }

            // Search include paths
            char *path = search_include_paths(filename);
            if (!path)
                error_tok(start, "cannot find include file: %s", filename);

            tok = include_file(tok, path, start->next);
            continue;
        }

        //
        // #define directive
        //
        if (equal(tok, "define")) {
            read_macro_definition(&tok, tok->next);
            continue;
        }

        //
        // #undef directive
        //
        if (equal(tok, "undef")) {
            tok = tok->next;
            if (tok->kind != TK_IDENT)
                error_tok(tok, "macro name must be an identifier");
            char *name = strndup_checked(tok->loc, tok->len);
            hashmap_delete2(&macros, tok->loc, tok->len);
            tok = skip_line(tok->next);
            continue;
        }

        //
        // #if directive
        //
        if (equal(tok, "if")) {
            // Find where expression ends
            Token *expr_end = tok->next;
            while (!expr_end->at_bol && expr_end->kind != TK_EOF)
                expr_end = expr_end->next;

            // Copy and evaluate expression
            Token *dummy;
            Token *expr = copy_line(&dummy, tok->next);
            if (!expr || expr->kind == TK_EOF)
                error_tok(start, "no expression in #if");

            expr = preprocess_if_expr(expr);
            Token *eval_rest;
            long val = eval_const_expr(&eval_rest, expr);

            push_cond_incl(start, val);

            if (val) {
                // Condition is true - skip to start of next line (like #ifdef)
                tok = expr_end;
            } else {
                // Condition is false - skip to #elif/#else/#endif
                tok = skip_cond_incl(expr_end);
            }
            continue;
        }

        //
        // #ifdef directive
        //
        if (equal(tok, "ifdef")) {
            bool defined = find_macro(tok->next);
            push_cond_incl(start, defined);
            if (!defined)
                tok = skip_cond_incl(tok->next);
            else
                tok = skip_line(tok->next->next);
            continue;
        }

        //
        // #ifndef directive
        //
        if (equal(tok, "ifndef")) {
            bool defined = find_macro(tok->next);
            push_cond_incl(start, !defined);
            if (defined)
                tok = skip_cond_incl(tok->next);
            else
                tok = skip_line(tok->next->next);
            continue;
        }

        //
        // #elif directive
        //
        if (equal(tok, "elif")) {
            if (!cond_incl || cond_incl->ctx == IN_ELSE)
                error_tok(start, "stray #elif");
            cond_incl->ctx = IN_ELIF;

            if (cond_incl->included) {
                // Already included a branch, skip this one
                tok = skip_cond_incl(tok->next);
            } else {
                // Find end of line
                Token *expr_start = tok->next;
                Token *line_end = expr_start;
                while (!line_end->at_bol && line_end->kind != TK_EOF)
                    line_end = line_end->next;

                // Evaluate condition
                Token *dummy;
                Token *expr = copy_line(&dummy, expr_start);
                expr = preprocess_if_expr(expr);
                Token *rest;
                long val = eval_const_expr(&rest, expr);
                cond_incl->included = val;
                if (!val)
                    tok = skip_cond_incl(line_end);
                else
                    tok = line_end;
            }
            continue;
        }

        //
        // #else directive
        //
        if (equal(tok, "else")) {
            if (!cond_incl || cond_incl->ctx == IN_ELSE)
                error_tok(start, "stray #else");
            cond_incl->ctx = IN_ELSE;

            if (cond_incl->included) {
                // Already included a branch, skip else
                tok = skip_cond_incl(tok->next);
            } else {
                // Include the else branch
                cond_incl->included = true;
                tok = skip_line(tok->next);
            }
            continue;
        }

        //
        // #endif directive
        //
        if (equal(tok, "endif")) {
            pop_cond_incl();
            tok = skip_line(tok->next);
            continue;
        }

        //
        // #error directive
        //
        if (equal(tok, "error")) {
            error_tok(start, "error");
        }

        //
        // #pragma directive
        //
        if (equal(tok, "pragma")) {
            if (equal(tok->next, "once")) {
                hashmap_put(&pragma_once, start->file->name, (void *)1);
                tok = skip_line(tok->next->next);
                continue;
            }

            // Ignore other pragmas
            warn_tok(start, "ignoring #pragma");
            tok = skip_line(tok->next);
            continue;
        }

        //
        // #line directive
        //
        if (equal(tok, "line")) {
            // TODO: Implement if needed
            warn_tok(start, "ignoring #line");
            tok = skip_line(tok->next);
            continue;
        }

        // Unknown directive
        error_tok(start, "invalid preprocessing directive");
    }

    cur->next = tok;
    return head.next;
}

//
// Predefined macro handlers
//

static Token *file_macro(Token *tmpl) {
    return new_str_token(tmpl->file->display_name ? tmpl->file->display_name : tmpl->file->name, tmpl);
}

static Token *line_macro(Token *tmpl) {
    return new_num_token(tmpl->line_no, tmpl);
}

static char *get_date_str(void) {
    static char buf[20];
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(buf, sizeof(buf), "%b %e %Y", tm);
    return buf;
}

static char *get_time_str(void) {
    static char buf[20];
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(buf, sizeof(buf), "%T", tm);
    return buf;
}

static Token *date_macro(Token *tmpl) {
    return new_str_token(get_date_str(), tmpl);
}

static Token *time_macro(Token *tmpl) {
    return new_str_token(get_time_str(), tmpl);
}

//
// Macro initialization (Week 4)
//

void init_macros(void) {
    // Register predefined macros
    Macro *m;

    m = add_macro("__FILE__", true, NULL);
    m->handler = file_macro;

    m = add_macro("__LINE__", true, NULL);
    m->handler = line_macro;

    m = add_macro("__DATE__", true, NULL);
    m->handler = date_macro;

    m = add_macro("__TIME__", true, NULL);
    m->handler = time_macro;
}

void define_macro(char *name, char *buf) {
    Token *tok = tokenize(new_file("<command line>", 1, buf));
    add_macro(name, true, tok);
}

void undef_macro(char *name) {
    hashmap_delete(&macros, name);
}
