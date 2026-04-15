#include "storage.h"

#include "config.h"
#include "schema.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_int_value(const char *text, long *out_value) {
    char *end_ptr;
    long value;

    if (text == NULL || text[0] == '\0') {
        return 0;
    }

    value = strtol(text, &end_ptr, 10);
    if (*end_ptr != '\0') {
        return 0;
    }

    *out_value = value;
    return 1;
}

static int parse_float_value(const char *text, double *out_value) {
    char *end_ptr;
    double value;

    if (text == NULL || text[0] == '\0') {
        return 0;
    }

    value = strtod(text, &end_ptr);
    if (*end_ptr != '\0') {
        return 0;
    }

    *out_value = value;
    return 1;
}

static void strip_newline(char *line) {
    size_t length = strlen(line);

    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        line[length - 1] = '\0';
        length--;
    }
}

/* 파이프 구분 포맷을 그대로 분해한다. */
static void split_pipe_line(const char *line,
                            char fields[MAX_COLUMNS][MAX_TOKEN_LEN],
                            int expected_fields) {
    int field_index = 0;
    int value_length = 0;
    int cursor = 0;

    for (field_index = 0; field_index < expected_fields; ++field_index) {
        fields[field_index][0] = '\0';
    }

    field_index = 0;
    while (line[cursor] != '\0' && field_index < expected_fields) {
        if (line[cursor] == '|') {
            fields[field_index][value_length] = '\0';
            field_index++;
            value_length = 0;
            cursor++;
            continue;
        }

        if (value_length < MAX_TOKEN_LEN - 1) {
            fields[field_index][value_length++] = line[cursor];
        }
        cursor++;
    }

    if (field_index < expected_fields) {
        fields[field_index][value_length] = '\0';
    }
}

int evaluate_condition(Row *row, Schema *schema, Condition *cond) {
    int column_index;
    const char *actual;

    if (row == NULL || schema == NULL || cond == NULL) {
        return 0;
    }

    column_index = schema_get_column_index(schema, cond->column_name);
    if (column_index < 0 || column_index >= row->column_count) {
        return 0;
    }

    actual = row->data[column_index];

    if (schema->columns[column_index].type == COL_INT) {
        long left;
        long right;

        if (!parse_int_value(actual, &left) || !parse_int_value(cond->value, &right)) {
            return 0;
        }

        if (strcmp(cond->operator, "=") == 0) {
            return left == right;
        }
        if (strcmp(cond->operator, "!=") == 0 || strcmp(cond->operator, "<>") == 0) {
            return left != right;
        }
        if (strcmp(cond->operator, "<") == 0) {
            return left < right;
        }
        if (strcmp(cond->operator, ">") == 0) {
            return left > right;
        }
        if (strcmp(cond->operator, "<=") == 0) {
            return left <= right;
        }
        if (strcmp(cond->operator, ">=") == 0) {
            return left >= right;
        }
        return 0;
    }

    if (schema->columns[column_index].type == COL_FLOAT) {
        double left;
        double right;

        if (!parse_float_value(actual, &left) || !parse_float_value(cond->value, &right)) {
            return 0;
        }

        if (strcmp(cond->operator, "=") == 0) {
            return left == right;
        }
        if (strcmp(cond->operator, "!=") == 0 || strcmp(cond->operator, "<>") == 0) {
            return left != right;
        }
        if (strcmp(cond->operator, "<") == 0) {
            return left < right;
        }
        if (strcmp(cond->operator, ">") == 0) {
            return left > right;
        }
        if (strcmp(cond->operator, "<=") == 0) {
            return left <= right;
        }
        if (strcmp(cond->operator, ">=") == 0) {
            return left >= right;
        }
        return 0;
    }

    if (strcmp(cond->operator, "=") == 0) {
        return strcmp(actual, cond->value) == 0;
    }
    if (strcmp(cond->operator, "!=") == 0 || strcmp(cond->operator, "<>") == 0) {
        return strcmp(actual, cond->value) != 0;
    }
    if (strcmp(cond->operator, "<") == 0) {
        return strcmp(actual, cond->value) < 0;
    }
    if (strcmp(cond->operator, ">") == 0) {
        return strcmp(actual, cond->value) > 0;
    }
    if (strcmp(cond->operator, "<=") == 0) {
        return strcmp(actual, cond->value) <= 0;
    }
    if (strcmp(cond->operator, ">=") == 0) {
        return strcmp(actual, cond->value) >= 0;
    }

    return 0;
}

int evaluate_where(Row *row, Schema *schema, WhereClause *where) {
    int index;
    int result;

    if (where == NULL || where->condition_count == 0) {
        return 1;
    }

    result = evaluate_condition(row, schema, &where->conditions[0]);
    for (index = 1; index < where->condition_count; ++index) {
        if (strcmp(where->logical_op, "OR") == 0) {
            result = result || evaluate_condition(row, schema, &where->conditions[index]);
        } else {
            result = result && evaluate_condition(row, schema, &where->conditions[index]);
        }
    }

    return result;
}

