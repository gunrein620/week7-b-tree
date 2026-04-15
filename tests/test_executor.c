#include "config.h"
#include "executor.h"
#include "lexer.h"
#include "parser.h"
#include "test_helpers.h"

#include <limits.h>
#include <stdio.h>

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

static int test_duplicate_primary_key_rejected(void) {
    char workspace[PATH_MAX];
    char schema_dir[PATH_MAX];
    char data_dir[PATH_MAX];

    if (!th_setup_workspace("executor_pk", workspace, sizeof(workspace))) {
        return th_fail("failed to create temporary workspace");
    }

    th_join_path(schema_dir, sizeof(schema_dir), workspace, "schemas");
    th_join_path(data_dir, sizeof(data_dir), workspace, "data");

    if (!th_write_members_schema(schema_dir)) {
        th_remove_tree(workspace);
        return th_fail("failed to write members schema");
    }

    config_set_schema_dir(schema_dir);
    config_set_data_dir(data_dir);

    if (run_statement(
            "INSERT INTO members (id, name, grade, class, age) VALUES (1, 'Alice', 'vip', 'advanced', 30);") !=
        0) {
        th_remove_tree(workspace);
        return th_fail("first insert should succeed");
    }

    if (run_statement(
            "INSERT INTO members (id, name, grade, class, age) VALUES (1, 'Bob', 'normal', 'basic', 22);") !=
        -1) {
        th_remove_tree(workspace);
        return th_fail("duplicate primary key should fail");
    }

    th_remove_tree(workspace);
    return 1;
}

static int test_varchar_length_rejected(void) {
    char workspace[PATH_MAX];
    char schema_dir[PATH_MAX];
    char data_dir[PATH_MAX];

    if (!th_setup_workspace("executor_varchar", workspace, sizeof(workspace))) {
        return th_fail("failed to create temporary workspace");
    }

    th_join_path(schema_dir, sizeof(schema_dir), workspace, "schemas");
    th_join_path(data_dir, sizeof(data_dir), workspace, "data");

    if (!th_write_members_schema(schema_dir)) {
        th_remove_tree(workspace);
        return th_fail("failed to write members schema");
    }

    config_set_schema_dir(schema_dir);
    config_set_data_dir(data_dir);

    if (run_statement(
            "INSERT INTO members (id, name, grade, class, age) VALUES (2, 'ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFG', 'vip', 'advanced', 30);") !=
        -1) {
        th_remove_tree(workspace);
        return th_fail("name longer than 32 chars should fail");
    }

    th_remove_tree(workspace);
    return 1;
}

static int test_unknown_select_column_rejected(void) {
    char workspace[PATH_MAX];
    char schema_dir[PATH_MAX];
    char data_dir[PATH_MAX];

    if (!th_setup_workspace("executor_select", workspace, sizeof(workspace))) {
        return th_fail("failed to create temporary workspace");
    }

    th_join_path(schema_dir, sizeof(schema_dir), workspace, "schemas");
    th_join_path(data_dir, sizeof(data_dir), workspace, "data");

    if (!th_write_members_schema(schema_dir)) {
        th_remove_tree(workspace);
        return th_fail("failed to write members schema");
    }

    config_set_schema_dir(schema_dir);
    config_set_data_dir(data_dir);

    if (run_statement("SELECT unknown_column FROM members;") != -1) {
        th_remove_tree(workspace);
        return th_fail("unknown select column should fail");
    }

    th_remove_tree(workspace);
    return 1;
}

static int test_unknown_order_by_column_rejected(void) {
    char workspace[PATH_MAX];
    char schema_dir[PATH_MAX];
    char data_dir[PATH_MAX];

    if (!th_setup_workspace("executor_order", workspace, sizeof(workspace))) {
        return th_fail("failed to create temporary workspace");
    }

    th_join_path(schema_dir, sizeof(schema_dir), workspace, "schemas");
    th_join_path(data_dir, sizeof(data_dir), workspace, "data");

    if (!th_write_members_schema(schema_dir)) {
        th_remove_tree(workspace);
        return th_fail("failed to write members schema");
    }

    config_set_schema_dir(schema_dir);
    config_set_data_dir(data_dir);

    if (run_statement("SELECT * FROM members ORDER BY unknown;") != -1) {
        th_remove_tree(workspace);
        return th_fail("unknown ORDER BY column should fail");
    }

    th_remove_tree(workspace);
    return 1;
}

