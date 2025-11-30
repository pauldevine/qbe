/* preprocess_test.c - Test preprocessor with includes */

#include "minic_token.h"

static void print_tokens(Token *tok) {
    Token *start = tok;  // Save start pointer
    int count = 0;
    for (; tok->kind != TK_EOF; tok = tok->next) {
        count++;
    }
    printf("Total tokens: %d\n", count);

    // Print first 20 tokens as sample
    tok = start;  // Reset to beginning
    int shown = 0;
    for (; tok->kind != TK_EOF && shown < 20; tok = tok->next, shown++) {
        printf("[%s:%d] ", tok->filename, tok->line_no);

        switch (tok->kind) {
        case TK_IDENT:
            printf("IDENT: %.*s\n", tok->len, tok->loc);
            break;
        case TK_NUM:
            if (tok->fval != 0.0)
                printf("NUM: %g\n", tok->fval);
            else
                printf("NUM: %ld\n", tok->val);
            break;
        case TK_STR:
            printf("STR: \"%s\"\n", tok->str);
            break;
        case TK_PUNCT:
            printf("PUNCT: %.*s\n", tok->len, tok->loc);
            break;
        default:
            printf("OTHER\n");
        }
    }
    if (tok->kind != TK_EOF)
        printf("... (%d more tokens)\n", count - shown);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [-Ipath] <source.c>\n", argv[0]);
        return 1;
    }

    // Parse command line arguments
    char *input_file = NULL;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-I", 2) == 0) {
            char *path = argv[i] + 2;
            if (*path == '\0' && i + 1 < argc)
                path = argv[++i];
            add_include_path(path);
            printf("Added include path: %s\n", path);
        } else {
            input_file = argv[i];
        }
    }

    if (!input_file) {
        fprintf(stderr, "No input file specified\n");
        return 1;
    }

    printf("\nPreprocessing: %s\n", input_file);
    printf("==================\n");

    // Tokenize
    Token *tok = tokenize_file(input_file);
    printf("Tokenized: OK\n");

    // Preprocess
    tok = preprocess(tok);
    printf("Preprocessed: OK\n\n");

    // Print result
    print_tokens(tok);

    return 0;
}
