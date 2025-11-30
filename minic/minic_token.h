/* minic_token.h - Token structures for MiniC preprocessor
 * Adapted from chibicc (MIT License) by Rui Ueyama
 * https://github.com/rui314/chibicc
 */

#ifndef MINIC_TOKEN_H
#define MINIC_TOKEN_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//
// Utility macros
//

#define MAX(x, y) ((x) < (y) ? (y) : (x))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

//
// Forward declarations
//

typedef struct Token Token;
typedef struct File File;
typedef struct Hideset Hideset;
typedef struct Macro Macro;
typedef struct MacroParam MacroParam;
typedef struct MacroArg MacroArg;

//
// File structure
//

struct File {
    char *name;           // Filename
    int file_no;          // File number (for tracking)
    char *contents;       // Entire file contents in memory

    // For #line directive
    char *display_name;   // Display name (if different from real name)
    int line_delta;       // Line number adjustment
};

//
// Token kinds
//

typedef enum {
    TK_IDENT,      // Identifiers
    TK_PUNCT,      // Punctuators (operators, delimiters)
    TK_KEYWORD,    // Keywords (not used in preprocessing, for future)
    TK_STR,        // String literals
    TK_NUM,        // Numeric literals
    TK_PP_NUM,     // Preprocessing numbers (not yet converted)
    TK_EOF,        // End-of-file marker
} TokenKind;

//
// Token structure
//

struct Token {
    TokenKind kind;       // Token kind
    Token *next;          // Next token in linked list

    // Token value (for TK_NUM)
    long val;             // Integer value (32-bit for DOS)
    double fval;          // Floating-point value

    // Token location in source
    char *loc;            // Pointer to token start in source
    int len;              // Token length

    // String literal contents (for TK_STR)
    char *str;            // String contents (null-terminated)

    // Source location tracking
    File *file;           // Source file
    char *filename;       // Filename (for error messages)
    int line_no;          // Line number
    int line_delta;       // Line adjustment (for #line directive)

    // Preprocessing state
    bool at_bol;          // True if at beginning of line
    bool has_space;       // True if preceded by whitespace
    Hideset *hideset;     // For macro expansion (prevent recursion)
    Token *origin;        // Original token (if expanded from macro)
};

//
// Hideset - Tracks macro expansion to prevent infinite recursion
// Based on Prossor algorithm
//

struct Hideset {
    Hideset *next;
    char *name;           // Macro name in hideset
};

//
// Macro structures
//

struct MacroParam {
    MacroParam *next;
    char *name;           // Parameter name
};

struct MacroArg {
    MacroArg *next;
    char *name;           // Argument name
    bool is_va_args;      // True if this is __VA_ARGS__
    Token *tok;           // Argument tokens
};

typedef Token *macro_handler_fn(Token *);

struct Macro {
    char *name;           // Macro name
    bool is_objlike;      // True if object-like, false if function-like
    MacroParam *params;   // Parameters (for function-like macros)
    char *va_args_name;   // Name for variadic args (usually __VA_ARGS__)
    Token *body;          // Replacement tokens
    macro_handler_fn *handler; // Built-in handler (for __LINE__, etc.)
};

//
// Conditional inclusion - Track #if/#ifdef/#ifndef blocks
//

typedef struct CondIncl CondIncl;
struct CondIncl {
    CondIncl *next;       // Next outer conditional block
    enum { IN_THEN, IN_ELIF, IN_ELSE } ctx; // Current context
    Token *tok;           // Token for error reporting
    bool included;        // True if we're including this block
};

//
// HashMap - For efficient macro lookup
//

typedef struct {
    char *key;
    int keylen;
    void *val;
} HashEntry;

typedef struct {
    HashEntry *buckets;
    int capacity;
    int used;
} HashMap;

//
// String array - For collecting strings
//

typedef struct {
    char **data;
    int capacity;
    int len;
} StringArray;

//
// tokenize.c - Tokenization functions
//

File *new_file(char *name, int file_no, char *contents);
Token *tokenize(File *file);
Token *tokenize_file(char *path);
Token *tokenize_string(char *str);
bool equal(Token *tok, char *op);
Token *skip(Token *tok, char *op);
bool consume(Token **rest, Token *tok, char *str);
Token *copy_token(Token *tok);
void convert_pp_tokens(Token *tok);

//
// preprocess.c - Preprocessing functions
//

void init_macros(void);
void define_macro(char *name, char *buf);
void undef_macro(char *name);
Token *preprocess(Token *tok);
char *search_include_paths(char *filename);
void add_include_path(char *path);

//
// support.c - Utility functions
//

// HashMap functions
void *hashmap_get(HashMap *map, char *key);
void *hashmap_get2(HashMap *map, char *key, int keylen);
void hashmap_put(HashMap *map, char *key, void *val);
void hashmap_put2(HashMap *map, char *key, int keylen, void *val);
void hashmap_delete(HashMap *map, char *key);
void hashmap_delete2(HashMap *map, char *key, int keylen);

// String array functions
void strarray_push(StringArray *arr, char *s);
char *format(char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Memory functions
void *calloc_checked(size_t nmemb, size_t size);
void *malloc_checked(size_t size);
char *strndup_checked(char *s, size_t n);

//
// Error reporting
//

void error(char *fmt, ...) __attribute__((format(printf, 1, 2))) __attribute__((noreturn));
void error_at(char *loc, char *fmt, ...) __attribute__((format(printf, 2, 3))) __attribute__((noreturn));
void error_tok(Token *tok, char *fmt, ...) __attribute__((format(printf, 2, 3))) __attribute__((noreturn));
void warn_tok(Token *tok, char *fmt, ...) __attribute__((format(printf, 2, 3)));

//
// Global state
//

extern File **input_files;
extern int num_input_files;

#endif /* MINIC_TOKEN_H */