static int test_int_overflow_rejected(void) {
    char workspace[PATH_MAX];
    char schema_dir[PATH_MAX];
    char data_dir[PATH_MAX];

    if (!th_setup_workspace("executor_int_overflow", workspace, sizeof(workspace))) {
        return th_fail("failed to create temporary workspace");
    }

    th_join_path(schema_dir, sizeof(schema_dir), workspace, "schemas");
    th_join_path(data_dir, sizeof(data_dir), workspace, "data");

    if (!th_write_members_schema(schema_dir)) {
        th_remove_tree(workspace);
        return th_fail("failed to write members schema");
    }

    config_set_schema_dir(schema_dir);
    config_set_data_dir(data_dir);

    if (run_statement(
            "INSERT INTO members (id, name, grade, class, age) VALUES (3, 'Alice', 'vip', 'advanced', 999999999999999999999);") !=
        -1) {
        th_remove_tree(workspace);
        return th_fail("INT overflow should fail");
    }

    th_remove_tree(workspace);
    return 1;
}

static int test_float_and_date_validation_rejected(void) {
    char workspace[PATH_MAX];
    char schema_dir[PATH_MAX];
    char data_dir[PATH_MAX];
    char schema_path[PATH_MAX];

    if (!th_setup_workspace("executor_float_date", workspace, sizeof(workspace))) {
        return th_fail("failed to create temporary workspace");
    }

    th_join_path(schema_dir, sizeof(schema_dir), workspace, "schemas");
    th_join_path(data_dir, sizeof(data_dir), workspace, "data");
    th_join_path(schema_path, sizeof(schema_path), schema_dir, "typed_values.schema");

    if (!th_write_text_file(schema_path,
                            "id,INT,0,0,1\n"
                            "score,FLOAT,0,0,0\n"
                            "created_at,DATE,0,0,0\n")) {
        th_remove_tree(workspace);
        return th_fail("failed to write typed_values schema");
    }

    config_set_schema_dir(schema_dir);
    config_set_data_dir(data_dir);

    if (run_statement(
            "INSERT INTO typed_values (id, score, created_at) VALUES (1, 'nan', '2026-04-08');") !=
        -1) {
        th_remove_tree(workspace);
        return th_fail("non-finite FLOAT should fail");
    }

    if (run_statement(
            "INSERT INTO typed_values (id, score, created_at) VALUES (2, 1.5, '2026-02-31');") !=
        -1) {
        th_remove_tree(workspace);
        return th_fail("invalid DATE should fail");
    }

    th_remove_tree(workspace);
    return 1;
}

int main(void) {
    int passed = 0;
    int failed = 0;

    th_reset_reason();
    if (test_duplicate_primary_key_rejected()) {
        passed++;
        th_print_result("duplicate_primary_key_rejected", 1);
    } else {
        failed++;
        th_print_result("duplicate_primary_key_rejected", 0);
    }

    th_reset_reason();
    if (test_varchar_length_rejected()) {
        passed++;
        th_print_result("varchar_length_rejected", 1);
    } else {
        failed++;
        th_print_result("varchar_length_rejected", 0);
    }

    th_reset_reason();
    if (test_unknown_select_column_rejected()) {
        passed++;
        th_print_result("unknown_select_column_rejected", 1);
    } else {
        failed++;
        th_print_result("unknown_select_column_rejected", 0);
    }

    th_reset_reason();
    if (test_unknown_order_by_column_rejected()) {
        passed++;
        th_print_result("unknown_order_by_column_rejected", 1);
    } else {
        failed++;
        th_print_result("unknown_order_by_column_rejected", 0);
    }

    th_reset_reason();
    if (test_int_overflow_rejected()) {
        passed++;
        th_print_result("int_overflow_rejected", 1);
    } else {
        failed++;
        th_print_result("int_overflow_rejected", 0);
    }

    th_reset_reason();
    if (test_float_and_date_validation_rejected()) {
        passed++;
        th_print_result("float_and_date_validation_rejected", 1);
    } else {
        failed++;
        th_print_result("float_and_date_validation_rejected", 0);
    }

    printf("Tests: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
