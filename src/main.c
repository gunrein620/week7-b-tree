#include "btree.h"
#include "config.h"
#include "executor.h"
#include "index.h"
#include "lexer.h"
#include "parser.h"
#include "schema.h"
#include "storage.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void print_help(void) {
    puts("Usage: ./sqlengine [-f file | -e sql] [-d data_dir] [-s schema_dir]");
    puts("  -f <file>           Execute SQL statements from file");
    puts("  -e <sql>            Execute SQL statements from string");
    puts("  -d <dir>            Set data directory (default: ./data)");
    puts("  -s <dir>            Set schema directory (default: ./schemas)");
    puts("  --bench <table>     Compare indexed vs linear SELECT");
    puts("  --runs <n>          Repetitions for --bench (default 5)");
    puts("  --help              Show this help message");
    puts("  --version           Show version");
}

static double timespec_diff_usec(struct timespec start, struct timespec end) {
    double sec = (double)(end.tv_sec - start.tv_sec);
    double nsec = (double)(end.tv_nsec - start.tv_nsec);
    return sec * 1e6 + nsec / 1e3;
}

/* 인덱스 경로 vs 선형 스캔 경로를 같은 크기의 테이블에서 벤치마크한다.
 * - 인덱스 경로: btree_find + storage_read_row_at  (O(log n) + 1 seek)
 * - 선형 경로:   storage_select with WHERE name = ?  (O(n) 풀 스캔)
 * 두 쿼리 모두 "id=K 인 행" 을 가리키도록 key 와 name 을 동기화한다. */
static int run_benchmark(const char *table_name, int runs) {
    Schema *schema;
    BTree *tree;
    int32_t max_key;
    int32_t target_key;
    char name_buf[64];
    struct timespec t0;
    struct timespec t1;
    double total_index = 0.0;
    double total_linear = 0.0;
    int i;
    int pk_index;

    schema = schema_load(table_name);
    if (schema == NULL) {
        fprintf(stderr, "[BENCH] failed to load schema '%s'\n", table_name);
        return 1;
    }
    pk_index = -1;
    for (i = 0; i < schema->column_count; ++i) {
        if (schema->columns[i].is_primary_key) {
            pk_index = i;
            break;
        }
    }
    if (pk_index < 0 || schema->columns[pk_index].type != COL_INT) {
        fprintf(stderr, "[BENCH] table '%s' has no INT primary key\n", table_name);
        schema_free(schema);
        return 1;
    }

    /* 인덱스 구축 시간도 한번 측정한다 (최초 쿼리의 lazy build 비용). */
    clock_gettime(CLOCK_MONOTONIC, &t0);
    tree = index_get_or_build(table_name, schema);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    if (tree == NULL) {
        fprintf(stderr, "[BENCH] failed to build index\n");
        schema_free(schema);
        return 1;
    }
    printf("[BENCH] table=%s rows=%zu build=%.1f us\n",
           table_name,
           btree_size(tree),
           timespec_diff_usec(t0, t1));

    max_key = btree_max_key(tree);
    if (max_key <= 0) {
        fprintf(stderr, "[BENCH] table is empty\n");
        schema_free(schema);
        return 1;
    }
    /* 범위 한가운데 키를 고정 타겟으로 사용. */
    target_key = max_key / 2;
    if (target_key <= 0) {
        target_key = 1;
    }
    snprintf(name_buf, sizeof(name_buf), "name_%07d", (int)target_key);

    printf("[BENCH] target id=%d name='%s' runs=%d\n",
           (int)target_key,
           name_buf,
           runs);

    for (i = 0; i < runs; ++i) {
        int64_t offset = -1;
        Row row;

        /* 인덱스 경로 */
        clock_gettime(CLOCK_MONOTONIC, &t0);
        if (btree_find(tree, target_key, &offset)) {
            storage_read_row_at(table_name, schema, offset, &row);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        total_index += timespec_diff_usec(t0, t1);

        /* 선형 경로: storage_select 를 직접 호출하여 print 오버헤드 제거 */
        {
            ColumnList star;
            WhereClause where;
            ResultSet *result;

            memset(&star, 0, sizeof(star));
            star.is_star = 1;
            memset(&where, 0, sizeof(where));
            where.condition_count = 1;
            strncpy(where.conditions[0].column_name, "name",
                    sizeof(where.conditions[0].column_name) - 1);
            strncpy(where.conditions[0].operator, "=",
                    sizeof(where.conditions[0].operator) - 1);
            strncpy(where.conditions[0].value, name_buf,
                    sizeof(where.conditions[0].value) - 1);

            clock_gettime(CLOCK_MONOTONIC, &t0);
            result = storage_select(table_name, schema, &star, &where);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            if (result != NULL) {
                free_result_set(result);
            }
            total_linear += timespec_diff_usec(t0, t1);
        }
    }

    {
        double avg_index = total_index / runs;
        double avg_linear = total_linear / runs;
        double speedup = (avg_index > 0.0) ? (avg_linear / avg_index) : 0.0;
        printf("[BENCH] indexed avg: %10.2f us\n", avg_index);
        printf("[BENCH] linear  avg: %10.2f us\n", avg_linear);
        printf("[BENCH] speedup    : %10.2fx\n", speedup);
    }

    schema_free(schema);
    index_drop_all();
    return 0;
}

static char *read_file_contents(const char *path) {
    FILE *file;
    long file_size;
    char *buffer;

    file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    buffer = (char *)malloc((size_t)file_size + 1);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }

    if (fread(buffer, 1, (size_t)file_size, file) != (size_t)file_size) {
        free(buffer);
        fclose(file);
        return NULL;
    }

    buffer[file_size] = '\0';
    fclose(file);
    return buffer;
}

