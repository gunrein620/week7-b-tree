#include "lexer.h"
#include "test_helpers.h"

#include <string.h>

static int test_empty_input(void) {
    Token *tokens;
    int token_count = 0;

    tokens = tokenize("", &token_count);
    if (tokens == NULL) {
        return th_fail("tokenize returned NULL for empty input");
    }
    if (token_count != 1 || tokens[0].type != TOKEN_EOF) {
        free_tokens(tokens);
        return th_fail("empty input should produce only EOF");
    }

    free_tokens(tokens);
    return 1;
}

static int test_case_insensitive_select(void) {
    Token *tokens;
    int token_count = 0;

    tokens = tokenize("SeLeCt id, name FROM members WHERE age >= 20;", &token_count);
    if (tokens == NULL) {
        return th_fail("tokenize returned NULL for SELECT");
    }

    if (tokens[0].type != TOKEN_SELECT || strcmp(tokens[1].value, "id") != 0 ||
        tokens[3].type != TOKEN_IDENT || strcmp(tokens[3].value, "name") != 0 ||
        tokens[4].type != TOKEN_FROM || tokens[7].type != TOKEN_IDENT ||
        strcmp(tokens[7].value, "age") != 0 || tokens[8].type != TOKEN_GTE ||
        strcmp(tokens[9].value, "20") != 0) {
        free_tokens(tokens);
        return th_fail("SELECT tokens did not match expected sequence");
    }

    free_tokens(tokens);
    return 1;
}

static int test_string_escape_and_comments(void) {
    Token *tokens;
    int token_count = 0;
    int found_string = 0;
    int found_neq = 0;

    tokens = tokenize("-- ignore me\nINSERT INTO members (name) VALUES ('it''s');"
                      "SELECT * FROM members WHERE age <> 3;",
                      &token_count);
    if (tokens == NULL) {
        return th_fail("tokenize returned NULL for escaped string");
    }

    for (int i = 0; i < token_count; ++i) {
        if (tokens[i].type == TOKEN_STRING && strcmp(tokens[i].value, "it's") == 0) {
            found_string = 1;
        }
        if (tokens[i].type == TOKEN_NEQ && strcmp(tokens[i].value, "<>") == 0) {
            found_neq = 1;
        }
    }

    free_tokens(tokens);

    if (!found_string) {
        return th_fail("escaped string literal was not decoded");
    }
    if (!found_neq) {
        return th_fail("<> operator was not tokenized");
    }

    return 1;
}

static int test_order_by_keywords(void) {
    Token *tokens;
    int token_count = 0;

    tokens = tokenize("select * from members order by age desc;", &token_count);
    if (tokens == NULL) {
        return th_fail("tokenize returned NULL for ORDER BY");
    }

    if (tokens[4].type != TOKEN_ORDER || tokens[5].type != TOKEN_BY ||
        strcmp(tokens[6].value, "age") != 0 || tokens[7].type != TOKEN_DESC) {
        free_tokens(tokens);
        return th_fail("ORDER BY tokens were not recognized");
    }

    free_tokens(tokens);
    return 1;
}

static int test_negative_number_literal(void) {
    Token *tokens;
    int token_count = 0;
    int found_negative = 0;

    tokens = tokenize("INSERT INTO members (age) VALUES (-55);", &token_count);
    if (tokens == NULL) {
        return th_fail("tokenize returned NULL for negative number");
    }

    for (int i = 0; i < token_count; ++i) {
        if (tokens[i].type == TOKEN_NUMBER && strcmp(tokens[i].value, "-55") == 0) {
            found_negative = 1;
            break;
        }
    }

    free_tokens(tokens);

    if (!found_negative) {
        return th_fail("negative numeric literal was not preserved");
    }

    return 1;
}

static int test_unterminated_string(void) {
    Token *tokens;
    int token_count = 0;

    tokens = tokenize("SELECT 'oops", &token_count);
    if (tokens != NULL) {
        free_tokens(tokens);
        return th_fail("unterminated string should fail");
    }

    return 1;
}

int main(void) {
    int passed = 0;
    int failed = 0;

    th_reset_reason();
    if (test_empty_input()) {
        passed++;
        th_print_result("empty_input", 1);
    } else {
        failed++;
        th_print_result("empty_input", 0);
    }

    th_reset_reason();
    if (test_case_insensitive_select()) {
        passed++;
        th_print_result("case_insensitive_select", 1);
    } else {
        failed++;
        th_print_result("case_insensitive_select", 0);
    }

    th_reset_reason();
    if (test_string_escape_and_comments()) {
        passed++;
        th_print_result("string_escape_and_comments", 1);
    } else {
        failed++;
        th_print_result("string_escape_and_comments", 0);
    }

    th_reset_reason();
    if (test_order_by_keywords()) {
        passed++;
        th_print_result("order_by_keywords", 1);
    } else {
        failed++;
        th_print_result("order_by_keywords", 0);
    }

    th_reset_reason();
    if (test_negative_number_literal()) {
        passed++;
        th_print_result("negative_number_literal", 1);
    } else {
        failed++;
        th_print_result("negative_number_literal", 0);
    }

    th_reset_reason();
    if (test_unterminated_string()) {
        passed++;
        th_print_result("unterminated_string", 1);
    } else {
        failed++;
        th_print_result("unterminated_string", 0);
    }

    printf("Tests: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
