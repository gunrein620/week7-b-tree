#include "lexer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *keyword;
    TokenType type;
} KeywordEntry;

static const KeywordEntry KEYWORDS[] = {
    {"SELECT", TOKEN_SELECT},
    {"INSERT", TOKEN_INSERT},
    {"INTO", TOKEN_INTO},
    {"FROM", TOKEN_FROM},
    {"WHERE", TOKEN_WHERE},
    {"ORDER", TOKEN_ORDER},
    {"BY", TOKEN_BY},
    {"ASC", TOKEN_ASC},
    {"DESC", TOKEN_DESC},
    {"VALUES", TOKEN_VALUES},
    {"AND", TOKEN_AND},
    {"OR", TOKEN_OR},
    {"NULL", TOKEN_NULL},
};

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

static int append_token(Token **tokens,
                        int *count,
                        int *capacity,
                        TokenType type,
                        const char *value,
                        int line) {
    Token *resized;

    if (*count >= *capacity) {
        *capacity = (*capacity == 0) ? 16 : (*capacity * 2);
        resized = (Token *)realloc(*tokens, (size_t)(*capacity) * sizeof(Token));
        if (resized == NULL) {
            fprintf(stderr, "[ERROR] Lexer: failed to allocate tokens\n");
            return -1;
        }
        *tokens = resized;
    }

    (*tokens)[*count].type = type;
    strncpy((*tokens)[*count].value, value, MAX_TOKEN_LEN - 1);
    (*tokens)[*count].value[MAX_TOKEN_LEN - 1] = '\0';
    (*tokens)[*count].line = line;
    (*count)++;
    return 0;
}

static TokenType keyword_type_for(const char *value) {
    size_t index;

    for (index = 0; index < sizeof(KEYWORDS) / sizeof(KEYWORDS[0]); ++index) {
        if (strcmp(KEYWORDS[index].keyword, value) == 0) {
            return KEYWORDS[index].type;
        }
    }

    return TOKEN_IDENT;
}

static int lexer_token_too_long(const char *kind, int line, int max_length) {
    fprintf(stderr,
            "[ERROR] Lexer: %s too long at line %d (max %d characters)\n",
            kind,
            line,
            max_length);
    return -1;
}

static int read_string_literal(const char *sql,
                               int *cursor,
                               int line,
                               Token **tokens,
                               int *count,
                               int *capacity) {
    char buffer[MAX_TOKEN_LEN];
    int length = 0;
    int index = *cursor + 1;

    while (sql[index] != '\0') {
        if (sql[index] == '\'') {
            if (sql[index + 1] == '\'') {
                if (length >= MAX_TOKEN_LEN - 1) {
                    return lexer_token_too_long("string literal", line, MAX_TOKEN_LEN - 1);
                }
                buffer[length++] = '\'';
                index += 2;
                continue;
            }

            buffer[length] = '\0';
            *cursor = index + 1;
            return append_token(tokens, count, capacity, TOKEN_STRING, buffer, line);
        }

        if (length >= MAX_TOKEN_LEN - 1) {
            return lexer_token_too_long("string literal", line, MAX_TOKEN_LEN - 1);
        }
        buffer[length++] = sql[index];
        index++;
    }

    fprintf(stderr, "[ERROR] Lexer: unterminated string literal at line %d\n", line);
    return -1;
}

static int read_number_literal(const char *sql,
                               int *cursor,
                               int line,
                               Token **tokens,
                               int *count,
                               int *capacity) {
    char buffer[MAX_TOKEN_LEN];
    int length = 0;
    int index = *cursor;
    int seen_dot = 0;
    int seen_digit = 0;

    if (sql[index] == '+' || sql[index] == '-') {
        if (length >= MAX_TOKEN_LEN - 1) {
            return lexer_token_too_long("number literal", line, MAX_TOKEN_LEN - 1);
        }
        buffer[length++] = sql[index];
        index++;
    }

    while (isdigit((unsigned char)sql[index]) || sql[index] == '.') {
        if (sql[index] == '.') {
            if (seen_dot) {
                break;
            }
            seen_dot = 1;
        } else {
            seen_digit = 1;
        }

        if (length >= MAX_TOKEN_LEN - 1) {
            return lexer_token_too_long("number literal", line, MAX_TOKEN_LEN - 1);
        }
        buffer[length++] = sql[index];
        index++;
    }

    if (!seen_digit) {
        return 0;
    }

    buffer[length] = '\0';
    *cursor = index;
    return append_token(tokens, count, capacity, TOKEN_NUMBER, buffer, line);
}