int storage_insert(const char *table_name,
                   Row *row,
                   Schema *schema,
                   int64_t *out_offset) {
    char path[MAX_PATH_LEN];
    FILE *check_file;
    FILE *file;
    int column_index;
    int file_exists = 0;
    long row_start;

    (void)table_name;

    if (row == NULL || schema == NULL) {
        fprintf(stderr, "[ERROR] Storage: insert input is NULL\n");
        return -1;
    }

    snprintf(path, sizeof(path), "%s/%s.tbl", g_data_dir, schema->table_name);

    check_file = fopen(path, "r");
    if (check_file != NULL) {
        file_exists = 1;
        fclose(check_file);
    }

    file = fopen(path, file_exists ? "a" : "w");
    if (file == NULL) {
        fprintf(stderr, "[ERROR] Storage: failed to open %s\n", path);
        return -1;
    }

    /* 새 파일일 때만 헤더를 한 번 기록한다. */
    if (!file_exists) {
        for (column_index = 0; column_index < schema->column_count; ++column_index) {
            fprintf(file, "%s", schema->columns[column_index].name);
            if (column_index + 1 < schema->column_count) {
                fputc('|', file);
            }
        }
        fputc('\n', file);
    }

    /* 행 기록 직전 오프셋을 B+ 트리 인덱스가 보관할 수 있도록 반환. */
    row_start = ftell(file);
    if (row_start < 0) {
        fprintf(stderr, "[ERROR] Storage: ftell failed on %s\n", path);
        fclose(file);
        return -1;
    }

    for (column_index = 0; column_index < schema->column_count; ++column_index) {
        fprintf(file, "%s", row->data[column_index]);
        if (column_index + 1 < schema->column_count) {
            fputc('|', file);
        }
    }
    fputc('\n', file);

    fclose(file);

    if (out_offset != NULL) {
        *out_offset = (int64_t)row_start;
    }
    return 0;
}

int storage_read_row_at(const char *table_name,
                        Schema *schema,
                        int64_t offset,
                        Row *out) {
    char path[MAX_PATH_LEN];
    FILE *file;
    char line[4096];

    (void)table_name;

    if (schema == NULL || out == NULL) {
        return 0;
    }

    snprintf(path, sizeof(path), "%s/%s.tbl", g_data_dir, schema->table_name);
    file = fopen(path, "r");
    if (file == NULL) {
        return 0;
    }

    if (fseek(file, (long)offset, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        return 0;
    }
    fclose(file);

    strip_newline(line);
    memset(out, 0, sizeof(*out));
    out->column_count = schema->column_count;
    split_pipe_line(line, out->data, schema->column_count);
    return 1;
}

ResultSet *storage_select(const char *table_name,
                          Schema *schema,
                          ColumnList *columns,
                          WhereClause *where) {
    char path[MAX_PATH_LEN];
    FILE *file;
    char line[4096];
    ResultSet *result;
    int index;

    if (table_name == NULL || schema == NULL) {
        fprintf(stderr, "[ERROR] Storage: select input is NULL\n");
        return NULL;
    }

    result = (ResultSet *)calloc(1, sizeof(ResultSet));
    if (result == NULL) {
        fprintf(stderr, "[ERROR] Storage: failed to allocate result set\n");
        return NULL;
    }

    result->schema = schema;

    if (columns == NULL || columns->is_star) {
        result->selected_count = schema->column_count;
        for (index = 0; index < schema->column_count; ++index) {
            result->selected_indexes[index] = index;
        }
    } else {
        result->selected_count = columns->count;
        for (index = 0; index < columns->count; ++index) {
            int column_index = schema_get_column_index(schema, columns->names[index]);
            if (column_index < 0) {
                fprintf(stderr,
                        "[ERROR] Storage: unknown column '%s' in projection\n",
                        columns->names[index]);
                free_result_set(result);
                return NULL;
            }
            result->selected_indexes[index] = column_index;
        }
    }

    snprintf(path, sizeof(path), "%s/%s.tbl", g_data_dir, table_name);
    file = fopen(path, "r");
    if (file == NULL) {
        if (errno == ENOENT) {
            return result;
        }

        fprintf(stderr, "[ERROR] Storage: failed to open %s\n", path);
        free_result_set(result);
        return NULL;
    }

    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        return result;
    }

    /* 저장 파일은 단순 텍스트라서 한 줄씩 읽으며 필터링한다. */
    while (fgets(line, sizeof(line), file) != NULL) {
        Row full_row;

        if (result->row_count >= MAX_ROWS) {
            break;
        }

        strip_newline(line);
        memset(&full_row, 0, sizeof(full_row));
        full_row.column_count = schema->column_count;
        split_pipe_line(line, full_row.data, schema->column_count);

        if (!evaluate_where(&full_row, schema, where)) {
            continue;
        }

        (void)index;
        result->rows[result->row_count++] = full_row;
    }

    fclose(file);
    return result;
}

void free_result_set(ResultSet *rs) {
    free(rs);
}
