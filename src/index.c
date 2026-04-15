#include "index.h"

#include "config.h"
#include "schema.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define INDEX_MAX_TABLES 16

typedef struct {
    int exists;
    long long size;
    long long mtime_sec;
    long mtime_nsec;
} FileStamp;

typedef struct {
    int32_t key;
    int64_t offset;
} PersistedIndexRecord;

typedef struct {
    char table_name[MAX_IDENTIFIER_LEN];
    BTree *tree;
    FileStamp table_stamp;
    int persisted_dirty;
} IndexEntry;

static IndexEntry g_entries[INDEX_MAX_TABLES];
static int g_entry_count = 0;

static void build_table_path(char *path, size_t size, const char *table_name) {
    snprintf(path, size, "%s/%s.tbl", g_data_dir, table_name);
}

static void build_index_path(char *path, size_t size, const char *table_name) {
    snprintf(path, size, "%s/%s.idx", g_data_dir, table_name);
}

static long stat_mtime_nsec(const struct stat *st) {
    (void)st;
    return 0;
}

static void clear_file_stamp(FileStamp *stamp) {
    if (stamp != NULL) {
        memset(stamp, 0, sizeof(*stamp));
    }
}

static int read_file_stamp(const char *path, FileStamp *stamp) {
    struct stat st;

    if (path == NULL || stamp == NULL) {
        return 0;
    }

    clear_file_stamp(stamp);
    if (stat(path, &st) != 0) {
        return 0;
    }

    stamp->exists = 1;
    stamp->size = (long long)st.st_size;
    stamp->mtime_sec = (long long)st.st_mtime;
    stamp->mtime_nsec = stat_mtime_nsec(&st);
    return 1;
}

static int file_stamps_equal(const FileStamp *left, const FileStamp *right) {
    if (left == NULL || right == NULL) {
        return 0;
    }

    return left->exists == right->exists &&
           left->size == right->size &&
           left->mtime_sec == right->mtime_sec &&
           left->mtime_nsec == right->mtime_nsec;
}

static int file_stamp_is_newer_or_equal(const FileStamp *left, const FileStamp *right) {
    if (left == NULL || right == NULL || !left->exists || !right->exists) {
        return 0;
    }
    if (left->mtime_sec > right->mtime_sec) {
        return 1;
    }
    if (left->mtime_sec < right->mtime_sec) {
        return 0;
    }
    return left->mtime_nsec >= right->mtime_nsec;
}

static int find_pk_column(Schema *schema) {
    int i;

    for (i = 0; i < schema->column_count; ++i) {
        if (schema->columns[i].is_primary_key) {
            return i;
        }
    }
    return -1;
}

static int find_entry_index(const char *table_name) {
    int i;

    if (table_name == NULL) {
        return -1;
    }

    for (i = 0; i < g_entry_count; ++i) {
        if (strcmp(g_entries[i].table_name, table_name) == 0) {
            return i;
        }
    }
    return -1;
}

static IndexEntry *find_entry(const char *table_name) {
    int index = find_entry_index(table_name);

    if (index < 0) {
        return NULL;
    }
    return &g_entries[index];
}

static IndexEntry *create_entry(const char *table_name) {
    IndexEntry *entry;

    if (table_name == NULL || g_entry_count >= INDEX_MAX_TABLES) {
        return NULL;
    }

    entry = &g_entries[g_entry_count++];
    memset(entry, 0, sizeof(*entry));
    strncpy(entry->table_name, table_name, MAX_IDENTIFIER_LEN - 1);
    entry->table_name[MAX_IDENTIFIER_LEN - 1] = '\0';
    return entry;
}

static void capture_table_stamp(IndexEntry *entry, const char *table_name) {
    char path[MAX_PATH_LEN];

    if (entry == NULL || table_name == NULL) {
        return;
    }

    build_table_path(path, sizeof(path), table_name);
    if (!read_file_stamp(path, &entry->table_stamp)) {
        clear_file_stamp(&entry->table_stamp);
    }
}