static char *trim_copy(const char *start, size_t length) {
    char *buffer;

    while (length > 0 && isspace((unsigned char)*start)) {
        start++;
        length--;
    }

    while (length > 0 && isspace((unsigned char)start[length - 1])) {
        length--;
    }

    buffer = (char *)malloc(length + 1);
    if (buffer == NULL) {
        return NULL;
    }

    memcpy(buffer, start, length);
    buffer[length] = '\0';
    return buffer;
}

static int run_single_statement(const char *sql) {
    Token *tokens;
    Statement *stmt;
    int token_count = 0;
    int exit_code = 0;

    /* CLI는 항상 lexer -> parser -> executor 순서로만 실행한다. */
    tokens = tokenize(sql, &token_count);
    if (tokens == NULL) {
        return 1;
    }

    if (token_count == 1 && tokens[0].type == TOKEN_EOF) {
        free_tokens(tokens);
        return 0;
    }

    stmt = parse(tokens, token_count);
    if (stmt == NULL) {
        free_tokens(tokens);
        return 1;
    }

    if (execute(stmt) != 0) {
        exit_code = 2;
    }

    free_statement(stmt);
    free_tokens(tokens);
    return exit_code;
}

/* 문자열 안의 세미콜론은 보존하고, 실제 문장 경계만 분리한다. */
static int run_sql_script(const char *script) {
    int worst_exit_code = 0;
    int in_string = 0;
    size_t statement_start = 0;
    size_t index;

    for (index = 0;; ++index) {
        char ch = script[index];

        if (!in_string && ch == '-' && script[index + 1] == '-') {
            index += 2;
            while (script[index] != '\0' && script[index] != '\n') {
                index++;
            }
            ch = script[index];
        }

        if (ch == '\'') {
            if (in_string && script[index + 1] == '\'') {
                index++;
                continue;
            }
            in_string = !in_string;
        }

        if ((ch == ';' && !in_string) || ch == '\0') {
            char *statement = trim_copy(script + statement_start, index - statement_start);
            int statement_exit_code;

            if (statement == NULL) {
                return 2;
            }

            if (statement[0] != '\0') {
                statement_exit_code = run_single_statement(statement);
                if (statement_exit_code > worst_exit_code) {
                    worst_exit_code = statement_exit_code;
                }
            }

            free(statement);
            statement_start = index + 1;
        }

        if (ch == '\0') {
            break;
        }
    }

    return worst_exit_code;
}

int main(int argc, char **argv) {
    const char *file_input = NULL;
    const char *sql_input = NULL;
    const char *bench_table = NULL;
    int bench_runs = 5;
    char *script = NULL;
    int index;

    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--bench") == 0) {
            if (index + 1 >= argc) {
                fprintf(stderr, "[ERROR] CLI: missing table after --bench\n");
                return 3;
            }
            bench_table = argv[++index];
        } else if (strcmp(argv[index], "--runs") == 0) {
            if (index + 1 >= argc) {
                fprintf(stderr, "[ERROR] CLI: missing number after --runs\n");
                return 3;
            }
            bench_runs = atoi(argv[++index]);
            if (bench_runs <= 0) {
                bench_runs = 1;
            }
        } else if (strcmp(argv[index], "-f") == 0) {
            if (index + 1 >= argc) {
                fprintf(stderr, "[ERROR] CLI: missing file path after -f\n");
                return 3;
            }
            file_input = argv[++index];
        } else if (strcmp(argv[index], "-e") == 0) {
            if (index + 1 >= argc) {
                fprintf(stderr, "[ERROR] CLI: missing SQL text after -e\n");
                return 3;
            }
            sql_input = argv[++index];
        } else if (strcmp(argv[index], "-d") == 0) {
            if (index + 1 >= argc) {
                fprintf(stderr, "[ERROR] CLI: missing directory after -d\n");
                return 3;
            }
            config_set_data_dir(argv[++index]);
        } else if (strcmp(argv[index], "-s") == 0) {
            if (index + 1 >= argc) {
                fprintf(stderr, "[ERROR] CLI: missing directory after -s\n");
                return 3;
            }
            config_set_schema_dir(argv[++index]);
        } else if (strcmp(argv[index], "--help") == 0) {
            print_help();
            return 0;
        } else if (strcmp(argv[index], "--version") == 0) {
            puts("1.0.0");
            return 0;
        } else {
            fprintf(stderr, "[ERROR] CLI: unknown option '%s'\n", argv[index]);
            return 3;
        }
    }

    if (bench_table != NULL) {
        return run_benchmark(bench_table, bench_runs);
    }

    if (file_input != NULL && sql_input != NULL) {
        fprintf(stderr, "[ERROR] CLI: use either -f or -e, not both\n");
        return 3;
    }

    if (file_input == NULL && sql_input == NULL) {
        print_help();
        return 0;
    }

    if (file_input != NULL) {
        script = read_file_contents(file_input);
        if (script == NULL) {
            fprintf(stderr, "[ERROR] CLI: failed to read %s\n", file_input);
            return 3;
        }
    } else {
        script = trim_copy(sql_input, strlen(sql_input));
        if (script == NULL) {
            fprintf(stderr, "[ERROR] CLI: failed to allocate SQL buffer\n");
            return 2;
        }
    }

    index = run_sql_script(script);
    free(script);
    return index;
}
