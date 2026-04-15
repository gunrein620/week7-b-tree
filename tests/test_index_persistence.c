#include "config.h"
#include "index.h"
#include "schema.h"
#include "storage.h"
#include "test_helpers.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int setup_members_workspace(char *workspace,
                                   size_t workspace_size,
                                   char *schema_dir,
                                   size_t schema_dir_size,
                                   char *data_dir,
                                   size_t data_dir_size) {
    if (!th_setup_workspace("index_persist", workspace, workspace_size)) {
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

static int test_index_lookup_creates_persisted_file(void) {
    char workspace[PATH_MAX];
    char schema_dir[PATH_MAX];
    char data_dir[PATH_MAX];
    char table_path[PATH_MAX];
    char index_path[PATH_MAX];
    Schema *schema = NULL;
    Row row;
    int64_t offset = -1;
    int ok = 1;

    index_drop_all();
    if (!setup_members_workspace(workspace,
                                 sizeof(workspace),
                                 schema_dir,
                                 sizeof(schema_dir),
                                 data_dir,
                                 sizeof(data_dir))) {
        return th_fail("failed to prepare members workspace");
    }

    th_join_path(table_path, sizeof(table_path), data_dir, "members.tbl");
    th_join_path(index_path, sizeof(index_path), data_dir, "members.idx");
    if (!th_write_text_file(table_path,
                            "id|name|grade|class|age\n"
                            "1|alice|vip|advanced|30\n"
                            "2|bob|gold|basic|25\n")) {
        th_remove_tree(workspace);
        return th_fail("failed to seed members.tbl");
    }

    schema = schema_load("members");
    if (schema == NULL) {
        ok = th_fail("schema_load failed");
    }
    if (ok && th_file_exists(index_path)) {
        ok = th_fail("members.idx should not exist before indexed lookup");
    }
    if (ok && index_lookup_offset("members", schema, 2, &offset) != 1) {
        ok = th_fail("index lookup should build persisted idx");
    }
    if (ok && !th_file_exists(index_path)) {
        ok = th_fail("members.idx should be created after indexed lookup");
    }
    if (ok && !storage_read_row_at("members", schema, offset, &row)) {
        ok = th_fail("storage_read_row_at failed after first lookup");
    }
    if (ok && strcmp(row.data[1], "bob") != 0) {
        ok = th_fail("first lookup returned wrong row");
    }

    if (ok) {
        index_drop_all();
        offset = -1;
        if (index_lookup_offset("members", schema, 2, &offset) != 1) {
            ok = th_fail("persisted idx should answer lookup after cache drop");
        }
    }

    if (schema != NULL) {
        schema_free(schema);
    }
    index_drop_all();
    th_remove_tree(workspace);
    return ok;
}

static int test_corrupt_persisted_index_must_rebuild_from_table(void) {
    char workspace[PATH_MAX];
    char schema_dir[PATH_MAX];
    char data_dir[PATH_MAX];
    char table_path[PATH_MAX];
    char index_path[PATH_MAX];
    Schema *schema = NULL;
    Row row;
    int64_t offset = -1;
    int ok = 1;

    index_drop_all();
    if (!setup_members_workspace(workspace,
                                 sizeof(workspace),
                                 schema_dir,
                                 sizeof(schema_dir),
                                 data_dir,
                                 sizeof(data_dir))) {
        return th_fail("failed to prepare members workspace");
    }

    th_join_path(table_path, sizeof(table_path), data_dir, "members.tbl");
    th_join_path(index_path, sizeof(index_path), data_dir, "members.idx");
    if (!th_write_text_file(table_path,
                            "id|name|grade|class|age\n"
                            "1|alice|vip|advanced|30\n"
                            "2|bob|gold|basic|25\n")) {
        th_remove_tree(workspace);
        return th_fail("failed to seed members.tbl");
    }

    schema = schema_load("members");
    if (schema == NULL) {
        ok = th_fail("schema_load failed");
    }
    if (ok && index_lookup_offset("members", schema, 2, &offset) != 1) {
        ok = th_fail("initial lookup should build idx");
    }
    if (ok && !th_write_text_file(index_path, "broken-index")) {
        ok = th_fail("failed to corrupt persisted idx");
    }

    if (ok) {
        index_drop_all();
        offset = -1;
        if (index_lookup_offset("members", schema, 2, &offset) != 1) {
            ok = th_fail("corrupt idx should rebuild from table");
        }
    }
    if (ok && !storage_read_row_at("members", schema, offset, &row)) {
        ok = th_fail("storage_read_row_at failed after rebuild");
    }
    if (ok && strcmp(row.data[1], "bob") != 0) {
        ok = th_fail("rebuild returned wrong row");
    }

    if (schema != NULL) {
        schema_free(schema);
    }
    index_drop_all();
    th_remove_tree(workspace);
    return ok;
}

static int test_stale_in_memory_index_after_table_mutation_must_be_detected(void) {
    char workspace[PATH_MAX];
    char schema_dir[PATH_MAX];
    char data_dir[PATH_MAX];
    char table_path[PATH_MAX];
    Schema *schema = NULL;
    Row row;
    int64_t offset = -1;
    int ok = 1;
    FILE *file;

    index_drop_all();
    if (!setup_members_workspace(workspace,
                                 sizeof(workspace),
                                 schema_dir,
                                 sizeof(schema_dir),
                                 data_dir,
                                 sizeof(data_dir))) {
        return th_fail("failed to prepare members workspace");
    }

    th_join_path(table_path, sizeof(table_path), data_dir, "members.tbl");
    if (!th_write_text_file(table_path,
                            "id|name|grade|class|age\n"
                            "1|alice|vip|advanced|30\n")) {
        th_remove_tree(workspace);
        return th_fail("failed to seed members.tbl");
    }

    schema = schema_load("members");
    if (schema == NULL) {
        ok = th_fail("schema_load failed");
    }
    if (ok && index_get_or_build("members", schema) == NULL) {
        ok = th_fail("failed to cache initial index");
    }

    if (ok) {
        file = fopen(table_path, "ab");
        if (file == NULL) {
            ok = th_fail("failed to append externally mutated row");
        } else {
            fputs("2|bob|gold|basic|25\n", file);
            fclose(file);
        }
    }

    if (ok && index_lookup_offset("members", schema, 2, &offset) != 1) {
        ok = th_fail("stale in-memory index missed externally appended row");
    }
    if (ok && !storage_read_row_at("members", schema, offset, &row)) {
        ok = th_fail("failed to read row after stale-cache rebuild");
    }
    if (ok && strcmp(row.data[1], "bob") != 0) {
        ok = th_fail("stale-cache rebuild returned wrong row");
    }

    if (ok) {
        index_drop_all();
        offset = -1;
        if (index_lookup_offset("members", schema, 2, &offset) != 1) {
            ok = th_fail("rewritten idx should answer lookup after cache drop");
        }
    }

    if (schema != NULL) {
        schema_free(schema);
    }
    index_drop_all();
    th_remove_tree(workspace);
    return ok;
}

int main(void) {
    int passed = 0;
    int failed = 0;

    th_reset_reason();
    if (test_index_lookup_creates_persisted_file()) {
        passed++;
        th_print_result("index_lookup_creates_persisted_file", 1);
    } else {
        failed++;
        th_print_result("index_lookup_creates_persisted_file", 0);
    }

    th_reset_reason();
    if (test_corrupt_persisted_index_must_rebuild_from_table()) {
        passed++;
        th_print_result("corrupt_persisted_index_must_rebuild_from_table", 1);
    } else {
        failed++;
        th_print_result("corrupt_persisted_index_must_rebuild_from_table", 0);
    }

    th_reset_reason();
    if (test_stale_in_memory_index_after_table_mutation_must_be_detected()) {
        passed++;
        th_print_result("stale_in_memory_index_after_table_mutation_must_be_detected", 1);
    } else {
        failed++;
        th_print_result("stale_in_memory_index_after_table_mutation_must_be_detected", 0);
    }

    printf("Tests: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