static int entry_is_fresh(IndexEntry *entry, const char *table_name) {
    char path[MAX_PATH_LEN];
    FileStamp current_stamp;

    if (entry == NULL || table_name == NULL) {
        return 0;
    }

    build_table_path(path, sizeof(path), table_name);
    if (!read_file_stamp(path, &current_stamp)) {
        clear_file_stamp(&current_stamp);
    }
    return file_stamps_equal(&entry->table_stamp, &current_stamp);
}

static int write_record_visitor(int32_t key, int64_t offset, void *context) {
    PersistedIndexRecord record;
    FILE *file = (FILE *)context;

    record.key = key;
    record.offset = offset;
    return fwrite(&record, sizeof(record), 1, file) == 1;
}

static int write_persisted_index(const char *table_name, BTree *tree) {
    char index_path[MAX_PATH_LEN];
    FILE *file;
    size_t record_count;
    size_t visited;

    if (table_name == NULL || tree == NULL) {
        return 0;
    }

    build_index_path(index_path, sizeof(index_path), table_name);
    file = fopen(index_path, "wb");
    if (file == NULL) {
        return 0;
    }

    record_count = btree_size(tree);
    if (record_count > 0) {
        visited = btree_visit_first_n(tree, record_count, write_record_visitor, file);
        if (visited != record_count) {
            fclose(file);
            remove(index_path);
            return 0;
        }
    }

    fclose(file);
    return 1;
}

static int sync_persisted_index(IndexEntry *entry) {
    if (entry == NULL || entry->tree == NULL || !entry->persisted_dirty) {
        return 1;
    }

    if (!write_persisted_index(entry->table_name, entry->tree)) {
        return 0;
    }

    entry->persisted_dirty = 0;
    return 1;
}

static int is_persisted_index_fresh(const char *table_name) {
    char table_path[MAX_PATH_LEN];
    char index_path[MAX_PATH_LEN];
    FileStamp table_stamp;
    FileStamp index_stamp;

    if (table_name == NULL) {
        return 0;
    }

    build_table_path(table_path, sizeof(table_path), table_name);
    build_index_path(index_path, sizeof(index_path), table_name);

    if (!read_file_stamp(table_path, &table_stamp)) {
        return 0;
    }
    if (!read_file_stamp(index_path, &index_stamp)) {
        return 0;
    }

    return file_stamp_is_newer_or_equal(&index_stamp, &table_stamp);
}

