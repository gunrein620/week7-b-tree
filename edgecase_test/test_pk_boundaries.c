#include "config.h"
#include "executor.h"
#include "lexer.h"
#include "parser.h"
#include "test_helpers.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#ifdef _WIN32
#define TEST_SQLENGINE_COMMAND ".\\sqlengine.cmd"
#else
#define TEST_SQLENGINE_COMMAND "./sqlengine"
#endif

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

static int exit_code_from_system(int status) {
    if (status == -1) {
        return -1;
    }
#ifdef _WIN32
    return status;
#else
    if (!WIFEXITED(status)) {
        return -1;
    }
    return WEXITSTATUS(status);
#endif
}

static int setup_members_workspace(char *workspace,
                                   size_t workspace_size,
                                   char *schema_dir,
                                   size_t schema_dir_size,
                                   char *data_dir,
                                   size_t data_dir_size) {
    if (!th_setup_workspace("edge_pk", workspace, workspace_size)) {
        return 0;
    }

    th_join_path(schema_dir, schema_dir_size, workspace, "schemas");
    th_join_path(data_dir, data_dir_size, workspace, "data");
    if (!th_write_members_schema(schema_dir)) {
        th_remove_tree(workspace);
        return 0;
    }

    config_set_schema_dir(schema_dir);
    config_set_data_dir(data_dir);
    return 1;
}

static int test_insert_int32_max_then_auto_increment_must_not_wrap(void) {
    char workspace[PATH_MAX];
    char schema_dir[PATH_MAX];
    char data_dir[PATH_MAX];
    int ok = 1;

    if (!setup_members_workspace(workspace,
                                 sizeof(workspace),
                                 schema_dir,
                                 sizeof(schema_dir),
                                 data_dir,
                                 sizeof(data_dir))) {
        return th_fail("failed to prepare members workspace");
    }

    if (run_statement("INSERT INTO members (id, name, age) VALUES (2147483647, 'max_row', 30);") != 0) {
        ok = th_fail("initial INT32_MAX insert should succeed");
    }

    if (ok && run_statement("INSERT INTO members (name, age) VALUES ('wrapped_row', 31);") == 0) {
        ok = th_fail("auto increment wrapped past INT32_MAX instead of failing");
    }

    th_remove_tree(workspace);
    return ok;
}

static int test_explicit_out_of_range_pk_must_fail_cleanly(void) {
    char workspace[PATH_MAX];
    char schema_dir[PATH_MAX];
    char data_dir[PATH_MAX];
    int ok = 1;

    if (!setup_members_workspace(workspace,
                                 sizeof(workspace),
                                 schema_dir,
                                 sizeof(schema_dir),
                                 data_dir,
                                 sizeof(data_dir))) {
        return th_fail("failed to prepare members workspace");
    }

    if (run_statement("INSERT INTO members (id, name, age) VALUES (2147483648, 'too_big', 30);") == 0) {
        ok = th_fail("out-of-range INT primary key should be rejected");
    }

    th_remove_tree(workspace);
    return ok;
}

static int test_where_id_out_of_range_must_not_match_existing_row(void) {
    char workspace[PATH_MAX];
    char schema_dir[PATH_MAX];
    char data_dir[PATH_MAX];
    char out_path[PATH_MAX];
    char err_path[PATH_MAX];
    char command[4096];
    char *stdout_text = NULL;
    int exit_code;
    int ok = 1;

    if (!setup_members_workspace(workspace,
                                 sizeof(workspace),
                                 schema_dir,
                                 sizeof(schema_dir),
                                 data_dir,
                                 sizeof(data_dir))) {
        return th_fail("failed to prepare members workspace");
    }

    if (run_statement("INSERT INTO members (id, name, age) VALUES (2147483647, 'max_row', 30);") != 0 ||
        run_statement("INSERT INTO members (id, name, age) VALUES (-2147483648, 'min_row', 31);") != 0) {
        th_remove_tree(workspace);
        return th_fail("boundary rows should insert successfully");
    }

    th_join_path(out_path, sizeof(out_path), workspace, "stdout.txt");
    th_join_path(err_path, sizeof(err_path), workspace, "stderr.txt");

    snprintf(command,
             sizeof(command),
             "%s -s \"%s\" -d \"%s\" -e \"SELECT name FROM members WHERE id = 2147483648;\" > \"%s\" 2> \"%s\"",
             TEST_SQLENGINE_COMMAND,
             schema_dir,
             data_dir,
             out_path,
             err_path);

    exit_code = exit_code_from_system(system(command));
    stdout_text = th_read_text_file(out_path);

    if (stdout_text == NULL) {
        ok = th_fail("failed to read CLI output");
    } else if (th_string_contains(stdout_text, "max_row") ||
               th_string_contains(stdout_text, "min_row")) {
        ok = th_fail("out-of-range WHERE id matched an existing boundary row");
    } else if (exit_code == 0 &&
               !th_string_contains(stdout_text, "0 row(s) selected.")) {
        ok = th_fail("out-of-range WHERE id should not return a row");
    }

    free(stdout_text);
    th_remove_tree(workspace);
    return ok;
}

int main(void) {
    int passed = 0;
    int failed = 0;

    th_reset_reason();
    if (test_insert_int32_max_then_auto_increment_must_not_wrap()) {
        passed++;
        th_print_result("insert_int32_max_then_auto_increment_must_not_wrap", 1);
    } else {
        failed++;
        th_print_result("insert_int32_max_then_auto_increment_must_not_wrap", 0);
    }

    th_reset_reason();
    if (test_explicit_out_of_range_pk_must_fail_cleanly()) {
        passed++;
        th_print_result("explicit_out_of_range_pk_must_fail_cleanly", 1);
    } else {
        failed++;
        th_print_result("explicit_out_of_range_pk_must_fail_cleanly", 0);
    }

    th_reset_reason();
    if (test_where_id_out_of_range_must_not_match_existing_row()) {
        passed++;
        th_print_result("where_id_out_of_range_must_not_match_existing_row", 1);
    } else {
        failed++;
        th_print_result("where_id_out_of_range_must_not_match_existing_row", 0);
    }

    printf("Tests: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
