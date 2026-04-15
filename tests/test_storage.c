#include "config.h"
#include "schema.h"
#include "storage.h"
#include "test_helpers.h"

#include <limits.h>
#include <string.h>

static void fill_member_row(Row *row,
                            const char *id,
                            const char *name,
                            const char *grade,
                            const char *class_name,
                            const char *age) {
    memset(row, 0, sizeof(*row));
    row->column_count = 5;
    strncpy(row->data[0], id, MAX_TOKEN_LEN - 1);
    strncpy(row->data[1], name, MAX_TOKEN_LEN - 1);
    strncpy(row->data[2], grade, MAX_TOKEN_LEN - 1);
    strncpy(row->data[3], class_name, MAX_TOKEN_LEN - 1);
    strncpy(row->data[4], age, MAX_TOKEN_LEN - 1);
}

static int test_insert_creates_header(void) {
    char workspace[PATH_MAX];
    char schema_dir[PATH_MAX];
    char data_dir[PATH_MAX];
    char table_path[PATH_MAX];
    char *contents;
    Schema *schema;
    Row row;

    if (!th_setup_workspace("storage_create", workspace, sizeof(workspace))) {
        return th_fail("failed to create temporary workspace");
    }

    th_join_path(schema_dir, sizeof(schema_dir), workspace, "schemas");
    th_join_path(data_dir, sizeof(data_dir), workspace, "data");
    th_join_path(table_path, sizeof(table_path), data_dir, "members.tbl");

    if (!th_write_members_schema(schema_dir)) {
        th_remove_tree(workspace);
        return th_fail("failed to write members schema");
    }

    config_set_schema_dir(schema_dir);
    config_set_data_dir(data_dir);

    schema = schema_load("members");
    if (schema == NULL) {
        th_remove_tree(workspace);
        return th_fail("schema_load returned NULL");
    }

    fill_member_row(&row, "1", "Alice", "vip", "advanced", "30");
    if (storage_insert("members", &row, schema) != 0) {
        schema_free(schema);
        th_remove_tree(workspace);
        return th_fail("storage_insert failed");
    }

    contents = th_read_text_file(table_path);
    schema_free(schema);
    th_remove_tree(workspace);

    if (contents == NULL) {
        return th_fail("failed to read table file");
    }

    if (!th_string_contains(contents, "id|name|grade|class|age\n") ||
        !th_string_contains(contents, "1|Alice|vip|advanced|30\n")) {
        free(contents);
        return th_fail("table file contents were incorrect");
    }

    free(contents);
    return 1;
}

static int test_select_filters_rows(void) {
    char workspace[PATH_MAX];
    char schema_dir[PATH_MAX];
    char data_dir[PATH_MAX];
    Schema *schema;
    Row row;
    ColumnList columns;
    WhereClause where;
    ResultSet *result;

    if (!th_setup_workspace("storage_select", workspace, sizeof(workspace))) {
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

    schema = schema_load("members");
    if (schema == NULL) {
        th_remove_tree(workspace);
        return th_fail("schema_load returned NULL");
    }

    fill_member_row(&row, "1", "Alice", "vip", "advanced", "30");
    if (storage_insert("members", &row, schema) != 0) {
        schema_free(schema);
        th_remove_tree(workspace);
        return th_fail("first insert failed");
    }

    fill_member_row(&row, "2", "Bob", "normal", "basic", "22");
    if (storage_insert("members", &row, schema) != 0) {
        schema_free(schema);
        th_remove_tree(workspace);
        return th_fail("second insert failed");
    }

    memset(&columns, 0, sizeof(columns));
    columns.count = 2;
    strncpy(columns.names[0], "id", sizeof(columns.names[0]) - 1);
    strncpy(columns.names[1], "name", sizeof(columns.names[1]) - 1);

    memset(&where, 0, sizeof(where));
    where.condition_count = 1;
    strncpy(where.conditions[0].column_name,
            "age",
            sizeof(where.conditions[0].column_name) - 1);
    strncpy(where.conditions[0].operator, ">=", sizeof(where.conditions[0].operator) - 1);
    strncpy(where.conditions[0].value, "25", sizeof(where.conditions[0].value) - 1);

    result = storage_select("members", schema, &columns, &where);
    if (result == NULL) {
        schema_free(schema);
        th_remove_tree(workspace);
        return th_fail("storage_select returned NULL");
    }

    if (result->row_count != 1 || result->selected_count != 2 ||
        strcmp(result->rows[0].data[0], "1") != 0 ||
        strcmp(result->rows[0].data[1], "Alice") != 0) {
        free_result_set(result);
        schema_free(schema);
        th_remove_tree(workspace);
        return th_fail("filtered SELECT returned unexpected rows");
    }

    free_result_set(result);
    schema_free(schema);
    th_remove_tree(workspace);
    return 1;
}

int main(void) {
    int passed = 0;
    int failed = 0;

    th_reset_reason();
    if (test_insert_creates_header()) {
        passed++;
        th_print_result("insert_creates_header", 1);
    } else {
        failed++;
        th_print_result("insert_creates_header", 0);
    }

    th_reset_reason();
    if (test_select_filters_rows()) {
        passed++;
        th_print_result("select_filters_rows", 1);
    } else {
        failed++;
        th_print_result("select_filters_rows", 0);
    }

    printf("Tests: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
