#include "test_helpers.h"

#include <limits.h>
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

static int test_cli_executes_e_option(void) {
    char workspace[PATH_MAX];
    char schema_dir[PATH_MAX];
    char data_dir[PATH_MAX];
    char out_path[PATH_MAX];
    char err_path[PATH_MAX];
    char command[4096];
    char *stdout_text;
    int exit_code;

    if (!th_setup_workspace("cli_e", workspace, sizeof(workspace))) {
        return th_fail("failed to create temporary workspace");
    }

    th_join_path(schema_dir, sizeof(schema_dir), workspace, "schemas");
    th_join_path(data_dir, sizeof(data_dir), workspace, "data");
    th_join_path(out_path, sizeof(out_path), workspace, "stdout.txt");
    th_join_path(err_path, sizeof(err_path), workspace, "stderr.txt");

    if (!th_write_members_schema(schema_dir)) {
        th_remove_tree(workspace);
        return th_fail("failed to write members schema");
    }

    snprintf(command,
             sizeof(command),
             "%s -s \"%s\" -d \"%s\" -e \"INSERT INTO members (id, name, grade, class, age) "
             "VALUES (1, 'Alice', 'vip', 'advanced', 30);\" > \"%s\" 2> \"%s\"",
             TEST_SQLENGINE_COMMAND,
             schema_dir,
             data_dir,
             out_path,
             err_path);

    exit_code = exit_code_from_system(system(command));
    stdout_text = th_read_text_file(out_path);
    th_remove_tree(workspace);

    if (exit_code != 0) {
        free(stdout_text);
        return th_fail("CLI -e should exit with 0");
    }
    if (!th_string_contains(stdout_text, "1 row inserted.")) {
        free(stdout_text);
        return th_fail("CLI -e output did not contain insert message");
    }

    free(stdout_text);
    return 1;
}

static int test_cli_executes_file_script(void) {
    char workspace[PATH_MAX];
    char schema_dir[PATH_MAX];
    char data_dir[PATH_MAX];
    char sql_dir[PATH_MAX];
    char sql_path[PATH_MAX];
    char out_path[PATH_MAX];
    char err_path[PATH_MAX];
    char command[4096];
    char *stdout_text;
    int exit_code;

    if (!th_setup_workspace("cli_f", workspace, sizeof(workspace))) {
        return th_fail("failed to create temporary workspace");
    }

    th_join_path(schema_dir, sizeof(schema_dir), workspace, "schemas");
    th_join_path(data_dir, sizeof(data_dir), workspace, "data");
    th_join_path(sql_dir, sizeof(sql_dir), workspace, "sql");
    th_join_path(sql_path, sizeof(sql_path), sql_dir, "queries.sql");
    th_join_path(out_path, sizeof(out_path), workspace, "stdout.txt");
    th_join_path(err_path, sizeof(err_path), workspace, "stderr.txt");

    if (!th_write_members_schema(schema_dir)) {
        th_remove_tree(workspace);
        return th_fail("failed to write members schema");
    }
    if (!th_write_text_file(sql_path,
                            "INSERT INTO members (id, name, grade, class, age) VALUES "
                            "(1, 'Alice', 'vip', 'advanced', 30);\n"
                            "INSERT INTO members (id, name, grade, class, age) VALUES "
                            "(2, 'Bob', 'normal', 'basic', 22);\n"
                            "SELECT id, name FROM members WHERE age >= 20;\n")) {
        th_remove_tree(workspace);
        return th_fail("failed to write SQL script");
    }

    snprintf(command,
             sizeof(command),
             "%s -s \"%s\" -d \"%s\" -f \"%s\" > \"%s\" 2> \"%s\"",
             TEST_SQLENGINE_COMMAND,
             schema_dir,
             data_dir,
             sql_path,
             out_path,
             err_path);

    exit_code = exit_code_from_system(system(command));
    stdout_text = th_read_text_file(out_path);
    th_remove_tree(workspace);

    if (exit_code != 0) {
        free(stdout_text);
        return th_fail("CLI -f should exit with 0");
    }
    if (!th_string_contains(stdout_text, "2 row(s) selected.") ||
        !th_string_contains(stdout_text, "Alice") || !th_string_contains(stdout_text, "Bob")) {
        free(stdout_text);
        return th_fail("CLI -f output did not contain expected SELECT results");
    }

    free(stdout_text);
    return 1;
}

