#include "index.h"

#include "config.h"
#include "schema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INDEX_MAX_TABLES 16

typedef struct {
    char table_name[MAX_IDENTIFIER_LEN];
    BTree *tree;
} IndexEntry;

static IndexEntry g_entries[INDEX_MAX_TABLES];
static int g_entry_count = 0;

static int find_pk_column(Schema *schema) {
    int i;

    for (i = 0; i < schema->column_count; ++i) {
        if (schema->columns[i].is_primary_key) {
            return i;
        }
    }
    return -1;
}

/* .tbl 파일을 한 번만 읽어 (id → 행 시작 오프셋) 매핑을 구축한다.
 * 이미 존재하지 않는 파일이면 빈 트리 그대로 반환. */
static int build_from_file(BTree *tree, Schema *schema, int pk_index) {
    char path[MAX_PATH_LEN];
    FILE *file;
    char line[4096];
    long row_start;

    snprintf(path, sizeof(path), "%s/%s.tbl", g_data_dir, schema->table_name);
    file = fopen(path, "r");
    if (file == NULL) {
        /* 파일이 없어도 빈 인덱스는 유효한 상태다. */
        return 1;
    }

    /* 헤더 한 줄 건너뛰기. */
    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        return 1;
    }

    while (1) {
        int field_idx = 0;
        int col = 0;
        int pk_val_len = 0;
        char pk_buf[64];
        long key_long;
        char *end_ptr;

        row_start = ftell(file);
        if (row_start < 0) {
            fclose(file);
            return 0;
        }
        if (fgets(line, sizeof(line), file) == NULL) {
            break;
        }

        /* 첫 번째 PK 필드만 추출하면 된다. 파이프로 구분되므로 pk_index 번째
         * 필드까지만 읽는다. */
        pk_buf[0] = '\0';
        while (line[col] != '\0' && line[col] != '\n' && line[col] != '\r') {
            if (line[col] == '|') {
                if (field_idx == pk_index) {
                    pk_buf[pk_val_len] = '\0';
                    break;
                }
                field_idx++;
                pk_val_len = 0;
                col++;
                continue;
            }
            if (field_idx == pk_index && pk_val_len < (int)sizeof(pk_buf) - 1) {
                pk_buf[pk_val_len++] = line[col];
            }
            col++;
        }
        if (field_idx == pk_index && pk_buf[0] == '\0' && pk_val_len == 0) {
            /* PK 가 비어있는 행은 스킵 */
            continue;
        }
        pk_buf[pk_val_len] = '\0';

        key_long = strtol(pk_buf, &end_ptr, 10);
        if (*end_ptr != '\0') {
            /* 정수가 아닌 PK → 무시 (members 스키마는 INT PK) */
            continue;
        }
        if (btree_insert(tree, (int32_t)key_long, (int64_t)row_start) == -2) {
            fclose(file);
            return 0;
        }
        /* 중복 키(-1)는 데이터 파일 기준으로는 무시하고 진행한다. */
    }

    fclose(file);
    return 1;
}

BTree *index_get_or_build(const char *table_name, Schema *schema) {
    int i;
    int pk_index;
    BTree *tree;

    if (table_name == NULL || schema == NULL) {
        return NULL;
    }

    for (i = 0; i < g_entry_count; ++i) {
        if (strcmp(g_entries[i].table_name, table_name) == 0) {
            return g_entries[i].tree;
        }
    }

    pk_index = find_pk_column(schema);
    if (pk_index < 0 || schema->columns[pk_index].type != COL_INT) {
        return NULL;
    }
    if (g_entry_count >= INDEX_MAX_TABLES) {
        fprintf(stderr, "[ERROR] Index: too many tables\n");
        return NULL;
    }

    tree = btree_create();
    if (tree == NULL) {
        return NULL;
    }
    if (!build_from_file(tree, schema, pk_index)) {
        btree_free(tree);
        return NULL;
    }

    strncpy(g_entries[g_entry_count].table_name,
            table_name,
            MAX_IDENTIFIER_LEN - 1);
    g_entries[g_entry_count].table_name[MAX_IDENTIFIER_LEN - 1] = '\0';
    g_entries[g_entry_count].tree = tree;
    g_entry_count++;
    return tree;
}

void index_drop(const char *table_name) {
    int i;
    int j;

    if (table_name == NULL) {
        return;
    }
    for (i = 0; i < g_entry_count; ++i) {
        if (strcmp(g_entries[i].table_name, table_name) == 0) {
            btree_free(g_entries[i].tree);
            for (j = i; j < g_entry_count - 1; ++j) {
                g_entries[j] = g_entries[j + 1];
            }
            g_entry_count--;
            return;
        }
    }
}

void index_drop_all(void) {
    int i;

    for (i = 0; i < g_entry_count; ++i) {
        btree_free(g_entries[i].tree);
        g_entries[i].tree = NULL;
    }
    g_entry_count = 0;
}