Token *tokenize(const char *sql, int *token_count) {
    Token *tokens = NULL;
    int count = 0;
    int capacity = 0;
    int cursor = 0;
    int line = 1;

    if (token_count == NULL) {
        return NULL;
    }
    *token_count = 0;

    if (sql == NULL) {
        fprintf(stderr, "[ERROR] Lexer: input SQL is NULL\n");
        return NULL;
    }

    while (sql[cursor] != '\0') {
        char ch = sql[cursor];

        if (ch == '\n') {
            line++;
            cursor++;
            continue;
        }

        if (isspace((unsigned char)ch)) {
            cursor++;
            continue;
        }

        if (ch == '-' && sql[cursor + 1] == '-') {
            cursor += 2;
            while (sql[cursor] != '\0' && sql[cursor] != '\n') {
                cursor++;
            }
            continue;
        }

        /* 식별자는 소문자로 통일해서 이후 단계 비교를 단순하게 만든다. */
        if (isalpha((unsigned char)ch) || ch == '_') {
            char raw[MAX_TOKEN_LEN];
            char upper[MAX_TOKEN_LEN];
            char lower[MAX_TOKEN_LEN];
            int length = 0;

            while (isalnum((unsigned char)sql[cursor]) || sql[cursor] == '_') {
                if (length >= MAX_IDENTIFIER_LEN - 1) {
                    fprintf(stderr,
                            "[ERROR] Lexer: identifier too long at line %d (max %d characters)\n",
                            line,
                            MAX_IDENTIFIER_LEN - 1);
                    free(tokens);
                    return NULL;
                }
                raw[length++] = sql[cursor];
                cursor++;
            }
            raw[length] = '\0';

            to_upper_copy(upper, raw, sizeof(upper));
            if (keyword_type_for(upper) == TOKEN_IDENT) {
                to_lower_copy(lower, raw, sizeof(lower));
                if (append_token(&tokens, &count, &capacity, TOKEN_IDENT, lower, line) != 0) {
                    free(tokens);
                    return NULL;
                }
            } else {
                if (append_token(&tokens,
                                 &count,
                                 &capacity,
                                 keyword_type_for(upper),
                                 upper,
                                 line) != 0) {
                    free(tokens);
                    return NULL;
                }
            }
            continue;
        }

        if (isdigit((unsigned char)ch) ||
            ((ch == '+' || ch == '-') && isdigit((unsigned char)sql[cursor + 1]))) {
            if (read_number_literal(sql, &cursor, line, &tokens, &count, &capacity) != 0) {
                free(tokens);
                return NULL;
            }
            continue;
        }

        if (ch == '\'') {
            if (read_string_literal(sql, &cursor, line, &tokens, &count, &capacity) != 0) {
                free(tokens);
                return NULL;
            }
            continue;
        }

        if (ch == '!' && sql[cursor + 1] == '=') {
            if (append_token(&tokens, &count, &capacity, TOKEN_NEQ, "!=", line) != 0) {
                free(tokens);
                return NULL;
            }
            cursor += 2;
            continue;
        }

        if (ch == '<' && sql[cursor + 1] == '>') {
            if (append_token(&tokens, &count, &capacity, TOKEN_NEQ, "<>", line) != 0) {
                free(tokens);
                return NULL;
            }
            cursor += 2;
            continue;
        }

        if (ch == '<' && sql[cursor + 1] == '=') {
            if (append_token(&tokens, &count, &capacity, TOKEN_LTE, "<=", line) != 0) {
                free(tokens);
                return NULL;
            }
            cursor += 2;
            continue;
        }

        if (ch == '>' && sql[cursor + 1] == '=') {
            if (append_token(&tokens, &count, &capacity, TOKEN_GTE, ">=", line) != 0) {
                free(tokens);
                return NULL;
            }
            cursor += 2;
            continue;
        }

        if (ch == '=') {
            if (append_token(&tokens, &count, &capacity, TOKEN_EQ, "=", line) != 0) {
                free(tokens);
                return NULL;
            }
            cursor++;
            continue;
        }

        if (ch == '<') {
            if (append_token(&tokens, &count, &capacity, TOKEN_LT, "<", line) != 0) {
                free(tokens);
                return NULL;
            }
            cursor++;
            continue;
        }

        if (ch == '>') {
            if (append_token(&tokens, &count, &capacity, TOKEN_GT, ">", line) != 0) {
                free(tokens);
                return NULL;
            }
            cursor++;
            continue;
        }

        if (ch == '*') {
            if (append_token(&tokens, &count, &capacity, TOKEN_STAR, "*", line) != 0) {
                free(tokens);
                return NULL;
            }
            cursor++;
            continue;
        }

        if (ch == ',') {
            if (append_token(&tokens, &count, &capacity, TOKEN_COMMA, ",", line) != 0) {
                free(tokens);
                return NULL;
            }
            cursor++;
            continue;
        }

        if (ch == '(') {
            if (append_token(&tokens, &count, &capacity, TOKEN_LPAREN, "(", line) != 0) {
                free(tokens);
                return NULL;
            }
            cursor++;
            continue;
        }

        if (ch == ')') {
            if (append_token(&tokens, &count, &capacity, TOKEN_RPAREN, ")", line) != 0) {
                free(tokens);
                return NULL;
            }
            cursor++;
            continue;
        }

        if (ch == ';') {
            if (append_token(&tokens, &count, &capacity, TOKEN_SEMICOLON, ";", line) != 0) {
                free(tokens);
                return NULL;
            }
            cursor++;
            continue;
        }

        fprintf(stderr, "[WARN] Lexer: skipping unsupported character '%c' at line %d\n", ch, line);
        cursor++;
    }

    if (append_token(&tokens, &count, &capacity, TOKEN_EOF, "", line) != 0) {
        free(tokens);
        return NULL;
    }

    *token_count = count;
    return tokens;
}

void free_tokens(Token *tokens) {
    free(tokens);
}