static int test_cli_parse_error_exit_code(void) {
    char workspace[PATH_MAX];
    char schema_dir[PATH_MAX];
    char data_dir[PATH_MAX];
    char out_path[PATH_MAX];
    char err_path[PATH_MAX];
    char command[4096];
    int exit_code;

    if (!th_setup_workspace("cli_error", workspace, sizeof(workspace))) {
        return th_fail("failed to create temporary workspace");
    }

    th_join_path(schema_dir, sizeof(schema_dir), workspace, "schemas");
    th_join_path(data_dir, sizeof(data_dir), workspace, "data");
    th_join_path(out_path, sizeof(out_path), workspace, "stdout.txt");
    th_join_path(err_path, sizeof(err_path), workspace, "stderr.txt");

    if (!th_write_members_schema(schema_dir)) {
        th_remove_tree(workspace);
        return th_fail("failed to write members schema");
    }

    snprintf(command,
             sizeof(command),
             "%s -s \"%s\" -d \"%s\" -e \"SELEC * FROM members;\" > \"%s\" 2> \"%s\"",
             TEST_SQLENGINE_COMMAND,
             schema_dir,
             data_dir,
             out_path,
             err_path);

    exit_code = exit_code_from_system(system(command));
    th_remove_tree(workspace);

    if (exit_code != 1) {
        return th_fail("parse error should exit with code 1");
    }

    return 1;
}

static int test_cli_order_by_desc_output(void) {
    char workspace[PATH_MAX];
    char schema_dir[PATH_MAX];
    char data_dir[PATH_MAX];
    char sql_dir[PATH_MAX];
    char sql_path[PATH_MAX];
    char out_path[PATH_MAX];
    char err_path[PATH_MAX];
    char command[4096];
    char *stdout_text;
    char *alice_ptr;
    char *bob_ptr;
    char *cara_ptr;
    int exit_code;

    if (!th_setup_workspace("cli_order", workspace, sizeof(workspace))) {
        return th_fail("failed to create temporary workspace");
    }

    th_join_path(schema_dir, sizeof(schema_dir), workspace, "schemas");
    th_join_path(data_dir, sizeof(data_dir), workspace, "data");
    th_join_path(sql_dir, sizeof(sql_dir), workspace, "sql");
    th_join_path(sql_path, sizeof(sql_path), sql_dir, "queries.sql");
    th_join_path(out_path, sizeof(out_path), workspace, "stdout.txt");
    th_join_path(err_path, sizeof(err_path), workspace, "stderr.txt");

    if (!th_write_members_schema(schema_dir)) {
        th_remove_tree(workspace);
        return th_fail("failed to write members schema");
    }
    if (!th_write_text_file(sql_path,
                            "INSERT INTO members (id, name, grade, class, age) VALUES "
                            "(1, 'Alice', 'vip', 'advanced', 30);\n"
                            "INSERT INTO members (id, name, grade, class, age) VALUES "
                            "(2, 'Bob', 'normal', 'basic', 22);\n"
                            "INSERT INTO members (id, name, grade, class, age) VALUES "
                            "(3, 'Cara', 'vip', 'middle', 41);\n"
                            "SELECT name FROM members ORDER BY age DESC;\n")) {
        th_remove_tree(workspace);
        return th_fail("failed to write SQL script");
    }

    snprintf(command,
             sizeof(command),
             "%s -s \"%s\" -d \"%s\" -f \"%s\" > \"%s\" 2> \"%s\"",
             TEST_SQLENGINE_COMMAND,
             schema_dir,
             data_dir,
             sql_path,
             out_path,
             err_path);

    exit_code = exit_code_from_system(system(command));
    stdout_text = th_read_text_file(out_path);
    th_remove_tree(workspace);

    if (exit_code != 0) {
        free(stdout_text);
        return th_fail("CLI ORDER BY script should exit with 0");
    }
    if (stdout_text == NULL) {
        return th_fail("failed to read ORDER BY output");
    }

    alice_ptr = strstr(stdout_text, "Alice");
    bob_ptr = strstr(stdout_text, "Bob");
    cara_ptr = strstr(stdout_text, "Cara");

    if (cara_ptr == NULL || alice_ptr == NULL || bob_ptr == NULL ||
        !(cara_ptr < alice_ptr && alice_ptr < bob_ptr)) {
        free(stdout_text);
        return th_fail("ORDER BY DESC output order was incorrect");
    }

    free(stdout_text);
    return 1;
}

int main(void) {
    int passed = 0;
    int failed = 0;

    th_reset_reason();
    if (test_cli_executes_e_option()) {
        passed++;
        th_print_result("cli_executes_e_option", 1);
    } else {
        failed++;
        th_print_result("cli_executes_e_option", 0);
    }

    th_reset_reason();
    if (test_cli_executes_file_script()) {
        passed++;
        th_print_result("cli_executes_file_script", 1);
    } else {
        failed++;
        th_print_result("cli_executes_file_script", 0);
    }

    th_reset_reason();
    if (test_cli_parse_error_exit_code()) {
        passed++;
        th_print_result("cli_parse_error_exit_code", 1);
    } else {
        failed++;
        th_print_result("cli_parse_error_exit_code", 0);
    }

    th_reset_reason();
    if (test_cli_order_by_desc_output()) {
        passed++;
        th_print_result("cli_order_by_desc_output", 1);
    } else {
        failed++;
        th_print_result("cli_order_by_desc_output", 0);
    }

    printf("Tests: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
