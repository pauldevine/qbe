/* minic_cpp.c - Standalone C preprocessor for MiniC
 *
 * Usage: minic_cpp [-Ipath] [-Dname[=value]] <input.c>
 *
 * Outputs preprocessed C code to stdout, which can be piped to minic.
 */

#include "minic_token.h"

// Print a token to output stream
static void print_token(FILE *out, Token *tok) {
    // Handle whitespace/newline before token
    if (tok->at_bol)
        fprintf(out, "\n");
    else if (tok->has_space)
        fprintf(out, " ");

    // Print token text
    fprintf(out, "%.*s", tok->len, tok->loc);
}

// Print all tokens as C code
static void print_tokens(FILE *out, Token *tok) {
    for (; tok->kind != TK_EOF; tok = tok->next) {
        print_token(out, tok);
    }
    fprintf(out, "\n");
}

static void usage(char *prog) {
    fprintf(stderr, "Usage: %s [-Ipath] [-Dname[=value]] <input.c>\n", prog);
    fprintf(stderr, "  -Ipath         Add include path\n");
    fprintf(stderr, "  -Dname         Define macro (value=1)\n");
    fprintf(stderr, "  -Dname=value   Define macro with value\n");
    exit(1);
}

int main(int argc, char **argv) {
    char *input_file = NULL;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strncmp(argv[i], "-I", 2) == 0) {
                // Include path
                char *path = argv[i] + 2;
                if (*path == '\0') {
                    if (++i >= argc) {
                        fprintf(stderr, "Error: -I requires an argument\n");
                        usage(argv[0]);
                    }
                    path = argv[i];
                }
                add_include_path(path);
            } else if (strncmp(argv[i], "-D", 2) == 0) {
                // Define macro
                char *def = argv[i] + 2;
                if (*def == '\0') {
                    if (++i >= argc) {
                        fprintf(stderr, "Error: -D requires an argument\n");
                        usage(argv[0]);
                    }
                    def = argv[i];
                }
                // Parse name=value
                char *eq = strchr(def, '=');
                if (eq) {
                    *eq = '\0';
                    define_macro(def, eq + 1);
                } else {
                    define_macro(def, "1");
                }
            } else {
                fprintf(stderr, "Unknown option: %s\n", argv[i]);
                usage(argv[0]);
            }
        } else {
            if (input_file) {
                fprintf(stderr, "Error: Multiple input files specified\n");
                usage(argv[0]);
            }
            input_file = argv[i];
        }
    }

    if (!input_file) {
        fprintf(stderr, "Error: No input file specified\n");
        usage(argv[0]);
    }

    // Initialize built-in macros
    init_macros();

    // Add current directory to include path
    add_include_path(".");

    // Tokenize the input file
    Token *tok = tokenize_file(input_file);
    if (!tok) {
        fprintf(stderr, "Error: Could not read file: %s\n", input_file);
        return 1;
    }

    // Preprocess
    tok = preprocess(tok);

    // Output preprocessed code
    print_tokens(stdout, tok);

    return 0;
}
