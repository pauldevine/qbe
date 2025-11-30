/* tokenize_test.c - Simple test for the tokenizer */

#include "minic_token.h"

static void print_tokens(Token *tok) {
    for (; tok->kind != TK_EOF; tok = tok->next) {
        printf("[%d] ", tok->line_no);

        switch (tok->kind) {
        case TK_IDENT:
            printf("IDENT: %.*s\n", tok->len, tok->loc);
            break;
        case TK_NUM:
            if (tok->fval != 0.0)
                printf("NUM: %g (float)\n", tok->fval);
            else
                printf("NUM: %ld (int)\n", tok->val);
            break;
        case TK_PP_NUM:
            printf("PP_NUM: %.*s\n", tok->len, tok->loc);
            break;
        case TK_STR:
            printf("STR: \"%s\"\n", tok->str);
            break;
        case TK_PUNCT:
            printf("PUNCT: %.*s\n", tok->len, tok->loc);
            break;
        default:
            printf("OTHER: %.*s\n", tok->len, tok->loc);
        }
    }
    printf("[EOF]\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <source.c>\n", argv[0]);
        return 1;
    }

    printf("Tokenizing: %s\n", argv[1]);
    printf("================\n");

    Token *tok = tokenize_file(argv[1]);
    convert_pp_tokens(tok);  // Convert TK_PP_NUM to TK_NUM
    print_tokens(tok);

    return 0;
}