static int binary_search_persisted_index(const char *table_name,
                                         int32_t key,
                                         int64_t *out_offset) {
    char index_path[MAX_PATH_LEN];
    FILE *file;
    long file_size;
    long record_count;
    long lo;
    long hi;

    if (table_name == NULL) {
        return -1;
    }

    build_index_path(index_path, sizeof(index_path), table_name);
    file = fopen(index_path, "rb");
    if (file == NULL) {
        return -1;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    file_size = ftell(file);
    if (file_size < 0 || (file_size % (long)sizeof(PersistedIndexRecord)) != 0) {
        fclose(file);
        return -1;
    }

    record_count = file_size / (long)sizeof(PersistedIndexRecord);
    lo = 0;
    hi = record_count;
    while (lo < hi) {
        long mid = lo + (hi - lo) / 2;
        PersistedIndexRecord record;

        if (fseek(file, mid * (long)sizeof(PersistedIndexRecord), SEEK_SET) != 0) {
            fclose(file);
            return -1;
        }
        if (fread(&record, sizeof(record), 1, file) != 1) {
            fclose(file);
            return -1;
        }

        if (record.key == key) {
            if (out_offset != NULL) {
                *out_offset = record.offset;
            }
            fclose(file);
            return 1;
        }
        if (record.key < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    fclose(file);
    return 0;
}

/* .tbl 파일을 한 번만 읽어 (id → 행 시작 오프셋) 매핑을 구축한다.
 * 이미 존재하지 않는 파일이면 빈 트리 그대로 반환. */
static int build_from_file(BTree *tree, Schema *schema, int pk_index) {
    char path[MAX_PATH_LEN];
    FILE *file;
    char line[4096];
    long row_start;

    build_table_path(path, sizeof(path), schema->table_name);
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

static int refresh_entry_from_table(IndexEntry *entry, const char *table_name, Schema *schema) {
    BTree *tree;
    int pk_index;
    int persisted_ok;

    if (entry == NULL || table_name == NULL || schema == NULL) {
        return 0;
    }

    pk_index = find_pk_column(schema);
    if (pk_index < 0 || schema->columns[pk_index].type != COL_INT) {
        return 0;
    }

    tree = btree_create();
    if (tree == NULL) {
        return 0;
    }
    if (!build_from_file(tree, schema, pk_index)) {
        btree_free(tree);
        return 0;
    }

    persisted_ok = write_persisted_index(table_name, tree);

    if (entry->tree != NULL) {
        btree_free(entry->tree);
    }
    entry->tree = tree;
    capture_table_stamp(entry, table_name);
    entry->persisted_dirty = persisted_ok ? 0 : 1;
    return 1;
}

BTree *index_get_or_build(const char *table_name, Schema *schema) {
    IndexEntry *entry;
    int created = 0;
    int pk_index;

    if (table_name == NULL || schema == NULL) {
        return NULL;
    }

    pk_index = find_pk_column(schema);
    if (pk_index < 0 || schema->columns[pk_index].type != COL_INT) {
        return NULL;
    }

    entry = find_entry(table_name);
    if (entry == NULL) {
        entry = create_entry(table_name);
        if (entry == NULL) {
            fprintf(stderr, "[ERROR] Index: too many tables\n");
            return NULL;
        }
        created = 1;
    }

    if (entry->tree != NULL && entry_is_fresh(entry, table_name)) {
        return entry->tree;
    }

    if (!refresh_entry_from_table(entry, table_name, schema)) {
        if (created) {
            g_entry_count--;
        }
        return NULL;
    }

    return entry->tree;
}

int index_lookup_offset(const char *table_name,
                        Schema *schema,
                        int32_t key,
                        int64_t *out_offset) {
    int persisted_rc;
    IndexEntry *entry;
    BTree *tree;

    if (table_name == NULL || schema == NULL) {
        return -1;
    }

    entry = find_entry(table_name);
    if (entry != NULL) {
        if (!entry_is_fresh(entry, table_name)) {
            if (!refresh_entry_from_table(entry, table_name, schema)) {
                return -1;
            }
        } else {
            (void)sync_persisted_index(entry);
        }

        if (entry->tree == NULL) {
            return -1;
        }
        return btree_find(entry->tree, key, out_offset);
    }

    if (is_persisted_index_fresh(table_name)) {
        persisted_rc = binary_search_persisted_index(table_name, key, out_offset);
        if (persisted_rc >= 0) {
            return persisted_rc;
        }
    }

    tree = index_get_or_build(table_name, schema);
    if (tree == NULL) {
        return -1;
    }
    return btree_find(tree, key, out_offset);
}

void index_invalidate_persisted(const char *table_name) {
    char index_path[MAX_PATH_LEN];
    IndexEntry *entry;

    if (table_name == NULL) {
        return;
    }

    build_index_path(index_path, sizeof(index_path), table_name);
    remove(index_path);

    entry = find_entry(table_name);
    if (entry != NULL) {
        entry->persisted_dirty = 1;
    }
}

void index_record_insert(const char *table_name, int32_t key, int64_t row_offset) {
    IndexEntry *entry;
    int insert_rc;

    if (table_name == NULL) {
        return;
    }

    entry = find_entry(table_name);
    if (entry == NULL || entry->tree == NULL) {
        index_invalidate_persisted(table_name);
        return;
    }

    index_invalidate_persisted(table_name);
    insert_rc = btree_insert(entry->tree, key, row_offset);
    if (insert_rc == -2) {
        return;
    }

    capture_table_stamp(entry, table_name);
    (void)sync_persisted_index(entry);
}

void index_drop(const char *table_name) {
    int i = find_entry_index(table_name);
    int j;

    if (i < 0) {
        return;
    }

    btree_free(g_entries[i].tree);
    for (j = i; j < g_entry_count - 1; ++j) {
        g_entries[j] = g_entries[j + 1];
    }
    g_entry_count--;
}

void index_drop_all(void) {
    int i;

    for (i = 0; i < g_entry_count; ++i) {
        btree_free(g_entries[i].tree);
    }
    g_entry_count = 0;
}
