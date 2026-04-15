#include "btree.h"
#include "config.h"
#include "executor.h"
#include "index.h"
#include "lexer.h"
#include "parser.h"
#include "schema.h"
#include "storage.h"

#include "test_helpers.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 주어진 SQL 을 lexer → parser → executor 로 한 번에 돌린다. */
static int run_sql(const char *sql) {
    Token *tokens;
    Statement *stmt;
    int token_count = 0;
    int rc;

    tokens = tokenize(sql, &token_count);
    if (tokens == NULL) return -1;
    stmt = parse(tokens, token_count);
    if (stmt == NULL) { free_tokens(tokens); return -1; }
    rc = execute(stmt);
    free_statement(stmt);
    free_tokens(tokens);
    return rc;
}

static int setup_members_workspace(char *workspace, size_t size) {
    char schema_dir[PATH_MAX];
    char data_dir[PATH_MAX];

    if (!th_setup_workspace("index_it", workspace, size)) return 0;
    th_join_path(schema_dir, sizeof(schema_dir), workspace, "schemas");
    th_join_path(data_dir, sizeof(data_dir), workspace, "data");
    if (!th_write_members_schema(schema_dir)) return 0;
    config_set_schema_dir(schema_dir);
    config_set_data_dir(data_dir);
    return 1;
}

/* auto-increment: id 를 지정하지 않은 INSERT 가 1, 2, 3 ... 순으로 할당되는지. */
static int test_auto_increment(void) {
    char workspace[PATH_MAX];
    Schema *schema;
    BTree *tree;
    int ok = 1;

    index_drop_all();
    if (!setup_members_workspace(workspace, sizeof(workspace))) {
        return th_fail("workspace");
    }

    if (run_sql("INSERT INTO members (name, age) VALUES ('alice', 20);") != 0) ok = th_fail("insert1");
    if (ok && run_sql("INSERT INTO members (name, age) VALUES ('bob',   21);") != 0) ok = th_fail("insert2");
    if (ok && run_sql("INSERT INTO members (name, age) VALUES ('carol', 22);") != 0) ok = th_fail("insert3");

    schema = schema_load("members");
    if (schema == NULL) ok = th_fail("schema_load");
    if (ok) {
        tree = index_get_or_build("members", schema);
        if (tree == NULL) ok = th_fail("index build");
        if (ok && btree_size(tree) != 3) ok = th_fail("index size after 3 inserts");
        if (ok && btree_max_key(tree) != 3) ok = th_fail("auto-increment last id should be 3");
    }

    if (schema) schema_free(schema);
    index_drop_all();
    th_remove_tree(workspace);
    th_print_result("auto_increment", ok);
    return ok;
}

/* 사용자가 id 를 명시적으로 넣어도 B+ 트리가 해당 id 를 인덱싱하고
 * 중복 PK 는 인덱스로 O(log n) 에 거부되는지. */
static int test_explicit_id_and_duplicate(void) {
    char workspace[PATH_MAX];
    int ok = 1;

    index_drop_all();
    if (!setup_members_workspace(workspace, sizeof(workspace))) {
        return th_fail("workspace");
    }

    if (run_sql("INSERT INTO members (id, name, age) VALUES (100, 'first', 30);") != 0) {
        ok = th_fail("explicit id insert");
    }
    /* 중복 PK 는 거부되어야 한다. execute 는 -1 을 반환. */
    if (ok && run_sql("INSERT INTO members (id, name, age) VALUES (100, 'dup', 31);") == 0) {
        ok = th_fail("duplicate PK was accepted");
    }
    /* 자동증가는 (max+1) = 101 이어야 한다. */
    if (ok && run_sql("INSERT INTO members (name, age) VALUES ('next', 32);") != 0) {
        ok = th_fail("next auto insert");
    }
    if (ok) {
        Schema *schema = schema_load("members");
        BTree *tree = index_get_or_build("members", schema);
        if (btree_size(tree) != 2) ok = th_fail("expected 2 rows after dup reject");
        if (btree_max_key(tree) != 101) ok = th_fail("auto id should be 101 after explicit 100");
        schema_free(schema);
    }

    index_drop_all();
    th_remove_tree(workspace);
    th_print_result("explicit_id_and_duplicate", ok);
    return ok;
}

/* IndexManager 가 .tbl 만 있는 상태에서 lazy build 로 정확히 구축되는지,
 * 그리고 storage_read_row_at 가 올바른 오프셋에서 행을 되살리는지. */
