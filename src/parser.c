#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Token *tokens;
    int token_count;
    int current;
} Parser;

static Token *peek(Parser *parser) {
    if (parser->current >= parser->token_count) {
        return &parser->tokens[parser->token_count - 1];
    }
    return &parser->tokens[parser->current];
}

static Token *previous(Parser *parser) {
    if (parser->current == 0) {
        return &parser->tokens[0];
    }
    return &parser->tokens[parser->current - 1];
}

static int is_at_end(Parser *parser) {
    return peek(parser)->type == TOKEN_EOF;
}

static Token *advance_token(Parser *parser) {
    if (!is_at_end(parser)) {
        parser->current++;
    }
    return previous(parser);
}

static int check(Parser *parser, TokenType type) {
    return peek(parser)->type == type;
}

static int match(Parser *parser, TokenType type) {
    if (!check(parser, type)) {
        return 0;
    }
    advance_token(parser);
    return 1;
}

static void parser_error(Parser *parser, const char *message) {
    Token *token = peek(parser);
    fprintf(stderr,
            "[ERROR] Parser: %s near '%s' at line %d\n",
            message,
            token->value[0] == '\0' ? "EOF" : token->value,
            token->line);
}

static int consume(Parser *parser, TokenType type, const char *message) {
    if (check(parser, type)) {
        advance_token(parser);
        return 1;
    }
    parser_error(parser, message);
    return 0;
}

static int parse_identifier(Parser *parser, char *dest, size_t size, const char *message) {
    if (!consume(parser, TOKEN_IDENT, message)) {
        return 0;
    }

    strncpy(dest, previous(parser)->value, size - 1);
    dest[size - 1] = '\0';
    return 1;
}

static int parse_value(Parser *parser, Statement *stmt, int index) {
    Token *token = peek(parser);

    if (token->type == TOKEN_STRING || token->type == TOKEN_NUMBER) {
        strncpy(stmt->values[index], token->value, MAX_TOKEN_LEN - 1);
        stmt->values[index][MAX_TOKEN_LEN - 1] = '\0';
        stmt->value_is_null[index] = 0;
        advance_token(parser);
        return 1;
    }

    if (token->type == TOKEN_NULL) {
        stmt->values[index][0] = '\0';
        stmt->value_is_null[index] = 1;
        advance_token(parser);
        return 1;
    }

    parser_error(parser, "expected value");
    return 0;
}

static int parse_condition(Parser *parser, Condition *condition) {
    Token *operator_token;
    Token *value_token;

    if (!parse_identifier(parser,
                          condition->column_name,
                          sizeof(condition->column_name),
                          "expected column name in WHERE")) {
        return 0;
    }

    operator_token = peek(parser);
    if (!(match(parser, TOKEN_EQ) || match(parser, TOKEN_NEQ) || match(parser, TOKEN_LT) ||
          match(parser, TOKEN_GT) || match(parser, TOKEN_LTE) || match(parser, TOKEN_GTE))) {
        parser_error(parser, "expected comparison operator");
        return 0;
    }
    strncpy(condition->operator, operator_token->value, sizeof(condition->operator) - 1);
    condition->operator[sizeof(condition->operator) - 1] = '\0';

    value_token = peek(parser);
    if (!(match(parser, TOKEN_STRING) || match(parser, TOKEN_NUMBER))) {
        parser_error(parser, "expected literal value in WHERE");
        return 0;
    }
    strncpy(condition->value, value_token->value, sizeof(condition->value) - 1);
    condition->value[sizeof(condition->value) - 1] = '\0';
    return 1;
}

static int parse_where_clause(Parser *parser, Statement *stmt) {
    TokenType logical_type = TOKEN_UNKNOWN;
    const char *logical_text = "";

    if (!match(parser, TOKEN_WHERE)) {
        stmt->where.condition_count = 0;
        stmt->where.logical_op[0] = '\0';
        return 1;
    }

    if (!parse_condition(parser, &stmt->where.conditions[0])) {
        return 0;
    }
    stmt->where.condition_count = 1;

    /* v1은 괄호/우선순위 없이 평면 WHERE만 다룬다. */
    while (match(parser, TOKEN_AND) || match(parser, TOKEN_OR)) {
        Token *logical_token = previous(parser);

        if (logical_type == TOKEN_UNKNOWN) {
            logical_type = logical_token->type;
            logical_text = logical_token->value;
            strncpy(stmt->where.logical_op, logical_text, sizeof(stmt->where.logical_op) - 1);
            stmt->where.logical_op[sizeof(stmt->where.logical_op) - 1] = '\0';
        } else if (logical_type != logical_token->type) {
            parser_error(parser, "mixed AND/OR is not supported in one WHERE clause");
            return 0;
        }

        if (stmt->where.condition_count >= MAX_CONDITIONS) {
            parser_error(parser, "too many WHERE conditions");
            return 0;
        }

        if (!parse_condition(parser, &stmt->where.conditions[stmt->where.condition_count])) {
            return 0;
        }
        stmt->where.condition_count++;
    }

    return 1;
}

