#include "executor.h"

#include "btree.h"
#include "config.h"
#include "index.h"
#include "schema.h"
#include "storage.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Schema *g_sort_schema = NULL;
static int g_sort_column_index = -1;
static SortDirection g_sort_direction = SORT_ASC;
static int g_executor_output_enabled = 1;

int executor_set_output_enabled(int enabled) {
    int previous = g_executor_output_enabled;

    g_executor_output_enabled = enabled ? 1 : 0;
    return previous;
}

static int is_valid_int(const char *text) {
    char *end_ptr;
    long value;

    if (text == NULL || text[0] == '\0') {
        return 0;
    }

    errno = 0;
    value = strtol(text, &end_ptr, 10);
    return errno == 0 && *end_ptr == '\0' && value >= INT32_MIN && value <= INT32_MAX;
}

static int is_valid_float(const char *text) {
    char *end_ptr;
    double value;

    if (text == NULL || text[0] == '\0') {
        return 0;
    }

    errno = 0;
    value = strtod(text, &end_ptr);
    return errno == 0 && *end_ptr == '\0' &&
           value == value &&
           value >= -DBL_MAX &&
           value <= DBL_MAX;
}

static int is_leap_year(int year) {
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

static int is_valid_date(const char *text) {
    static const int DAYS_IN_MONTH[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int index;
    int year;
    int month;
    int day;
    int max_day;

    if (text == NULL || strlen(text) != 10) {
        return 0;
    }

    for (index = 0; index < 10; ++index) {
        if (index == 4 || index == 7) {
            if (text[index] != '-') {
                return 0;
            }
        } else if (!isdigit((unsigned char)text[index])) {
            return 0;
        }
    }

    year = (text[0] - '0') * 1000 + (text[1] - '0') * 100 + (text[2] - '0') * 10 + (text[3] - '0');
    month = (text[5] - '0') * 10 + (text[6] - '0');
    day = (text[8] - '0') * 10 + (text[9] - '0');

    if (month < 1 || month > 12) {
        return 0;
    }

    max_day = DAYS_IN_MONTH[month - 1];
    if (month == 2 && is_leap_year(year)) {
        max_day = 29;
    }

    return day >= 1 && day <= max_day;
}

static int contains_forbidden_storage_char(const char *text) {
    return strchr(text, '|') != NULL || strchr(text, '\n') != NULL || strchr(text, '\r') != NULL;
}

static int compare_values_for_sort(ColumnType type, const char *left, const char *right) {
    long left_int;
    long right_int;
    double left_float;
    double right_float;

    if (left[0] == '\0' && right[0] == '\0') {
        return 0;
    }
    if (left[0] == '\0') {
        return -1;
    }
    if (right[0] == '\0') {
        return 1;
    }

    if (type == COL_INT) {
        if (is_valid_int(left) && is_valid_int(right)) {
            left_int = strtol(left, NULL, 10);
            right_int = strtol(right, NULL, 10);
            if (left_int < right_int) {
                return -1;
            }
            if (left_int > right_int) {
                return 1;
            }
            return 0;
        }
    } else if (type == COL_FLOAT) {
        if (is_valid_float(left) && is_valid_float(right)) {
            left_float = strtod(left, NULL);
            right_float = strtod(right, NULL);
            if (left_float < right_float) {
                return -1;
            }
            if (left_float > right_float) {
                return 1;
            }
            return 0;
        }
    }

    return strcmp(left, right);
}

static int compare_rows_for_sort(const void *left_ptr, const void *right_ptr) {
    const Row *left_row = (const Row *)left_ptr;
    const Row *right_row = (const Row *)right_ptr;
    int result;

    result = compare_values_for_sort(g_sort_schema->columns[g_sort_column_index].type,
                                     left_row->data[g_sort_column_index],
                                     right_row->data[g_sort_column_index]);

    if (g_sort_direction == SORT_DESC) {
        return -result;
    }

    return result;
}

static void sort_result_set(ResultSet *result, Schema *schema, int column_index, SortDirection direction) {
    if (result->row_count <= 1) {
        return;
    }

    g_sort_schema = schema;
    g_sort_column_index = column_index;
    g_sort_direction = direction;
    qsort(result->rows, (size_t)result->row_count, sizeof(Row), compare_rows_for_sort);
    g_sort_schema = NULL;
    g_sort_column_index = -1;
    g_sort_direction = SORT_ASC;
}

static int find_primary_key_index(Schema *schema) {
    int index;

    for (index = 0; index < schema->column_count; ++index) {
        if (schema->columns[index].is_primary_key) {
            return index;
        }
    }

    return -1;
}

static int validate_value(ColumnDef *column, const char *value, int is_null) {
    if (is_null) {
        if (!column->nullable || column->is_primary_key) {
            fprintf(stderr,
                    "[ERROR] Executor: column '%s' does not allow NULL\n",
                    column->name);
            return 0;
        }
        return 1;
    }

    if (contains_forbidden_storage_char(value)) {
        fprintf(stderr,
                "[ERROR] Executor: column '%s' contains unsupported storage character\n",
                column->name);
        return 0;
    }

    if (column->type == COL_INT && !is_valid_int(value)) {
        fprintf(stderr,
                "[ERROR] Executor: type mismatch for column '%s' (expected INT)\n",
                column->name);
        return 0;
    }

    if (column->type == COL_FLOAT && !is_valid_float(value)) {
        fprintf(stderr,
                "[ERROR] Executor: type mismatch for column '%s' (expected FLOAT)\n",
                column->name);
        return 0;
    }

    if (column->type == COL_DATE && !is_valid_date(value)) {
        fprintf(stderr,
                "[ERROR] Executor: type mismatch for column '%s' (expected DATE)\n",
                column->name);
        return 0;
    }

    if (column->type == COL_VARCHAR &&
        column->max_length > 0 &&
        (int)strlen(value) > column->max_length) {
        fprintf(stderr,
                "[ERROR] Executor: value too long for column '%s' (max %d)\n",
                column->name,
                column->max_length);
        return 0;
    }

    return 1;
}

/* 기존: storage_select 로 풀스캔 → O(n). 현재: B+ 트리로 O(log n).
 * INSERT 경로의 중복 PK 검사를 인덱스 조회로 대체한다. */
static int ensure_primary_key_is_unique(Schema *schema, Row *row) {
    BTree *tree;
    int pk_index;
    long key_long;
    char *end_ptr;
    int64_t dummy_offset;

    pk_index = find_primary_key_index(schema);
    if (pk_index < 0) {
        return 1;
    }
    if (schema->columns[pk_index].type != COL_INT) {
        /* 현 엔진은 INT PK 만 인덱싱한다. 비 INT 는 검사 생략. */
        return 1;
    }

    key_long = strtol(row->data[pk_index], &end_ptr, 10);
    if (*end_ptr != '\0') {
        fprintf(stderr,
                "[ERROR] Executor: primary key '%s' is not a valid integer\n",
                row->data[pk_index]);
        return 0;
    }

    tree = index_get_or_build(schema->table_name, schema);
    if (tree == NULL) {
        /* 인덱스를 못 만들면 예전 로직으로 폴백 */
        ColumnList star_columns;
        WhereClause where_clause;
        ResultSet *result;
        int is_unique;

        memset(&star_columns, 0, sizeof(star_columns));
        star_columns.is_star = 1;
        memset(&where_clause, 0, sizeof(where_clause));
        where_clause.condition_count = 1;
        strncpy(where_clause.conditions[0].column_name,
                schema->columns[pk_index].name,
                sizeof(where_clause.conditions[0].column_name) - 1);
        strncpy(where_clause.conditions[0].operator,
                "=",
                sizeof(where_clause.conditions[0].operator) - 1);
        strncpy(where_clause.conditions[0].value,
                row->data[pk_index],
                sizeof(where_clause.conditions[0].value) - 1);
        result = storage_select(schema->table_name,
                                schema,
                                &star_columns,
                                &where_clause);
        if (result == NULL) {
            return 0;
        }
        is_unique = (result->row_count == 0);
        free_result_set(result);
        if (!is_unique) {
            fprintf(stderr,
                    "[ERROR] Executor: duplicate primary key for column '%s'\n",
                    schema->columns[pk_index].name);
        }
        return is_unique;
    }

    if (btree_find(tree, (int32_t)key_long, &dummy_offset)) {
        fprintf(stderr,
                "[ERROR] Executor: duplicate primary key for column '%s'\n",
                schema->columns[pk_index].name);
        return 0;
    }
    return 1;
}

/* INSERT 시 사용자가 PK 를 지정하지 않았으면 (btree_max_key + 1) 로 자동 부여.
 * 호출 측은 pk_index 가 INT 이고 값이 비어있음을 이미 확인한 상태여야 한다. */
static int auto_assign_pk(Schema *schema, Row *row, int pk_index) {
    BTree *tree;
    int32_t next_id;

    tree = index_get_or_build(schema->table_name, schema);
    if (tree == NULL) {
        fprintf(stderr,
                "[ERROR] Executor: cannot build index for auto-increment on '%s'\n",
                schema->table_name);
        return 0;
    }
    next_id = btree_max_key(tree) + 1;
    snprintf(row->data[pk_index], MAX_TOKEN_LEN, "%d", next_id);
    return 1;
}

static int build_insert_row(Statement *stmt, Schema *schema, Row *row) {
    int provided_indices[MAX_COLUMNS];
    int index;

    memset(provided_indices, -1, sizeof(provided_indices));
    memset(row, 0, sizeof(*row));
    row->column_count = schema->column_count;

    if (stmt->insert_columns.count != stmt->value_count) {
        fprintf(stderr, "[ERROR] Executor: column count does not match value count\n");
        return 0;
    }

    /* 먼저 사용자가 넣은 컬럼들을 스키마 인덱스에 매핑한다. */
    for (index = 0; index < stmt->insert_columns.count; ++index) {
        int schema_index =
            schema_get_column_index(schema, stmt->insert_columns.names[index]);

        if (schema_index < 0) {
            fprintf(stderr,
                    "[ERROR] Executor: unknown column '%s'\n",
                    stmt->insert_columns.names[index]);
            return 0;
        }

        if (provided_indices[schema_index] != -1) {
            fprintf(stderr,
                    "[ERROR] Executor: duplicate column '%s' in INSERT\n",
                    stmt->insert_columns.names[index]);
            return 0;
        }

        if (!validate_value(&schema->columns[schema_index],
                            stmt->values[index],
                            stmt->value_is_null[index])) {
            return 0;
        }

        provided_indices[schema_index] = index;
    }

    /* 저장 직전에는 반드시 스키마 순서로 행을 재구성한다.
     * PK(INT)가 미지정이면 자동증가로 채우고, 사용자 지정이면 그대로 쓴다. */
    {
        int pk_index = find_primary_key_index(schema);

        for (index = 0; index < schema->column_count; ++index) {
            int value_index = provided_indices[index];

            if (value_index == -1) {
                /* PK INT 컬럼이 비어있다 → 자동증가로 채울 예정. 빈칸으로 둔다. */
                if (index == pk_index && schema->columns[index].type == COL_INT) {
                    row->data[index][0] = '\0';
                    continue;
                }
                if (!schema->columns[index].nullable) {
                    fprintf(stderr,
                            "[ERROR] Executor: missing value for non-nullable column '%s'\n",
                            schema->columns[index].name);
                    return 0;
                }
                row->data[index][0] = '\0';
                continue;
            }

            if (stmt->value_is_null[value_index]) {
                row->data[index][0] = '\0';
                continue;
            }

            strncpy(row->data[index], stmt->values[value_index], MAX_TOKEN_LEN - 1);
            row->data[index][MAX_TOKEN_LEN - 1] = '\0';
        }

        if (pk_index >= 0 &&
            schema->columns[pk_index].type == COL_INT &&
            row->data[pk_index][0] == '\0') {
            if (!auto_assign_pk(schema, row, pk_index)) {
                return 0;
            }
            /* 자동증가는 항상 (max+1)이라 중복 검사 생략. */
            return 1;
        }
    }

    return ensure_primary_key_is_unique(schema, row);
}

static void print_separator(const int *widths, int count) {
    int column_index;
    int dash_index;

    for (column_index = 0; column_index < count; ++column_index) {
        putchar('+');
        for (dash_index = 0; dash_index < widths[column_index] + 2; ++dash_index) {
            putchar('-');
        }
    }
    puts("+");
}

static void print_header_row(char headers[MAX_COLUMNS][MAX_TOKEN_LEN], const int *widths, int count) {
    int column_index;

    for (column_index = 0; column_index < count; ++column_index) {
        printf("| %-*s ", widths[column_index], headers[column_index]);
    }
    puts("|");
}

static void print_result_row(const ResultSet *result, const Row *row, const int *widths) {
    int column_index;

    for (column_index = 0; column_index < result->selected_count; ++column_index) {
        const char *value = row->data[result->selected_indexes[column_index]];
        printf("| %-*s ", widths[column_index], value);
    }
    puts("|");
}

static void print_result_table(ResultSet *result) {
    int widths[MAX_COLUMNS];
    char headers[MAX_COLUMNS][MAX_TOKEN_LEN];
    int column_index;
    int row_index;

    /* 표 폭은 헤더와 실제 데이터 중 더 긴 쪽에 맞춘다. */
    for (column_index = 0; column_index < result->selected_count; ++column_index) {
        strncpy(headers[column_index],
                result->schema->columns[result->selected_indexes[column_index]].name,
                MAX_TOKEN_LEN - 1);
        headers[column_index][MAX_TOKEN_LEN - 1] = '\0';
        widths[column_index] = (int)strlen(headers[column_index]);
    }

    for (row_index = 0; row_index < result->row_count; ++row_index) {
        for (column_index = 0; column_index < result->selected_count; ++column_index) {
            int value_length = (int)strlen(
                result->rows[row_index].data[result->selected_indexes[column_index]]);
            if (value_length > widths[column_index]) {
                widths[column_index] = value_length;
            }
        }
    }

    print_separator(widths, result->selected_count);
    print_header_row(headers, widths, result->selected_count);
    print_separator(widths, result->selected_count);

    for (row_index = 0; row_index < result->row_count; ++row_index) {
        print_result_row(result, &result->rows[row_index], widths);
    }

    print_separator(widths, result->selected_count);
}

int execute_insert(Statement *stmt) {
    Schema *schema;
    Row row;
    int status;
    int64_t row_offset = -1;
    int pk_index;

    schema = schema_load(stmt->table_name);
    if (schema == NULL) {
        return -1;
    }

    if (!build_insert_row(stmt, schema, &row)) {
        schema_free(schema);
        return -1;
    }

    status = storage_insert(stmt->table_name, &row, schema, &row_offset);
    if (status != 0) {
        schema_free(schema);
        return -1;
    }

    /* 저장이 성공했으니 인덱스에도 등록 (INT PK 에 한해). */
    pk_index = find_primary_key_index(schema);
    if (pk_index >= 0 && schema->columns[pk_index].type == COL_INT) {
        long key_long;
        char *end_ptr;

        key_long = strtol(row.data[pk_index], &end_ptr, 10);
        if (*end_ptr == '\0') {
            index_record_insert(stmt->table_name, (int32_t)key_long, row_offset);
        }
    }

    schema_free(schema);
    if (g_executor_output_enabled) {
        puts("1 row inserted.");
    }
    return 0;
}

static ResultSet *build_projected_result(Schema *schema, ColumnList *columns) {
    ResultSet *result;
    int i;

    result = (ResultSet *)calloc(1, sizeof(ResultSet));
    if (result == NULL) {
        return NULL;
    }
    result->schema = schema;
    if (columns == NULL || columns->is_star) {
        result->selected_count = schema->column_count;
        for (i = 0; i < schema->column_count; ++i) {
            result->selected_indexes[i] = i;
        }
    } else {
        result->selected_count = columns->count;
        for (i = 0; i < columns->count; ++i) {
            int idx = schema_get_column_index(schema, columns->names[i]);
            if (idx < 0) {
                free_result_set(result);
                return NULL;
            }
            result->selected_indexes[i] = idx;
        }
    }
    return result;
}

/* WHERE 절이 "PK = 상수" 단일 동등 조건일 때 B+ 트리로 즉시 해석한다.
 * - 리턴 1: 인덱스 경로 사용, *out 에 ResultSet 설정
 * - 리턴 0: 적용 불가 (호출자가 storage_select 로 폴백)
 * - 리턴 -1: 오류 */
static int try_index_select(Statement *stmt, Schema *schema, ResultSet **out) {
    int pk_index;
    int32_t key;
    long key_long;
    char *end_ptr;
    int64_t offset;
    ResultSet *result;
    int lookup_rc;

    if (stmt->where.condition_count != 1) {
        return 0;
    }
    if (strcmp(stmt->where.conditions[0].operator, "=") != 0) {
        return 0;
    }
    pk_index = find_primary_key_index(schema);
    if (pk_index < 0 || schema->columns[pk_index].type != COL_INT) {
        return 0;
    }
    if (strcmp(stmt->where.conditions[0].column_name,
               schema->columns[pk_index].name) != 0) {
        return 0;
    }

    key_long = strtol(stmt->where.conditions[0].value, &end_ptr, 10);
    if (*end_ptr != '\0') {
        return 0;
    }
    key = (int32_t)key_long;

    result = build_projected_result(schema, &stmt->select_columns);
    if (result == NULL) {
        return -1;
    }

    lookup_rc = index_lookup_offset(stmt->table_name, schema, key, &offset);
    if (lookup_rc < 0) {
        free_result_set(result);
        return 0;
    }

    if (lookup_rc > 0) {
        Row row;
        if (!storage_read_row_at(stmt->table_name, schema, offset, &row)) {
            free_result_set(result);
            return -1;
        }
        result->rows[0] = row;
        result->row_count = 1;
    }

    *out = result;
    return 1;
}

int execute_select(Statement *stmt) {
    Schema *schema;
    ResultSet *result;
    int index;
    int order_column_index = -1;
    int idx_rc;

    schema = schema_load(stmt->table_name);
    if (schema == NULL) {
        return -1;
    }

    if (!stmt->select_columns.is_star) {
        for (index = 0; index < stmt->select_columns.count; ++index) {
            if (schema_get_column_index(schema, stmt->select_columns.names[index]) < 0) {
                fprintf(stderr,
                        "[ERROR] Executor: unknown column '%s'\n",
                        stmt->select_columns.names[index]);
                schema_free(schema);
                return -1;
            }
        }
    }

    if (stmt->order_by.enabled) {
        order_column_index = schema_get_column_index(schema, stmt->order_by.column_name);
        if (order_column_index < 0) {
            fprintf(stderr,
                    "[ERROR] Executor: unknown ORDER BY column '%s'\n",
                    stmt->order_by.column_name);
            schema_free(schema);
            return -1;
        }
    }

    result = NULL;
    idx_rc = try_index_select(stmt, schema, &result);
    if (idx_rc < 0) {
        schema_free(schema);
        return -1;
    }
    if (idx_rc == 0) {
        result = storage_select(stmt->table_name, schema, &stmt->select_columns, &stmt->where);
    }
    if (result == NULL) {
        schema_free(schema);
        return -1;
    }

    if (stmt->order_by.enabled) {
        sort_result_set(result, schema, order_column_index, stmt->order_by.direction);
    }

    if (g_executor_output_enabled) {
        print_result_table(result);
        printf("%d row(s) selected.\n", result->row_count);
    }

    free_result_set(result);
    schema_free(schema);
    return 0;
}

int execute(Statement *stmt) {
    if (stmt == NULL) {
        fprintf(stderr, "[ERROR] Executor: statement is NULL\n");
        return -1;
    }

    if (stmt->type == STMT_INSERT) {
        return execute_insert(stmt);
    }

    if (stmt->type == STMT_SELECT) {
        return execute_select(stmt);
    }

    fprintf(stderr, "[ERROR] Executor: unsupported statement type\n");
    return -1;
}
