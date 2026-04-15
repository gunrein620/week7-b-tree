#include "lexer.h"
#include "parser.h"
#include "test_helpers.h"

#include <string.h>

static Statement *parse_sql(const char *sql) {
    Token *tokens;
    Statement *stmt;
    int token_count = 0;

    tokens = tokenize(sql, &token_count);
    if (tokens == NULL) {
        return NULL;
    }

    stmt = parse(tokens, token_count);
    free_tokens(tokens);
    return stmt;
}

static int test_select_star(void) {
    Statement *stmt = parse_sql("SELECT * FROM members;");

    if (stmt == NULL) {
        return th_fail("parse returned NULL");
    }
    if (stmt->type != STMT_SELECT || !stmt->select_columns.is_star ||
        strcmp(stmt->table_name, "members") != 0 || stmt->where.condition_count != 0) {
        free_statement(stmt);
        return th_fail("SELECT * statement fields were incorrect");
    }

    free_statement(stmt);
    return 1;
}

static int test_select_where_and(void) {
    Statement *stmt =
        parse_sql("SELECT id, name FROM members WHERE grade = 'vip' AND age >= 25;");

    if (stmt == NULL) {
        return th_fail("parse returned NULL");
    }
    if (stmt->select_columns.count != 2 || strcmp(stmt->select_columns.names[0], "id") != 0 ||
        strcmp(stmt->select_columns.names[1], "name") != 0 ||
        stmt->where.condition_count != 2 || strcmp(stmt->where.logical_op, "AND") != 0 ||
        strcmp(stmt->where.conditions[0].column_name, "grade") != 0 ||
        strcmp(stmt->where.conditions[0].value, "vip") != 0 ||
        strcmp(stmt->where.conditions[1].operator, ">=") != 0) {
        free_statement(stmt);
        return th_fail("WHERE clause was not parsed as expected");
    }

    free_statement(stmt);
    return 1;
}

static int test_insert_values(void) {
    Statement *stmt = parse_sql(
        "INSERT INTO members (id, name, grade, class, age) VALUES (1, 'Alice', NULL, 'basic', 20);");

    if (stmt == NULL) {
        return th_fail("parse returned NULL");
    }
    if (stmt->type != STMT_INSERT || stmt->insert_columns.count != 5 || stmt->value_count != 5 ||
        strcmp(stmt->values[1], "Alice") != 0 || stmt->value_is_null[2] != 1) {
        free_statement(stmt);
        return th_fail("INSERT values were not parsed correctly");
    }

    free_statement(stmt);
    return 1;
}

static int test_select_order_by_desc(void) {
    Statement *stmt = parse_sql("SELECT id, name FROM members ORDER BY age DESC;");

    if (stmt == NULL) {
        return th_fail("parse returned NULL");
    }
    if (!stmt->order_by.enabled || strcmp(stmt->order_by.column_name, "age") != 0 ||
        stmt->order_by.direction != SORT_DESC) {
        free_statement(stmt);
        return th_fail("ORDER BY DESC was not parsed correctly");
    }

    free_statement(stmt);
    return 1;
}

static int test_select_order_by_default_asc(void) {
    Statement *stmt =
        parse_sql("SELECT * FROM members WHERE grade = 'vip' ORDER BY name;");

    if (stmt == NULL) {
        return th_fail("parse returned NULL");
    }
    if (!stmt->order_by.enabled || strcmp(stmt->order_by.column_name, "name") != 0 ||
        stmt->order_by.direction != SORT_ASC || stmt->where.condition_count != 1) {
        free_statement(stmt);
        return th_fail("ORDER BY default ASC was not parsed correctly");
    }

    free_statement(stmt);
    return 1;
}

static int test_mixed_and_or_rejected(void) {
    Statement *stmt =
        parse_sql("SELECT * FROM members WHERE grade = 'vip' AND age >= 25 OR age < 10;");

    if (stmt != NULL) {
        free_statement(stmt);
        return th_fail("mixed AND/OR should be rejected");
    }

    return 1;
}

static int test_multiple_order_columns_rejected(void) {
    Statement *stmt = parse_sql("SELECT * FROM members ORDER BY age, name;");

    if (stmt != NULL) {
        free_statement(stmt);
        return th_fail("multiple ORDER BY columns should be rejected");
    }

    return 1;
}

static int test_missing_from_rejected(void) {
    Statement *stmt = parse_sql("SELECT * members;");

    if (stmt != NULL) {
        free_statement(stmt);
        return th_fail("missing FROM should be rejected");
    }

    return 1;
}

int main(void) {
    int passed = 0;
    int failed = 0;

    th_reset_reason();
    if (test_select_star()) {
        passed++;
        th_print_result("select_star", 1);
    } else {
        failed++;
        th_print_result("select_star", 0);
    }

    th_reset_reason();
    if (test_select_where_and()) {
        passed++;
        th_print_result("select_where_and", 1);
    } else {
        failed++;
        th_print_result("select_where_and", 0);
    }

    th_reset_reason();
    if (test_insert_values()) {
        passed++;
        th_print_result("insert_values", 1);
    } else {
        failed++;
        th_print_result("insert_values", 0);
    }

    th_reset_reason();
    if (test_select_order_by_desc()) {
        passed++;
        th_print_result("select_order_by_desc", 1);
    } else {
        failed++;
        th_print_result("select_order_by_desc", 0);
    }

    th_reset_reason();
    if (test_select_order_by_default_asc()) {
        passed++;
        th_print_result("select_order_by_default_asc", 1);
    } else {
        failed++;
        th_print_result("select_order_by_default_asc", 0);
    }

    th_reset_reason();
    if (test_mixed_and_or_rejected()) {
        passed++;
        th_print_result("mixed_and_or_rejected", 1);
    } else {
        failed++;
        th_print_result("mixed_and_or_rejected", 0);
    }

    th_reset_reason();
    if (test_multiple_order_columns_rejected()) {
        passed++;
        th_print_result("multiple_order_columns_rejected", 1);
    } else {
        failed++;
        th_print_result("multiple_order_columns_rejected", 0);
    }

    th_reset_reason();
    if (test_missing_from_rejected()) {
        passed++;
        th_print_result("missing_from_rejected", 1);
    } else {
        failed++;
        th_print_result("missing_from_rejected", 0);
    }

    printf("Tests: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