static int test_lazy_build_and_read(void) {
    char workspace[PATH_MAX];
    char tbl_path[PATH_MAX];
    Schema *schema = NULL;
    BTree *tree;
    Row row;
    int ok = 1;
    int64_t offset = -1;

    index_drop_all();
    if (!setup_members_workspace(workspace, sizeof(workspace))) {
        return th_fail("workspace");
    }

    /* .tbl 을 직접 기록: 3 개 행. */
    th_join_path(tbl_path, sizeof(tbl_path),
                 workspace, "data/members.tbl");
    if (!th_write_text_file(tbl_path,
                            "id|name|grade|class|age\n"
                            "1|alice|vip|advanced|30\n"
                            "2|bob|gold|basic|25\n"
                            "3|carol|normal|beginner|22\n")) {
        ok = th_fail("write tbl");
    }

    if (ok) {
        schema = schema_load("members");
        if (schema == NULL) ok = th_fail("schema_load");
    }
    if (ok) {
        tree = index_get_or_build("members", schema);
        if (tree == NULL) ok = th_fail("lazy build");
        if (ok && btree_size(tree) != 3) ok = th_fail("lazy size");
        if (ok && btree_find(tree, 2, &offset) != 1) ok = th_fail("lazy find 2");
        if (ok && storage_read_row_at("members", schema, offset, &row) != 1) {
            ok = th_fail("read_row_at");
        }
        if (ok && strcmp(row.data[1], "bob") != 0) ok = th_fail("wrong row content");
    }

    if (schema) schema_free(schema);
    index_drop_all();
    th_remove_tree(workspace);
    th_print_result("lazy_build_and_read", ok);
    return ok;
}

/* execute_select 의 인덱스 경로 vs 선형 경로가 동일한 결과를 주는지. */
static int test_index_select_matches_linear(void) {
    char workspace[PATH_MAX];
    Schema *schema = NULL;
    ColumnList star;
    WhereClause where_idx;
    WhereClause where_lin;
    ResultSet *r_idx = NULL;
    ResultSet *r_lin = NULL;
    int ok = 1;
    int i;

    index_drop_all();
    if (!setup_members_workspace(workspace, sizeof(workspace))) {
        return th_fail("workspace");
    }
    for (i = 1; i <= 50; ++i) {
        char sql[256];
        snprintf(sql, sizeof(sql),
                 "INSERT INTO members (name, age) VALUES ('m%d', %d);",
                 i, 20 + i);
        if (run_sql(sql) != 0) { ok = th_fail("insert loop"); break; }
    }

    schema = schema_load("members");
    if (schema == NULL) ok = th_fail("schema_load");

    if (ok) {
        memset(&star, 0, sizeof(star));
        star.is_star = 1;

        /* 인덱스 경로 시뮬레이션: btree_find + read_row_at */
        {
            BTree *tree = index_get_or_build("members", schema);
            int64_t off;
            Row row;
            if (btree_find(tree, 25, &off) != 1) ok = th_fail("btree_find 25");
            if (ok && storage_read_row_at("members", schema, off, &row) != 1) {
                ok = th_fail("read_row_at 25");
            }
            if (ok && strcmp(row.data[1], "m25") != 0) ok = th_fail("idx name");
        }

        /* 선형 경로: storage_select with WHERE name = 'm25' */
        memset(&where_lin, 0, sizeof(where_lin));
        where_lin.condition_count = 1;
        strncpy(where_lin.conditions[0].column_name, "name",
                sizeof(where_lin.conditions[0].column_name) - 1);
        strncpy(where_lin.conditions[0].operator, "=",
                sizeof(where_lin.conditions[0].operator) - 1);
        strncpy(where_lin.conditions[0].value, "m25",
                sizeof(where_lin.conditions[0].value) - 1);
        r_lin = storage_select("members", schema, &star, &where_lin);
        if (r_lin == NULL || r_lin->row_count != 1) ok = th_fail("linear select count");
        if (ok && atoi(r_lin->rows[0].data[0]) != 25) ok = th_fail("linear id mismatch");

        /* 인덱스 경로: storage_select with WHERE id = 25 → executor 가 내부에서
         * 인덱스 경로를 쓰는지 확인하기 위해 execute_select 는 결과를 stdout 으로
         * 찍는다. 여기서는 storage_select 가 같은 답을 주는지 비교.
         * (PK 경로는 executor 레벨에서만 최적화되므로 storage_select 는 여전히
         *  풀 스캔이지만, 결과의 정합성을 비교하는 용도로는 충분하다.) */
        memset(&where_idx, 0, sizeof(where_idx));
        where_idx.condition_count = 1;
        strncpy(where_idx.conditions[0].column_name, "id",
                sizeof(where_idx.conditions[0].column_name) - 1);
        strncpy(where_idx.conditions[0].operator, "=",
                sizeof(where_idx.conditions[0].operator) - 1);
        strncpy(where_idx.conditions[0].value, "25",
                sizeof(where_idx.conditions[0].value) - 1);
        r_idx = storage_select("members", schema, &star, &where_idx);
        if (r_idx == NULL || r_idx->row_count != 1) ok = th_fail("id select count");
        if (ok && strcmp(r_idx->rows[0].data[1], "m25") != 0) ok = th_fail("id name mismatch");
    }

    if (r_idx) free_result_set(r_idx);
    if (r_lin) free_result_set(r_lin);
    if (schema) schema_free(schema);
    index_drop_all();
    th_remove_tree(workspace);
    th_print_result("index_select_matches_linear", ok);
    return ok;
}

int main(void) {
    int passed = 0;
    int failed = 0;

    th_reset_reason(); if (test_auto_increment()) passed++; else failed++;
    th_reset_reason(); if (test_explicit_id_and_duplicate()) passed++; else failed++;
    th_reset_reason(); if (test_lazy_build_and_read()) passed++; else failed++;
    th_reset_reason(); if (test_index_select_matches_linear()) passed++; else failed++;

    printf("Tests: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
