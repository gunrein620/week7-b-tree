#include "config.h"
#include "schema.h"
#include "test_helpers.h"

#include <limits.h>
#include <string.h>

static int test_load_members_schema(void) {
    char workspace[PATH_MAX];
    char schema_dir[PATH_MAX];
    Schema *schema;

    if (!th_setup_workspace("schema_load", workspace, sizeof(workspace))) {
        return th_fail("failed to create temporary workspace");
    }

    th_join_path(schema_dir, sizeof(schema_dir), workspace, "schemas");
    if (!th_write_members_schema(schema_dir)) {
        th_remove_tree(workspace);
        return th_fail("failed to write members schema");
    }

    config_set_schema_dir(schema_dir);
    schema = schema_load("members");
    if (schema == NULL) {
        th_remove_tree(workspace);
        return th_fail("schema_load returned NULL");
    }

    if (schema->column_count != 5 || schema->columns[1].max_length != 32 ||
        schema->columns[2].nullable != 1 || schema->columns[0].is_primary_key != 1) {
        schema_free(schema);
        th_remove_tree(workspace);
        return th_fail("loaded schema contents were incorrect");
    }

    schema_free(schema);
    th_remove_tree(workspace);
    return 1;
}

static int test_missing_schema_returns_null(void) {
    char workspace[PATH_MAX];
    char schema_dir[PATH_MAX];
    Schema *schema;

    if (!th_setup_workspace("schema_missing", workspace, sizeof(workspace))) {
        return th_fail("failed to create temporary workspace");
    }

    th_join_path(schema_dir, sizeof(schema_dir), workspace, "schemas");
    config_set_schema_dir(schema_dir);
    schema = schema_load("missing");

    th_remove_tree(workspace);

    if (schema != NULL) {
        schema_free(schema);
        return th_fail("missing schema should return NULL");
    }

    return 1;
}

static int test_column_index_lookup(void) {
    char workspace[PATH_MAX];
    char schema_dir[PATH_MAX];
    Schema *schema;

    if (!th_setup_workspace("schema_index", workspace, sizeof(workspace))) {
        return th_fail("failed to create temporary workspace");
    }

    th_join_path(schema_dir, sizeof(schema_dir), workspace, "schemas");
    if (!th_write_members_schema(schema_dir)) {
        th_remove_tree(workspace);
        return th_fail("failed to write members schema");
    }

    config_set_schema_dir(schema_dir);
    schema = schema_load("members");
    if (schema == NULL) {
        th_remove_tree(workspace);
        return th_fail("schema_load returned NULL");
    }

    if (schema_get_column_index(schema, "name") != 1 ||
        schema_get_column_index(schema, "grade") != 2 ||
        schema_get_column_index(schema, "unknown") != -1) {
        schema_free(schema);
        th_remove_tree(workspace);
        return th_fail("column index lookup failed");
    }

    schema_free(schema);
    th_remove_tree(workspace);
    return 1;
}

int main(void) {
    int passed = 0;
    int failed = 0;

    th_reset_reason();
    if (test_load_members_schema()) {
        passed++;
        th_print_result("load_members_schema", 1);
    } else {
        failed++;
        th_print_result("load_members_schema", 0);
    }

    th_reset_reason();
    if (test_missing_schema_returns_null()) {
        passed++;
        th_print_result("missing_schema_returns_null", 1);
    } else {
        failed++;
        th_print_result("missing_schema_returns_null", 0);
    }

    th_reset_reason();
    if (test_column_index_lookup()) {
        passed++;
        th_print_result("column_index_lookup", 1);
    } else {
        failed++;
        th_print_result("column_index_lookup", 0);
    }

    printf("Tests: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
