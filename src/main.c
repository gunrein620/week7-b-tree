#include "btree.h"
#include "config.h"
#include "executor.h"
#include "index.h"
#include "lexer.h"
#include "parser.h"
#include "schema.h"
#include "storage.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define TokenType Win32TokenType
#include <windows.h>
#undef TokenType
#else
#include <time.h>
#endif

static void print_help(void) {
    puts("Usage: ./sqlengine [-f file | -e sql] [-d data_dir] [-s schema_dir]");
    puts("  -f <file>           Execute SQL statements from file");
    puts("  -e <sql>            Execute SQL statements from string");
    puts("  -d <dir>            Set data directory (default: ./data)");
    puts("  -s <dir>            Set schema directory (default: ./schemas)");
    puts("  --bench <table>     Compare indexed vs linear SELECT");
    puts("  --runs <n>          Repetitions for --bench (default 5)");
    puts("  --bulk-rows <n>     Extra benchmark: fetch first n rows via index vs linear scan");
    puts("  --bulk-pct <p>      Extra benchmark: fetch first p% rows (0 < p <= 100)");
    puts("  --bulk-sweep        Extra benchmark: scan multiple bulk sizes and report crossover");
    puts("  --help              Show this help message");
    puts("  --version           Show version");
}

#ifdef _WIN32
typedef LARGE_INTEGER bench_time_t;

static void bench_time_now(bench_time_t *timestamp) {
    QueryPerformanceCounter(timestamp);
}

static double timespec_diff_usec(bench_time_t start, bench_time_t end) {
    static LARGE_INTEGER frequency;
    static int frequency_loaded = 0;

    if (!frequency_loaded) {
        QueryPerformanceFrequency(&frequency);
        frequency_loaded = 1;
    }

    return (double)(end.QuadPart - start.QuadPart) * 1e6 / (double)frequency.QuadPart;
}
#else
typedef struct timespec bench_time_t;

static void bench_time_now(bench_time_t *timestamp) {
    clock_gettime(CLOCK_MONOTONIC, timestamp);
}

static double timespec_diff_usec(bench_time_t start, bench_time_t end) {
    double sec = (double)(end.tv_sec - start.tv_sec);
    double nsec = (double)(end.tv_nsec - start.tv_nsec);
    return sec * 1e6 + nsec / 1e3;
}
#endif

#define BENCH_BAR_WIDTH 36
#define BULK_SWEEP_BAR_WIDTH 10
#define BULK_SWEEP_MAX_POINTS 16

typedef struct {
    int enabled;
    size_t requested_rows;
    double requested_pct;
    int sweep_enabled;
} BulkBenchConfig;

static volatile uint64_t g_benchmark_sink = 0;

static int run_benchmark_split(const char *table_name, int runs, BulkBenchConfig bulk_config);

/* 인덱스 경로 vs 선형 스캔 경로를 같은 크기의 테이블에서 벤치마크한다.
 * - 인덱스 경로: btree_find + storage_read_row_at  (O(log n) + 1 seek)
 * - 선형 경로:   storage_select with WHERE name = ?  (O(n) 풀 스캔)
 * 두 쿼리 모두 "id=K 인 행" 을 가리키도록 key 와 name 을 동기화한다. */