static int parse_order_by_clause(Parser *parser, Statement *stmt) {
    if (!match(parser, TOKEN_ORDER)) {
        stmt->order_by.enabled = 0;
        stmt->order_by.column_name[0] = '\0';
        stmt->order_by.direction = SORT_ASC;
        return 1;
    }

    stmt->order_by.enabled = 1;
    stmt->order_by.direction = SORT_ASC;

    if (!consume(parser, TOKEN_BY, "expected BY after ORDER")) {
        return 0;
    }

    if (!parse_identifier(parser,
                          stmt->order_by.column_name,
                          sizeof(stmt->order_by.column_name),
                          "expected column name after ORDER BY")) {
        return 0;
    }

    if (match(parser, TOKEN_ASC)) {
        stmt->order_by.direction = SORT_ASC;
    } else if (match(parser, TOKEN_DESC)) {
        stmt->order_by.direction = SORT_DESC;
    }

    if (check(parser, TOKEN_COMMA)) {
        parser_error(parser, "multiple ORDER BY columns are not supported");
        return 0;
    }

    return 1;
}

static int parse_select(Parser *parser, Statement *stmt) {
    stmt->type = STMT_SELECT;

    /* SELECT * 와 컬럼 나열을 여기서 갈라서 처리한다. */
    if (match(parser, TOKEN_STAR)) {
        stmt->select_columns.is_star = 1;
        stmt->select_columns.count = 0;
    } else {
        stmt->select_columns.is_star = 0;
        stmt->select_columns.count = 0;

        do {
            if (stmt->select_columns.count >= MAX_COLUMNS) {
                parser_error(parser, "too many SELECT columns");
                return 0;
            }

            if (!parse_identifier(parser,
                                  stmt->select_columns.names[stmt->select_columns.count],
                                  sizeof(stmt->select_columns.names[stmt->select_columns.count]),
                                  "expected column name after SELECT")) {
                return 0;
            }
            stmt->select_columns.count++;
        } while (match(parser, TOKEN_COMMA));
    }

    if (!consume(parser, TOKEN_FROM, "expected FROM")) {
        return 0;
    }

    if (!parse_identifier(parser,
                          stmt->table_name,
                          sizeof(stmt->table_name),
                          "expected table name after FROM")) {
        return 0;
    }

    if (!parse_where_clause(parser, stmt)) {
        return 0;
    }

    return parse_order_by_clause(parser, stmt);
}

static int parse_insert(Parser *parser, Statement *stmt) {
    stmt->type = STMT_INSERT;

    if (!consume(parser, TOKEN_INTO, "expected INTO after INSERT")) {
        return 0;
    }

    if (!parse_identifier(parser,
                          stmt->table_name,
                          sizeof(stmt->table_name),
                          "expected table name after INTO")) {
        return 0;
    }

    if (!consume(parser, TOKEN_LPAREN, "expected '(' after table name")) {
        return 0;
    }

    /* INSERT는 컬럼 목록과 값 목록을 같은 순서로 먼저 수집한다. */
    stmt->insert_columns.count = 0;
    do {
        if (stmt->insert_columns.count >= MAX_COLUMNS) {
            parser_error(parser, "too many INSERT columns");
            return 0;
        }

        if (!parse_identifier(parser,
                              stmt->insert_columns.names[stmt->insert_columns.count],
                              sizeof(stmt->insert_columns.names[stmt->insert_columns.count]),
                              "expected column name in INSERT")) {
            return 0;
        }
        stmt->insert_columns.count++;
    } while (match(parser, TOKEN_COMMA));

    if (!consume(parser, TOKEN_RPAREN, "expected ')' after column list")) {
        return 0;
    }

    if (!consume(parser, TOKEN_VALUES, "expected VALUES")) {
        return 0;
    }

    if (!consume(parser, TOKEN_LPAREN, "expected '(' after VALUES")) {
        return 0;
    }

    stmt->value_count = 0;
    do {
        if (stmt->value_count >= MAX_COLUMNS) {
            parser_error(parser, "too many INSERT values");
            return 0;
        }

        if (!parse_value(parser, stmt, stmt->value_count)) {
            return 0;
        }
        stmt->value_count++;
    } while (match(parser, TOKEN_COMMA));

    if (!consume(parser, TOKEN_RPAREN, "expected ')' after value list")) {
        return 0;
    }

    return 1;
}

Statement *parse(Token *tokens, int token_count) {
    Parser parser;
    Statement *stmt;

    if (tokens == NULL || token_count <= 0) {
        fprintf(stderr, "[ERROR] Parser: token stream is empty\n");
        return NULL;
    }

    parser.tokens = tokens;
    parser.token_count = token_count;
    parser.current = 0;

    stmt = (Statement *)calloc(1, sizeof(Statement));
    if (stmt == NULL) {
        fprintf(stderr, "[ERROR] Parser: failed to allocate statement\n");
        return NULL;
    }

    if (match(&parser, TOKEN_SELECT)) {
        if (!parse_select(&parser, stmt)) {
            free(stmt);
            return NULL;
        }
    } else if (match(&parser, TOKEN_INSERT)) {
        if (!parse_insert(&parser, stmt)) {
            free(stmt);
            return NULL;
        }
    } else {
        parser_error(&parser, "unsupported statement");
        free(stmt);
        return NULL;
    }

    match(&parser, TOKEN_SEMICOLON);

    if (!is_at_end(&parser)) {
        parser_error(&parser, "unexpected tokens after statement");
        free(stmt);
        return NULL;
    }

    return stmt;
}

void free_statement(Statement *stmt) {
    free(stmt);
}
