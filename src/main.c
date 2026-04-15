#include "config.h"
#include "executor.h"
#include "lexer.h"
#include "parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_help(void) {
    puts("Usage: ./sqlengine [-f file | -e sql] [-d data_dir] [-s schema_dir]");
    puts("  -f <file>     Execute SQL statements from file");
    puts("  -e <sql>      Execute SQL statements from string");
    puts("  -d <dir>      Set data directory (default: ./data)");
    puts("  -s <dir>      Set schema directory (default: ./schemas)");
    puts("  --help        Show this help message");
    puts("  --version     Show version");
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
    char *script = NULL;
    int index;

    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "-f") == 0) {
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