static int run_benchmark(const char *table_name, int runs, BulkBenchConfig bulk_config) {
    return run_benchmark_split(table_name, runs, bulk_config);
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

static int run_single_statement_quiet(const char *sql) {
    Token *tokens;
    Statement *stmt;
    int token_count = 0;
    int exit_code = 0;
    int previous_output_enabled;

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

    previous_output_enabled = executor_set_output_enabled(0);
    if (execute(stmt) != 0) {
        exit_code = 2;
    }
    executor_set_output_enabled(previous_output_enabled);

    free_statement(stmt);
    free_tokens(tokens);
    return exit_code;
}

static int escape_sql_string_literal(char *buffer, size_t size, const char *value) {
    size_t write_index = 0;
    size_t read_index;

    if (size == 0) {
        return 0;
    }

    for (read_index = 0; value[read_index] != '\0'; ++read_index) {
        if (write_index + 2 >= size) {
            return 0;
        }
        if (value[read_index] == '\'') {
            buffer[write_index++] = '\'';
        }
        buffer[write_index++] = value[read_index];
    }

    buffer[write_index] = '\0';
    return 1;
}

static int build_select_sql(char *buffer,
                            size_t size,
                            const char *table_name,
                            const char *column_name,
                            ColumnType type,
                            const char *value) {
    if (type == COL_INT || type == COL_FLOAT) {
        return snprintf(buffer,
                        size,
                        "SELECT * FROM %s WHERE %s = %s;",
                        table_name,
                        column_name,
                        value) < (int)size;
    }

    {
        char escaped_value[MAX_TOKEN_LEN * 2];

        if (!escape_sql_string_literal(escaped_value, sizeof(escaped_value), value)) {
            return 0;
        }

        return snprintf(buffer,
                        size,
                        "SELECT * FROM %s WHERE %s = '%s';",
                        table_name,
                        column_name,
                        escaped_value) < (int)size;
    }
}

static int parse_size_argument(const char *text, size_t *out_value) {
    char *end_ptr;
    unsigned long long value;

    if (text == NULL || text[0] == '\0' || out_value == NULL) {
        return 0;
    }

    errno = 0;
    value = strtoull(text, &end_ptr, 10);
    if (errno != 0 || *end_ptr != '\0' || value == 0 ||
        value > (unsigned long long)SIZE_MAX) {
        return 0;
    }

    *out_value = (size_t)value;
    return 1;
}

static int parse_percent_argument(const char *text, double *out_value) {
    char *end_ptr;
    double value;

    if (text == NULL || text[0] == '\0' || out_value == NULL) {
        return 0;
    }

    errno = 0;
    value = strtod(text, &end_ptr);
    if (errno != 0 || *end_ptr != '\0' || value <= 0.0 || value > 100.0) {
        return 0;
    }

    *out_value = value;
    return 1;
}

static int benchmark_statement_once(const char *sql, double *out_usec) {
    bench_time_t t0;
    bench_time_t t1;
    int rc;

    bench_time_now(&t0);
    rc = run_single_statement_quiet(sql);
    bench_time_now(&t1);

    if (rc != 0) {
        return 0;
    }

    *out_usec = timespec_diff_usec(t0, t1);
    return 1;
}

static int benchmark_statement_avg(const char *sql, int runs, double *out_avg_usec) {
    double total_usec = 0.0;
    int index;

    for (index = 0; index < runs; ++index) {
        double elapsed_usec;

        if (!benchmark_statement_once(sql, &elapsed_usec)) {
            return 0;
        }
        total_usec += elapsed_usec;
    }

    *out_avg_usec = total_usec / runs;
    return 1;
}

static uint64_t benchmark_consume_line(const char *line) {
    uint64_t checksum = 1469598103934665603ULL;
    size_t index;

    for (index = 0; line[index] != '\0' && line[index] != '\n' && line[index] != '\r'; ++index) {
        checksum ^= (uint64_t)(unsigned char)line[index];
        checksum *= 1099511628211ULL;
    }

    return checksum ^ (uint64_t)index;
}

static int benchmark_index_core_avg(BTree *tree,
                                    const char *table_name,
                                    Schema *schema,
                                    int32_t target_key,
                                    int runs,
                                    double *out_avg_usec) {
    double total_usec = 0.0;
    int index;

    for (index = 0; index < runs; ++index) {
        int64_t offset = -1;
        Row row;
        bench_time_t t0;
        bench_time_t t1;

        bench_time_now(&t0);
        if (btree_find(tree, target_key, &offset)) {
            if (!storage_read_row_at(table_name, schema, offset, &row)) {
                return 0;
            }
        }
        bench_time_now(&t1);
        total_usec += timespec_diff_usec(t0, t1);
    }

    *out_avg_usec = total_usec / runs;
    return 1;
}

static int benchmark_linear_core_avg(const char *table_name,
                                     Schema *schema,
                                     const char *column_name,
                                     const char *value,
                                     int runs,
                                     double *out_avg_usec) {
    double total_usec = 0.0;
    int index;
    ColumnList star;
    WhereClause where;

    memset(&star, 0, sizeof(star));
    star.is_star = 1;

    memset(&where, 0, sizeof(where));
    where.condition_count = 1;
    strncpy(where.conditions[0].column_name,
            column_name,
            sizeof(where.conditions[0].column_name) - 1);
    strncpy(where.conditions[0].operator,
            "=",
            sizeof(where.conditions[0].operator) - 1);
    strncpy(where.conditions[0].value,
            value,
            sizeof(where.conditions[0].value) - 1);

    for (index = 0; index < runs; ++index) {
        ResultSet *result;
        bench_time_t t0;
        bench_time_t t1;

        bench_time_now(&t0);
        result = storage_select(table_name, schema, &star, &where);
        bench_time_now(&t1);
        if (result == NULL) {
            return 0;
        }
        free_result_set(result);
        total_usec += timespec_diff_usec(t0, t1);
    }

    *out_avg_usec = total_usec / runs;
    return 1;
}

typedef struct {
    FILE *file;
    size_t rows_read;
    uint64_t checksum;
    int failed;
} BulkIndexVisitContext;

static int benchmark_index_bulk_visit(int32_t key, int64_t offset, void *context) {
    BulkIndexVisitContext *ctx = (BulkIndexVisitContext *)context;
    char line[4096];

    (void)key;

    if (ctx == NULL || ctx->file == NULL) {
        return 0;
    }
    if (fseek(ctx->file, (long)offset, SEEK_SET) != 0) {
        ctx->failed = 1;
        return 0;
    }
    if (fgets(line, sizeof(line), ctx->file) == NULL) {
        ctx->failed = 1;
        return 0;
    }

    ctx->checksum ^= benchmark_consume_line(line);
    ctx->rows_read++;
    return 1;
}

static int benchmark_index_bulk_core_avg(BTree *tree,
                                         const char *table_name,
                                         int32_t start_key,
                                         size_t target_rows,
                                         int runs,
                                         double *out_avg_usec) {
    char path[MAX_PATH_LEN];
    double total_usec = 0.0;
    int index;

    snprintf(path, sizeof(path), "%s/%s.tbl", g_data_dir, table_name);

    for (index = 0; index < runs; ++index) {
        FILE *file = fopen(path, "r");
        BulkIndexVisitContext context;
        size_t visited;
        bench_time_t t0;
        bench_time_t t1;

        if (file == NULL) {
            return 0;
        }

        memset(&context, 0, sizeof(context));
        context.file = file;

        bench_time_now(&t0);
        visited = btree_visit_from(tree,
                                   start_key,
                                   target_rows,
                                   benchmark_index_bulk_visit,
                                   &context);
        bench_time_now(&t1);

        fclose(file);

        if (context.failed || visited != target_rows || context.rows_read != target_rows) {
            return 0;
        }

        g_benchmark_sink ^= context.checksum;
        total_usec += timespec_diff_usec(t0, t1);
    }

    *out_avg_usec = total_usec / runs;
    return 1;
}

static int benchmark_linear_bulk_core_avg(const char *table_name,
                                          size_t start_row_offset,
                                          size_t target_rows,
                                          int runs,
                                          double *out_avg_usec) {
    char path[MAX_PATH_LEN];
    double total_usec = 0.0;
    int index;

    snprintf(path, sizeof(path), "%s/%s.tbl", g_data_dir, table_name);

    for (index = 0; index < runs; ++index) {
        FILE *file = fopen(path, "r");
        char line[4096];
        size_t skipped_rows = 0;
        size_t rows_read = 0;
        uint64_t checksum = 0;
        bench_time_t t0;
        bench_time_t t1;

        if (file == NULL) {
            return 0;
        }

        if (fgets(line, sizeof(line), file) == NULL) {
            fclose(file);
            return 0;
        }

        bench_time_now(&t0);
        while (skipped_rows < start_row_offset && fgets(line, sizeof(line), file) != NULL) {
            skipped_rows++;
        }
        while (rows_read < target_rows && fgets(line, sizeof(line), file) != NULL) {
            checksum ^= benchmark_consume_line(line);
            rows_read++;
        }
        bench_time_now(&t1);

        fclose(file);

        if (skipped_rows != start_row_offset || rows_read != target_rows) {
            return 0;
        }

        g_benchmark_sink ^= checksum;
        total_usec += timespec_diff_usec(t0, t1);
    }

    *out_avg_usec = total_usec / runs;
    return 1;
}

static void format_benchmark_duration(double usec, char *buffer, size_t size) {
    if (usec >= 1000000.0) {
        snprintf(buffer, size, "%.2f s", usec / 1000000.0);
    } else if (usec >= 1000.0) {
        snprintf(buffer, size, "%.2f ms", usec / 1000.0);
    } else {
        snprintf(buffer, size, "%.2f us", usec);
    }
}

static void build_benchmark_bar(char *buffer, size_t size, double value, double max_value) {
    int filled = 0;
    int index;

    if (size == 0) {
        return;
    }
    if (size <= (size_t)BENCH_BAR_WIDTH) {
        buffer[0] = '\0';
        return;
    }

    if (max_value > 0.0 && value > 0.0) {
        double ratio = value / max_value;

        if (ratio > 1.0) {
            ratio = 1.0;
        }
        filled = (int)(ratio * BENCH_BAR_WIDTH + 0.5);
        if (filled < 1) {
            filled = 1;
        }
        if (filled > BENCH_BAR_WIDTH) {
            filled = BENCH_BAR_WIDTH;
        }
    }

    for (index = 0; index < BENCH_BAR_WIDTH; ++index) {
        buffer[index] = (index < filled) ? '#' : '.';
    }
    buffer[BENCH_BAR_WIDTH] = '\0';
}

static void print_benchmark_bar_line(const char *label, double value_usec, double max_value_usec) {
    char bar[BENCH_BAR_WIDTH + 1];
    char duration[32];

    build_benchmark_bar(bar, sizeof(bar), value_usec, max_value_usec);
    format_benchmark_duration(value_usec, duration, sizeof(duration));
    printf("[BENCH]   %-7s [%s] %10s\n", label, bar, duration);
}

static void print_benchmark_faster_line(const char *first_label,
                                        double first_usec,
                                        const char *second_label,
                                        double second_usec) {
    const char *winner = first_label;
    double factor = 1.0;

    if (first_usec <= 0.0 || second_usec <= 0.0) {
        printf("[BENCH]   faster  n/a\n");
        return;
    }

    if (second_usec < first_usec) {
        winner = second_label;
        factor = first_usec / second_usec;
    } else {
        factor = second_usec / first_usec;
    }

    printf("[BENCH]   faster  %-7s %10.2fx\n", winner, factor);
}

static void print_benchmark_group(const char *title,
                                  const char *indexed_label,
                                  double indexed_usec,
                                  const char *linear_label,
                                  double linear_usec) {
    double max_value = (indexed_usec > linear_usec) ? indexed_usec : linear_usec;

    printf("[BENCH] %s\n", title);
    print_benchmark_bar_line(indexed_label, indexed_usec, max_value);
    print_benchmark_bar_line(linear_label, linear_usec, max_value);
    print_benchmark_faster_line(indexed_label, indexed_usec, linear_label, linear_usec);
}

typedef struct {
    size_t rows;
    double share_pct;
    double indexed_usec;
    double linear_usec;
} BulkSweepSample;

static void build_progress_bar(char *buffer, size_t size, double ratio) {
    int filled = 0;
    int index;

    if (size == 0) {
        return;
    }
    if (size <= (size_t)BULK_SWEEP_BAR_WIDTH) {
        buffer[0] = '\0';
        return;
    }

    if (ratio < 0.0) {
        ratio = 0.0;
    }
    if (ratio > 1.0) {
        ratio = 1.0;
    }

    filled = (int)(ratio * BULK_SWEEP_BAR_WIDTH + 0.5);
    if (ratio > 0.0 && filled < 1) {
        filled = 1;
    }
    if (filled > BULK_SWEEP_BAR_WIDTH) {
        filled = BULK_SWEEP_BAR_WIDTH;
    }

    for (index = 0; index < BULK_SWEEP_BAR_WIDTH; ++index) {
        buffer[index] = (index < filled) ? '#' : '.';
    }
    buffer[BULK_SWEEP_BAR_WIDTH] = '\0';
}

static void get_benchmark_winner(double indexed_usec,
                                 double linear_usec,
                                 const char **out_winner,
                                 double *out_factor) {
    if (indexed_usec <= 0.0 || linear_usec <= 0.0) {
        *out_winner = "n/a";
        *out_factor = 0.0;
        return;
    }

    if (indexed_usec <= linear_usec) {
        *out_winner = "indexed";
        *out_factor = linear_usec / indexed_usec;
    } else {
        *out_winner = "linear";
        *out_factor = indexed_usec / linear_usec;
    }
}

static int append_bulk_sweep_target(size_t *targets,
                                    int *count,
                                    size_t candidate,
                                    size_t total_rows) {
    int index;

    if (targets == NULL || count == NULL || candidate == 0 || total_rows == 0) {
        return 0;
    }
    if (candidate > total_rows) {
        return 1;
    }
    for (index = 0; index < *count; ++index) {
        if (targets[index] == candidate) {
            return 1;
        }
    }
    if (*count >= BULK_SWEEP_MAX_POINTS) {
        return 0;
    }

    targets[*count] = candidate;
    (*count)++;
    return 1;
}

static int build_bulk_sweep_targets(size_t total_rows, size_t *targets, int *out_count) {
    static const size_t CANDIDATES[] = {
        1, 10, 100, 1000, 5000, 10000, 20000, 50000, 100000, 200000, 500000
    };
    int index;
    int count = 0;

    if (targets == NULL || out_count == NULL || total_rows == 0) {
        return 0;
    }

    for (index = 0; index < (int)(sizeof(CANDIDATES) / sizeof(CANDIDATES[0])); ++index) {
        if (!append_bulk_sweep_target(targets, &count, CANDIDATES[index], total_rows)) {
            return 0;
        }
    }

    if (!append_bulk_sweep_target(targets, &count, total_rows, total_rows)) {
        return 0;
    }

    *out_count = count;
    return count > 0;
}

static void resolve_bulk_window(size_t total_rows,
                                int32_t center_key,
                                size_t target_rows,
                                int32_t *out_start_key,
                                size_t *out_start_row_offset) {
    long long start_key = 1;
    size_t max_start_row_offset = 0;

    if (out_start_key == NULL || out_start_row_offset == NULL || total_rows == 0) {
        return;
    }

    if (target_rows >= total_rows) {
        *out_start_key = 1;
        *out_start_row_offset = 0;
        return;
    }

    start_key = (long long)center_key - (long long)(target_rows / 2);
    if (start_key < 1) {
        start_key = 1;
    }

    max_start_row_offset = total_rows - target_rows;
    if ((size_t)(start_key - 1) > max_start_row_offset) {
        start_key = (long long)max_start_row_offset + 1;
    }

    *out_start_key = (int32_t)start_key;
    *out_start_row_offset = (size_t)(start_key - 1);
}

static void print_bulk_sweep_summary(const BulkSweepSample *samples, int sample_count) {
    int index;
    int first_linear_index = -1;
    int previous_indexed_index = -1;

    printf("[BENCH] bulk sweep (avg over repeated runs, mode=center_window_pk_asc)\n");
    for (index = 0; index < sample_count; ++index) {
        char progress_bar[BULK_SWEEP_BAR_WIDTH + 1];
        char indexed_duration[32];
        char linear_duration[32];
        const char *winner;
        double factor;

        build_progress_bar(progress_bar,
                           sizeof(progress_bar),
                           samples[index].share_pct / 100.0);
        format_benchmark_duration(samples[index].indexed_usec,
                                  indexed_duration,
                                  sizeof(indexed_duration));
        format_benchmark_duration(samples[index].linear_usec,
                                  linear_duration,
                                  sizeof(linear_duration));
        get_benchmark_winner(samples[index].indexed_usec,
                             samples[index].linear_usec,
                             &winner,
                             &factor);

        printf("[BENCH]   [%s] rows=%-7zu share=%6.2f%% faster=%-7s %8.2fx  (%s / %s)\n",
               progress_bar,
               samples[index].rows,
               samples[index].share_pct,
               winner,
               factor,
               indexed_duration,
               linear_duration);

        if (strcmp(winner, "indexed") == 0) {
            previous_indexed_index = index;
        } else if (strcmp(winner, "linear") == 0 && first_linear_index < 0) {
            first_linear_index = index;
        }
    }

    if (first_linear_index < 0) {
        printf("[BENCH]   crossover not observed in sampled sizes\n");
        return;
    }

    if (previous_indexed_index >= 0 && previous_indexed_index < first_linear_index) {
        printf("[BENCH]   crossover between rows=%zu and rows=%zu\n",
               samples[previous_indexed_index].rows,
               samples[first_linear_index].rows);
        return;
    }

    printf("[BENCH]   linear first wins at rows=%zu share=%.2f%%\n",
           samples[first_linear_index].rows,
           samples[first_linear_index].share_pct);
}

static int run_bulk_sweep(BTree *tree,
                          const char *table_name,
                          size_t total_rows,
                          int32_t center_key,
                          int runs) {
    size_t targets[BULK_SWEEP_MAX_POINTS];
    BulkSweepSample samples[BULK_SWEEP_MAX_POINTS];
    int sample_count = 0;
    int index;

    if (tree == NULL || table_name == NULL || total_rows == 0) {
        return 0;
    }
    if (!build_bulk_sweep_targets(total_rows, targets, &sample_count)) {
        return 0;
    }

    for (index = 0; index < sample_count; ++index) {
        int32_t start_key;
        size_t start_row_offset;

        samples[index].rows = targets[index];
        samples[index].share_pct = (double)targets[index] * 100.0 / (double)total_rows;
        resolve_bulk_window(total_rows,
                            center_key,
                            targets[index],
                            &start_key,
                            &start_row_offset);
        if (!benchmark_index_bulk_core_avg(tree,
                                           table_name,
                                           start_key,
                                           targets[index],
                                           runs,
                                           &samples[index].indexed_usec)) {
            return 0;
        }
        if (!benchmark_linear_bulk_core_avg(table_name,
                                            start_row_offset,
                                            targets[index],
                                            runs,
                                            &samples[index].linear_usec)) {
            return 0;
        }
    }

    print_bulk_sweep_summary(samples, sample_count);
    return 1;
}

static int resolve_bulk_benchmark_target(size_t total_rows,
                                         BulkBenchConfig bulk_config,
                                         size_t *out_target_rows,
                                         double *out_share_pct) {
    size_t target_rows = 0;

    if (!bulk_config.enabled || total_rows == 0 ||
        out_target_rows == NULL || out_share_pct == NULL) {
        return 0;
    }

    if (bulk_config.requested_rows > 0) {
        target_rows = bulk_config.requested_rows;
    } else if (bulk_config.requested_pct > 0.0) {
        double raw_rows = (double)total_rows * bulk_config.requested_pct / 100.0;
        target_rows = (size_t)raw_rows;
        if ((double)target_rows < raw_rows) {
            target_rows++;
        }
        if (target_rows == 0) {
            target_rows = 1;
        }
    } else {
        return 0;
    }

    if (target_rows > total_rows) {
        target_rows = total_rows;
    }

    *out_target_rows = target_rows;
    *out_share_pct = (double)target_rows * 100.0 / (double)total_rows;
    return 1;
}

static int run_benchmark_split(const char *table_name, int runs, BulkBenchConfig bulk_config) {
    Schema *schema;
    BTree *tree;
    Row target_row;
    int64_t target_offset = -1;
    int32_t max_key;
    int32_t target_key;
    int pk_index;
    int linear_index;
    char indexed_sql[256];
    char linear_sql[512];
    char linear_value[MAX_TOKEN_LEN];
    double build_usec;
    double cold_indexed_e2e_usec;
    double cold_linear_e2e_usec;
    double warm_indexed_e2e_usec;
    double warm_linear_e2e_usec;
    double cold_indexed_core_usec;
    double cold_linear_core_usec;
    double warm_indexed_core_usec;
    double warm_linear_core_usec;
    size_t bulk_target_rows = 0;
    double bulk_share_pct = 0.0;
    double bulk_indexed_core_usec = 0.0;
    double bulk_linear_core_usec = 0.0;
    bench_time_t t0;
    bench_time_t t1;
    double build_scale_usec;
    int run_point_bench;
    int index;

    schema = schema_load(table_name);
    if (schema == NULL) {
        fprintf(stderr, "[BENCH] failed to load schema '%s'\n", table_name);
        return 1;
    }

    pk_index = -1;
    for (index = 0; index < schema->column_count; ++index) {
        if (schema->columns[index].is_primary_key) {
            pk_index = index;
            break;
        }
    }
    if (pk_index < 0 || schema->columns[pk_index].type != COL_INT) {
        fprintf(stderr, "[BENCH] table '%s' has no INT primary key\n", table_name);
        schema_free(schema);
        return 1;
    }

    linear_index = schema_get_column_index(schema, "name");
    if (linear_index < 0 || linear_index == pk_index) {
        fprintf(stderr,
                "[BENCH] table '%s' needs a non-PK 'name' column for linear benchmark\n",
                table_name);
        schema_free(schema);
        return 1;
    }

    bench_time_now(&t0);
    tree = index_get_or_build(table_name, schema);
    bench_time_now(&t1);
    if (tree == NULL) {
        fprintf(stderr, "[BENCH] failed to build index\n");
        schema_free(schema);
        return 1;
    }
    build_usec = timespec_diff_usec(t0, t1);

    max_key = btree_max_key(tree);
    if (max_key <= 0) {
        fprintf(stderr, "[BENCH] table is empty\n");
        schema_free(schema);
        return 1;
    }

    target_key = max_key / 2;
    if (target_key <= 0) {
        target_key = 1;
    }

    if (!btree_find(tree, target_key, &target_offset) ||
        !storage_read_row_at(table_name, schema, target_offset, &target_row)) {
        fprintf(stderr, "[BENCH] failed to load target row for key %d\n", (int)target_key);
        schema_free(schema);
        return 1;
    }

    strncpy(linear_value,
            target_row.data[linear_index],
            sizeof(linear_value) - 1);
    linear_value[sizeof(linear_value) - 1] = '\0';

    if (!build_select_sql(indexed_sql,
                          sizeof(indexed_sql),
                          table_name,
                          schema->columns[pk_index].name,
                          schema->columns[pk_index].type,
                          target_row.data[pk_index]) ||
        !build_select_sql(linear_sql,
                          sizeof(linear_sql),
                          table_name,
                          schema->columns[linear_index].name,
                          schema->columns[linear_index].type,
                          linear_value)) {
        fprintf(stderr, "[BENCH] failed to build benchmark SQL\n");
        schema_free(schema);
        return 1;
    }

    printf("[BENCH] table=%s rows=%zu build_only=%.1f us\n",
           table_name,
           btree_size(tree),
           build_usec);
    run_point_bench = (!bulk_config.enabled && !bulk_config.sweep_enabled);

    if (run_point_bench) {
        printf("[BENCH] target pk=%s value=%s linear=%s value='%s' warm_runs=%d\n",
               schema->columns[pk_index].name,
               target_row.data[pk_index],
               schema->columns[linear_index].name,
               linear_value,
               runs);

        index_drop_all();

        if (!benchmark_statement_once(indexed_sql, &cold_indexed_e2e_usec)) {
            fprintf(stderr, "[BENCH] cold indexed end-to-end benchmark failed\n");
            schema_free(schema);
            return 1;
        }
        if (!benchmark_statement_once(linear_sql, &cold_linear_e2e_usec)) {
            fprintf(stderr, "[BENCH] cold linear end-to-end benchmark failed\n");
            schema_free(schema);
            return 1;
        }

        tree = index_get_or_build(table_name, schema);
        if (tree == NULL) {
            fprintf(stderr, "[BENCH] failed to reuse warm index\n");
            schema_free(schema);
            return 1;
        }

        if (!benchmark_index_core_avg(tree,
                                      table_name,
                                      schema,
                                      target_key,
                                      1,
                                      &cold_indexed_core_usec)) {
            fprintf(stderr, "[BENCH] cold indexed core benchmark failed\n");
            schema_free(schema);
            return 1;
        }
        if (!benchmark_linear_core_avg(table_name,
                                       schema,
                                       schema->columns[linear_index].name,
                                       linear_value,
                                       1,
                                       &cold_linear_core_usec)) {
            fprintf(stderr, "[BENCH] cold linear core benchmark failed\n");
            schema_free(schema);
            return 1;
        }

        if (!benchmark_statement_avg(indexed_sql, runs, &warm_indexed_e2e_usec)) {
            fprintf(stderr, "[BENCH] warm indexed end-to-end benchmark failed\n");
            schema_free(schema);
            return 1;
        }
        if (!benchmark_statement_avg(linear_sql, runs, &warm_linear_e2e_usec)) {
            fprintf(stderr, "[BENCH] warm linear end-to-end benchmark failed\n");
            schema_free(schema);
            return 1;
        }
        if (!benchmark_index_core_avg(tree,
                                      table_name,
                                      schema,
                                      target_key,
                                      runs,
                                      &warm_indexed_core_usec)) {
            fprintf(stderr, "[BENCH] warm indexed core benchmark failed\n");
            schema_free(schema);
            return 1;
        }
        if (!benchmark_linear_core_avg(table_name,
                                       schema,
                                       schema->columns[linear_index].name,
                                       linear_value,
                                       runs,
                                       &warm_linear_core_usec)) {
            fprintf(stderr, "[BENCH] warm linear core benchmark failed\n");
            schema_free(schema);
            return 1;
        }

        build_scale_usec = build_usec;
        if (cold_linear_e2e_usec > build_scale_usec) {
            build_scale_usec = cold_linear_e2e_usec;
        }
        if (warm_linear_e2e_usec > build_scale_usec) {
            build_scale_usec = warm_linear_e2e_usec;
        }
        if (cold_linear_core_usec > build_scale_usec) {
            build_scale_usec = cold_linear_core_usec;
        }
        if (warm_linear_core_usec > build_scale_usec) {
            build_scale_usec = warm_linear_core_usec;
        }

        printf("[BENCH] build only\n");
        print_benchmark_bar_line("build", build_usec, build_scale_usec);
        print_benchmark_group("cold e2e (1 run, lazy build included)",
                              "indexed",
                              cold_indexed_e2e_usec,
                              "linear",
                              cold_linear_e2e_usec);
        print_benchmark_group("warm e2e (avg over repeated runs)",
                              "indexed",
                              warm_indexed_e2e_usec,
                              "linear",
                              warm_linear_e2e_usec);
        print_benchmark_group("cold core (1 run)",
                              "indexed",
                              cold_indexed_core_usec,
                              "linear",
                              cold_linear_core_usec);
        print_benchmark_group("warm core (avg over repeated runs)",
                              "indexed",
                              warm_indexed_core_usec,
                              "linear",
                              warm_linear_core_usec);
    } else {
        printf("[BENCH] bulk warm_runs=%d\n", runs);
        printf("[BENCH] build only\n");
        print_benchmark_bar_line("build", build_usec, build_usec);
    }

    if (resolve_bulk_benchmark_target(btree_size(tree),
                                      bulk_config,
                                      &bulk_target_rows,
                                      &bulk_share_pct)) {
        int32_t bulk_start_key;
        size_t bulk_start_row_offset;

        resolve_bulk_window(btree_size(tree),
                            target_key,
                            bulk_target_rows,
                            &bulk_start_key,
                            &bulk_start_row_offset);
        if (!benchmark_index_bulk_core_avg(tree,
                                           table_name,
                                           bulk_start_key,
                                           bulk_target_rows,
                                           runs,
                                           &bulk_indexed_core_usec)) {
            fprintf(stderr, "[BENCH] indexed bulk benchmark failed\n");
            schema_free(schema);
            return 1;
        }
        if (!benchmark_linear_bulk_core_avg(table_name,
                                            bulk_start_row_offset,
                                            bulk_target_rows,
                                            runs,
                                            &bulk_linear_core_usec)) {
            fprintf(stderr, "[BENCH] linear bulk benchmark failed\n");
            schema_free(schema);
            return 1;
        }

        printf("[BENCH] bulk target rows=%zu share=%.2f%% start_pk=%d mode=center_window_pk_asc\n",
               bulk_target_rows,
               bulk_share_pct,
               bulk_start_key);
        print_benchmark_group("bulk fetch core (avg over repeated runs)",
                              "indexed",
                              bulk_indexed_core_usec,
                              "linear",
                              bulk_linear_core_usec);
    }

    if (bulk_config.sweep_enabled) {
        if (!run_bulk_sweep(tree, table_name, btree_size(tree), target_key, runs)) {
            fprintf(stderr, "[BENCH] bulk sweep benchmark failed\n");
            schema_free(schema);
            return 1;
        }
    }

    schema_free(schema);
    index_drop_all();
    return 0;
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
    BulkBenchConfig bulk_config;
    char *script = NULL;
    int index;

    memset(&bulk_config, 0, sizeof(bulk_config));

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
        } else if (strcmp(argv[index], "--bulk-rows") == 0) {
            if (index + 1 >= argc) {
                fprintf(stderr, "[ERROR] CLI: missing number after --bulk-rows\n");
                return 3;
            }
            if (!parse_size_argument(argv[++index], &bulk_config.requested_rows)) {
                fprintf(stderr, "[ERROR] CLI: invalid value for --bulk-rows\n");
                return 3;
            }
            bulk_config.enabled = 1;
        } else if (strcmp(argv[index], "--bulk-pct") == 0) {
            if (index + 1 >= argc) {
                fprintf(stderr, "[ERROR] CLI: missing number after --bulk-pct\n");
                return 3;
            }
            if (!parse_percent_argument(argv[++index], &bulk_config.requested_pct)) {
                fprintf(stderr, "[ERROR] CLI: invalid value for --bulk-pct\n");
                return 3;
            }
            bulk_config.enabled = 1;
        } else if (strcmp(argv[index], "--bulk-sweep") == 0) {
            bulk_config.sweep_enabled = 1;
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

    if (bulk_config.requested_rows > 0 && bulk_config.requested_pct > 0.0) {
        fprintf(stderr, "[ERROR] CLI: use either --bulk-rows or --bulk-pct, not both\n");
        return 3;
    }
    if ((bulk_config.enabled || bulk_config.sweep_enabled) && bench_table == NULL) {
        fprintf(stderr, "[ERROR] CLI: --bulk-rows/--bulk-pct/--bulk-sweep require --bench\n");
        return 3;
    }

    if (bench_table != NULL) {
        return run_benchmark(bench_table, bench_runs, bulk_config);
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
