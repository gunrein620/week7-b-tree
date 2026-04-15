#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

#define MAX_COLUMNS 32
#define MAX_ROWS 10000
#define MAX_CONDITIONS 16
#define MAX_TOKEN_LEN 256
#define MAX_IDENTIFIER_LEN 64
#define MAX_PATH_LEN 512

/* MEMBERS 테이블의 문자열 길이 제한 */
#define MAX_NAME_LEN 32
#define MAX_GRADE_LEN 16
#define MAX_CLASS_LEN 16

/* MEMBERS 테이블의 고정 기준 레코드 */
typedef struct {
    int32_t id;
    char name[MAX_NAME_LEN];
    char grade[MAX_GRADE_LEN];
    char class[MAX_CLASS_LEN];
    int32_t age;
} MemberRecord;

typedef enum {
    TOKEN_UNKNOWN = 0,
    TOKEN_SELECT,
    TOKEN_INSERT,
    TOKEN_INTO,
    TOKEN_FROM,
    TOKEN_WHERE,
    TOKEN_ORDER,
    TOKEN_BY,
    TOKEN_ASC,
    TOKEN_DESC,
    TOKEN_VALUES,
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_NULL,
    TOKEN_STAR,
    TOKEN_COMMA,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_SEMICOLON,
    TOKEN_EQ,
    TOKEN_NEQ,
    TOKEN_LT,
    TOKEN_GT,
    TOKEN_LTE,
    TOKEN_GTE,
    TOKEN_IDENT,
    TOKEN_STRING,
    TOKEN_NUMBER,
    TOKEN_EOF
} TokenType;

typedef enum {
    COL_UNKNOWN = 0,
    COL_INT,
    COL_VARCHAR,
    COL_FLOAT,
    COL_DATE
} ColumnType;

typedef enum {
    STMT_UNKNOWN = 0,
    STMT_SELECT,
    STMT_INSERT
} StatementType;

typedef enum {
    SORT_ASC = 0,
    SORT_DESC
} SortDirection;

typedef struct {
    TokenType type;
    char value[MAX_TOKEN_LEN];
    int line;
} Token;

typedef struct {
    char name[MAX_IDENTIFIER_LEN];
    ColumnType type;
    int max_length;
    int nullable;
    int is_primary_key;
} ColumnDef;

typedef struct {
    char table_name[MAX_IDENTIFIER_LEN];
    ColumnDef columns[MAX_COLUMNS];
    int column_count;
} Schema;

typedef struct {
    char data[MAX_COLUMNS][MAX_TOKEN_LEN];
    int column_count;
} Row;

typedef struct {
    char column_name[MAX_IDENTIFIER_LEN];
    char operator[4];
    char value[MAX_TOKEN_LEN];
} Condition;

typedef struct {
    Condition conditions[MAX_CONDITIONS];
    int condition_count;
    char logical_op[4];
} WhereClause;

typedef struct {
    char names[MAX_COLUMNS][MAX_IDENTIFIER_LEN];
    int count;
    int is_star;
} ColumnList;

typedef struct {
    int enabled;
    char column_name[MAX_IDENTIFIER_LEN];
    SortDirection direction;
} OrderByClause;

typedef struct {
    StatementType type;
    char table_name[MAX_IDENTIFIER_LEN];
    ColumnList select_columns;
    WhereClause where;
    OrderByClause order_by;
    ColumnList insert_columns;
    char values[MAX_COLUMNS][MAX_TOKEN_LEN];
    int value_is_null[MAX_COLUMNS];
    int value_count;
} Statement;

typedef struct {
    Schema *schema;
    Row rows[MAX_ROWS];
    int row_count;
    int selected_indexes[MAX_COLUMNS];
    int selected_count;
} ResultSet;

#endif
