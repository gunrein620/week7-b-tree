#include "config.h"
#include "executor.h"
#include "lexer.h"
#include "parser.h"
#include "test_helpers.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int run_statement(const char *sql) {
    Token *tokens;
    Statement *stmt;
    int token_count = 0;
    int result;

    tokens = tokenize(sql, &token_count);
    if (tokens == NULL) {
        return -2;
    }

    stmt = parse(tokens, token_count);
    free_tokens(tokens);
    if (stmt == NULL) {
        return -2;
    }

    result = execute(stmt);
    free_statement(stmt);
    return result;
}

static char *repeat_char(char ch, size_t count) {
    char *buffer = (char *)malloc(count + 1);

    if (buffer == NULL) {
        return NULL;
    }

    memset(buffer, ch, count);
    buffer[count] = '\0';
    return buffer;
}

static int test_oversized_string_literal_must_fail_not_truncate(void) {
    char workspace[PATH_MAX];
    char schema_dir[PATH_MAX];
    char data_dir[PATH_MAX];
    char schema_path[PATH_MAX];
    char *payload = NULL;
    char *sql = NULL;
    int ok = 1;

    if (!th_setup_workspace("edge_token_string", workspace, sizeof(workspace))) {
        return th_fail("failed to create temporary workspace");
    }

    th_join_path(schema_dir, sizeof(schema_dir), workspace, "schemas");
    th_join_path(data_dir, sizeof(data_dir), workspace, "data");
    th_join_path(schema_path, sizeof(schema_path), schema_dir, "long_values.schema");

    if (!th_write_text_file(schema_path,
                            "id,INT,0,0,1\n"
                            "payload,VARCHAR,400,0,0\n")) {
        th_remove_tree(workspace);
        return th_fail("failed to write long_values schema");
    }

    config_set_schema_dir(schema_dir);
    config_set_data_dir(data_dir);

    payload = repeat_char('x', 300);
    if (payload == NULL) {
        th_remove_tree(workspace);
        return th_fail("failed to allocate oversized payload");
    }

    sql = (char *)malloc(strlen(payload) + 128);
    if (sql == NULL) {
        free(payload);
        th_remove_tree(workspace);
        return th_fail("failed to allocate SQL buffer");
    }

    snprintf(sql,
             strlen(payload) + 128,
             "INSERT INTO long_values (id, payload) VALUES (1, '%s');",
             payload);

    if (run_statement(sql) == 0) {
        ok = th_fail("oversized string literal was silently truncated and accepted");
    }

    free(sql);
    free(payload);
    th_remove_tree(workspace);
    return ok;
}

static int test_oversized_identifier_must_fail_not_truncate(void) {
    char workspace[PATH_MAX];
    char schema_dir[PATH_MAX];
    char data_dir[PATH_MAX];
    char schema_path[PATH_MAX];
    char *valid_column = NULL;
    char *oversized_ident = NULL;
    char *schema_text = NULL;
    char *sql = NULL;
    int ok = 1;

    if (!th_setup_workspace("edge_token_ident", workspace, sizeof(workspace))) {
        return th_fail("failed to create temporary workspace");
    }

    th_join_path(schema_dir, sizeof(schema_dir), workspace, "schemas");
    th_join_path(data_dir, sizeof(data_dir), workspace, "data");
    th_join_path(schema_path, sizeof(schema_path), schema_dir, "long_ident.schema");

    valid_column = repeat_char('a', 63);
    oversized_ident = repeat_char('a', 300);
    if (valid_column == NULL || oversized_ident == NULL) {
        free(valid_column);
        free(oversized_ident);
        th_remove_tree(workspace);
        return th_fail("failed to allocate identifier buffers");
    }

    schema_text = (char *)malloc(strlen(valid_column) + 64);
    if (schema_text == NULL) {
        free(valid_column);
        free(oversized_ident);
        th_remove_tree(workspace);
        return th_fail("failed to allocate schema buffer");
    }

    snprintf(schema_text,
             strlen(valid_column) + 64,
             "id,INT,0,0,1\n%s,VARCHAR,16,1,0\n",
             valid_column);

    if (!th_write_text_file(schema_path, schema_text)) {
        free(schema_text);
        free(valid_column);
        free(oversized_ident);
        th_remove_tree(workspace);
        return th_fail("failed to write long_ident schema");
    }

    config_set_schema_dir(schema_dir);
    config_set_data_dir(data_dir);

    sql = (char *)malloc(strlen(oversized_ident) + 64);
    if (sql == NULL) {
        free(schema_text);
        free(valid_column);
        free(oversized_ident);
        th_remove_tree(workspace);
        return th_fail("failed to allocate SQL buffer");
    }

    snprintf(sql,
             strlen(oversized_ident) + 64,
             "SELECT %s FROM long_ident;",
             oversized_ident);

    if (run_statement(sql) == 0) {
        ok = th_fail("oversized identifier was silently truncated and accepted");
    }

    free(sql);
    free(schema_text);
    free(valid_column);
    free(oversized_ident);
    th_remove_tree(workspace);
    return ok;
}

int main(void) {
    int passed = 0;
    int failed = 0;

    th_reset_reason();
    if (test_oversized_string_literal_must_fail_not_truncate()) {
        passed++;
        th_print_result("oversized_string_literal_must_fail_not_truncate", 1);
    } else {
        failed++;
        th_print_result("oversized_string_literal_must_fail_not_truncate", 0);
    }

    th_reset_reason();
    if (test_oversized_identifier_must_fail_not_truncate()) {
        passed++;
        th_print_result("oversized_identifier_must_fail_not_truncate", 1);
    } else {
        failed++;
        th_print_result("oversized_identifier_must_fail_not_truncate", 0);
    }

    printf("Tests: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
