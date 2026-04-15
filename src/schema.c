#include "schema.h"

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void trim_in_place(char *text) {
    char *start = text;
    char *end;

    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }

    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
}

static void to_upper_copy(char *dest, const char *src, size_t size) {
    size_t index;

    if (size == 0) {
        return;
    }

    for (index = 0; src[index] != '\0' && index < size - 1; ++index) {
        dest[index] = (char)toupper((unsigned char)src[index]);
    }
    dest[index] = '\0';
}

static void to_lower_copy(char *dest, const char *src, size_t size) {
    size_t index;

    if (size == 0) {
        return;
    }

    for (index = 0; src[index] != '\0' && index < size - 1; ++index) {
        dest[index] = (char)tolower((unsigned char)src[index]);
    }
    dest[index] = '\0';
}

static char *next_csv_token(char **cursor) {
    char *token;
    char *current;

    if (cursor == NULL || *cursor == NULL) {
        return NULL;
    }

    current = *cursor;
    token = current;

    while (*current != '\0' && *current != ',') {
        current++;
    }

    if (*current == ',') {
        *current = '\0';
        *cursor = current + 1;
    } else {
        *cursor = NULL;
    }

    return token;
}

ColumnType schema_parse_type(const char *type_str) {
    char upper[MAX_IDENTIFIER_LEN];

    if (type_str == NULL) {
        return COL_UNKNOWN;
    }

    to_upper_copy(upper, type_str, sizeof(upper));

    if (strcmp(upper, "INT") == 0) {
        return COL_INT;
    }
    if (strcmp(upper, "VARCHAR") == 0) {
        return COL_VARCHAR;
    }
    if (strcmp(upper, "FLOAT") == 0) {
        return COL_FLOAT;
    }
    if (strcmp(upper, "DATE") == 0) {
        return COL_DATE;
    }

    return COL_UNKNOWN;
}

int schema_get_column_index(Schema *schema, const char *column_name) {
    char normalized[MAX_IDENTIFIER_LEN];
    int index;

    if (schema == NULL || column_name == NULL) {
        return -1;
    }

    to_lower_copy(normalized, column_name, sizeof(normalized));

    for (index = 0; index < schema->column_count; ++index) {
        if (strcmp(schema->columns[index].name, normalized) == 0) {
            return index;
        }
    }

    return -1;
}

Schema *schema_load(const char *table_name) {
    Schema *schema;
    FILE *file;
    char path[MAX_PATH_LEN];
    char line[1024];
    int line_number = 0;

    if (table_name == NULL) {
        fprintf(stderr, "[ERROR] Schema: table name is NULL\n");
        return NULL;
    }

    snprintf(path, sizeof(path), "%s/%s.schema", g_schema_dir, table_name);
    file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "[ERROR] Schema: failed to open %s\n", path);
        return NULL;
    }

    schema = (Schema *)calloc(1, sizeof(Schema));
    if (schema == NULL) {
        fclose(file);
        fprintf(stderr, "[ERROR] Schema: failed to allocate schema\n");
        return NULL;
    }

    to_lower_copy(schema->table_name, table_name, sizeof(schema->table_name));

    /* 한 줄이 곧 하나의 컬럼 정의다. */
    while (fgets(line, sizeof(line), file) != NULL) {
        char *parts[5];
        char *token;
        char *rest = line;
        int part_count = 0;
        ColumnDef *column;

        line_number++;
        trim_in_place(line);

        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        while ((token = next_csv_token(&rest)) != NULL && part_count < 5) {
            trim_in_place(token);
            parts[part_count++] = token;
        }

        if (part_count != 5) {
            fprintf(stderr,
                    "[ERROR] Schema: invalid format at line %d in %s\n",
                    line_number,
                    path);
            schema_free(schema);
            fclose(file);
            return NULL;
        }

        if (schema->column_count >= MAX_COLUMNS) {
            fprintf(stderr, "[ERROR] Schema: too many columns in %s\n", path);
            schema_free(schema);
            fclose(file);
            return NULL;
        }

        column = &schema->columns[schema->column_count];
        to_lower_copy(column->name, parts[0], sizeof(column->name));
        column->type = schema_parse_type(parts[1]);
        column->max_length = atoi(parts[2]);
        column->nullable = atoi(parts[3]);
        column->is_primary_key = atoi(parts[4]);

        if (column->type == COL_UNKNOWN) {
            fprintf(stderr,
                    "[ERROR] Schema: unknown type '%s' at line %d in %s\n",
                    parts[1],
                    line_number,
                    path);
            schema_free(schema);
            fclose(file);
            return NULL;
        }

        schema->column_count++;
    }

    fclose(file);
    return schema;
}

void schema_free(Schema *schema) {
    free(schema);
}
