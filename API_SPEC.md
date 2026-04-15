# SQL Engine - API 명세서

## 아키텍처 개요

```
[CLI] → [Lexer] → [Parser] → [Executor] → [Storage Engine]
                                    ↕
                             [Schema Manager]
```

## 모듈 구조

```
src/
├── main.c          - CLI 진입점
├── lexer.h/c       - 토크나이저 (SQL → Token 배열)
├── parser.h/c      - 파서 (Token 배열 → AST)
├── executor.h/c    - 실행기 (AST → 실제 동작)
├── storage.h/c     - 파일 기반 스토리지 엔진
├── schema.h/c      - 스키마 관리자
└── types.h         - 공통 타입 정의
tests/
├── test_lexer.c
├── test_parser.c
├── test_executor.c
└── test_storage.c
data/               - 테이블 데이터 파일 (.tbl)
schemas/            - 스키마 정의 파일 (.schema)
sql/                - 예시 SQL 파일
Makefile
```

---

## 도메인 기준 (MEMBERS)

구현·예시·테스트는 **기준 테이블 `members`** 와 아래 타입·상수를 맞춥니다. 상세는 `PROMPT.md`의 「도메인 기준: MEMBERS 레코드」와 동일합니다.

- `MAX_NAME_LEN` (32), `MAX_GRADE_LEN` (16), `MAX_CLASS_LEN` (16): VARCHAR 길이·검증·버퍼 기준
- `MemberRecord`: `id`, `name`, `grade`, `class`, `age` 필드 순서 및 고정 크기(72바이트) 참조 모델
- 스키마 파일 `schemas/members.schema`·데이터 `data/members.tbl`이 대표 경로 예시가 됨

```c
#include <stdint.h>

#define MAX_NAME_LEN 32
#define MAX_GRADE_LEN 16
#define MAX_CLASS_LEN 16

typedef struct {
    int32_t id;
    char name[MAX_NAME_LEN];
    char grade[MAX_GRADE_LEN];
    char class[MAX_CLASS_LEN];
    int32_t age;
} MemberRecord;
```

다른 테이블명도 동일한 엔진 API로 처리 가능하나, 문서·블랙박스 명세는 `members`를 기본으로 합니다.

---

## 공통 타입 (`types.h`)

### TokenType (열거형)

| 상수 | 설명 |
|------|------|
| `TOKEN_SELECT` | SELECT 키워드 |
| `TOKEN_INSERT` | INSERT 키워드 |
| `TOKEN_INTO` | INTO 키워드 |
| `TOKEN_FROM` | FROM 키워드 |
| `TOKEN_WHERE` | WHERE 키워드 |
| `TOKEN_VALUES` | VALUES 키워드 |
| `TOKEN_AND` | AND 키워드 |
| `TOKEN_OR` | OR 키워드 |
| `TOKEN_STAR` | `*` 문자 |
| `TOKEN_COMMA` | `,` 문자 |
| `TOKEN_LPAREN` | `(` 문자 |
| `TOKEN_RPAREN` | `)` 문자 |
| `TOKEN_SEMICOLON` | `;` 문자 |
| `TOKEN_EQ` | `=` 연산자 |
| `TOKEN_NEQ` | `!=` 또는 `<>` 연산자 |
| `TOKEN_LT` | `<` 연산자 |
| `TOKEN_GT` | `>` 연산자 |
| `TOKEN_LTE` | `<=` 연산자 |
| `TOKEN_GTE` | `>=` 연산자 |
| `TOKEN_IDENT` | 식별자 (테이블명, 컬럼명 등) |
| `TOKEN_STRING` | 문자열 리터럴 (`'...'`) |
| `TOKEN_NUMBER` | 숫자 리터럴 |
| `TOKEN_EOF` | 입력 끝 |

### ColumnType (열거형)

| 상수 | 설명 |
|------|------|
| `COL_INT` | 정수형 |
| `COL_VARCHAR` | 가변 문자열 |
| `COL_FLOAT` | 부동소수점 |
| `COL_DATE` | 날짜 (YYYY-MM-DD) |

### StatementType (열거형)

| 상수 | 설명 |
|------|------|
| `STMT_SELECT` | SELECT 문 |
| `STMT_INSERT` | INSERT 문 |
| `STMT_UNKNOWN` | 미지원 문 |

### 구조체

```c
typedef struct {
    TokenType type;
    char value[256];
    int line;
} Token;

typedef struct {
    char name[64];
    ColumnType type;
    int max_length;     // VARCHAR용
    int nullable;       // 1 = NULL 허용
    int is_primary_key;
} ColumnDef;

typedef struct {
    char table_name[64];
    ColumnDef columns[MAX_COLUMNS];
    int column_count;
} Schema;

typedef struct {
    char data[MAX_COLUMNS][256];  // 문자열로 통일 저장
    int column_count;
} Row;

typedef struct {
    char column_name[64];
    char operator[4];   // =, !=, <, >, <=, >=
    char value[256];
} Condition;

typedef struct {
    Condition conditions[MAX_CONDITIONS];
    int condition_count;
    char logical_op[4]; // AND / OR
} WhereClause;

typedef struct {
    char names[MAX_COLUMNS][64];
    int count;
    int is_star;        // SELECT * 여부
} ColumnList;

typedef struct {
    StatementType type;
    char table_name[64];
    // SELECT용
    ColumnList select_columns;
    WhereClause where;
    // INSERT용
    ColumnList insert_columns;
    char values[MAX_COLUMNS][256];
    int value_count;
} Statement;

typedef struct {
    Schema* schema;
    Row rows[MAX_ROWS];
    int row_count;
} ResultSet;
```

---

## Lexer API (`lexer.h`)

### `tokenize`

```c
Token* tokenize(const char* sql, int* token_count);
```

- **설명**: SQL 문자열을 토큰 배열로 변환
- **파라미터**:
  - `sql`: 입력 SQL 문자열
  - `token_count`: 반환 토큰 개수 (출력 파라미터)
- **반환**: 동적 할당된 Token 배열, 실패 시 `NULL`
- **오류 조건**: 미지원 문자, 닫히지 않은 문자열 리터럴

### `free_tokens`

```c
void free_tokens(Token* tokens);
```

- **설명**: tokenize로 할당된 메모리 해제

### 지원 SQL 예시

```sql
-- Oracle 문법 기준 (MEMBERS 도메인 예시)
INSERT INTO members (id, name, grade, class, age) VALUES (1, 'Alice', 'vip', 'advanced', 30);
SELECT * FROM members;
SELECT id, name FROM members WHERE age > 20;
SELECT * FROM members WHERE grade = 'vip' AND age >= 25;
```

---

## Parser API (`parser.h`)

### `parse`

```c
Statement* parse(Token* tokens, int token_count);
```

- **설명**: 토큰 배열을 AST(Statement 구조체)로 변환
- **파라미터**:
  - `tokens`: tokenize() 반환값
  - `token_count`: 토큰 개수
- **반환**: 동적 할당된 Statement, 실패 시 `NULL`
- **오류 조건**: 문법 오류, 미지원 구문

### `free_statement`

```c
void free_statement(Statement* stmt);
```

### 지원 문법 (Oracle 기준)

#### INSERT

```sql
INSERT INTO table_name (col1, col2, ...) VALUES (val1, val2, ...);
```

#### SELECT

```sql
SELECT * FROM table_name;
SELECT col1, col2 FROM table_name;
SELECT col1, col2 FROM table_name WHERE col = value;
SELECT * FROM table_name WHERE col1 = val1 AND col2 > val2;
```

#### WHERE 절 조건 연산자

| 연산자 | 지원 여부 |
|--------|-----------|
| `=` | O |
| `!=` / `<>` | O |
| `<` | O |
| `>` | O |
| `<=` | O |
| `>=` | O |

#### WHERE 절 논리 연산자

| 연산자 | 지원 여부 |
|--------|-----------|
| `AND` | O |
| `OR` | O |

---

## Executor API (`executor.h`)

### `execute`

```c
int execute(Statement* stmt);
```

- **설명**: 파싱된 Statement를 실제로 실행
- **반환**: `0` = 성공, `-1` = 실패
- **내부 동작**:
  - `STMT_INSERT` → `storage_insert()` 호출
  - `STMT_SELECT` → `storage_select()` 호출 후 결과 출력

### `execute_insert`

```c
int execute_insert(Statement* stmt);
```

- **설명**: INSERT 실행 — 스키마 검증 후 스토리지에 행 저장
- **오류 조건**: 컬럼 수 불일치, 타입 미스매치, PK 중복

### `execute_select`

```c
int execute_select(Statement* stmt);
```

- **설명**: SELECT 실행 — 스토리지에서 읽어 WHERE 필터링 후 출력
- **출력 형식**:

```
+----+-------+------+----------+-----+
| id | name  | grade | class   | age |
+----+-------+------+----------+-----+
|  1 | Alice | vip  | advanced |  30 |
|  2 | Bob   | normal | basic  |  22 |
+----+-------+------+----------+-----+
2 row(s) selected.
```

(컬럼 선택·값 길이에 따라 열 너비는 구현에서 조정 가능)

---

## Storage API (`storage.h`)

### `storage_insert`

```c
int storage_insert(const char* table_name, Row* row, Schema* schema);
```

- **설명**: 테이블 데이터 파일에 행 추가
- **저장 형식**: CSV 기반 (컬럼 구분자 `|`, 행 구분자 `\n`)
- **파일 경로**: `data/{table_name}.tbl`
- **반환**: `0` = 성공, `-1` = 실패

### `storage_select`

```c
ResultSet* storage_select(const char* table_name, Schema* schema,
                           ColumnList* columns, WhereClause* where);
```

- **설명**: 테이블 파일에서 행을 읽어 WHERE 조건으로 필터링
- **반환**: 동적 할당된 ResultSet, 실패 시 `NULL`

### `free_result_set`

```c
void free_result_set(ResultSet* rs);
```

### 데이터 파일 포맷 (`.tbl`)

```
# 첫 줄: 컬럼 헤더 (스키마와 동일 순서)
id|name|grade|class|age
# 이후: 데이터 행
1|Alice|vip|advanced|30
2|Bob|normal|basic|22
```

---

## Schema API (`schema.h`)

### `schema_load`

```c
Schema* schema_load(const char* table_name);
```

- **설명**: 스키마 정의 파일 로드
- **파일 경로**: `schemas/{table_name}.schema`
- **반환**: 동적 할당된 Schema, 실패 시 `NULL`

### `schema_free`

```c
void schema_free(Schema* schema);
```

### 스키마 파일 포맷 (`.schema`)

```
# table: members
# format: column_name,type,max_length,nullable,primary_key
id,INT,0,0,1
name,VARCHAR,32,0,0
grade,VARCHAR,16,1,0
class,VARCHAR,16,1,0
age,INT,0,1,0
```

(`max_length`·nullable은 `MemberRecord`·`MAX_*`와 일치시킬 것)

---

## CLI 인터페이스

### 사용법

```bash
# SQL 파일 실행
./sqlengine -f query.sql

# SQL 직접 입력
./sqlengine -e "SELECT * FROM members;"

# 인터랙티브 모드
./sqlengine

# 도움말
./sqlengine --help
```

### 옵션

| 옵션 | 설명 |
|------|------|
| `-f <file>` | SQL 파일 실행 |
| `-e <sql>` | SQL 문자열 직접 실행 |
| `-d <dir>` | 데이터 디렉토리 지정 (기본: `./data`) |
| `-s <dir>` | 스키마 디렉토리 지정 (기본: `./schemas`) |
| `--help` | 도움말 출력 |
| `--version` | 버전 출력 |

### 종료 코드

| 코드 | 의미 |
|------|------|
| `0` | 정상 실행 |
| `1` | SQL 파싱 오류 |
| `2` | 실행 오류 (스키마 없음, 타입 오류 등) |
| `3` | 파일 I/O 오류 |

---

## 오류 처리

모든 함수는 오류 발생 시 `stderr`에 메시지를 출력합니다.

```
[ERROR] Parser: unexpected token 'FORM' at line 1 (expected FROM)
[ERROR] Schema: table 'unknown_table' not found
[ERROR] Storage: failed to open data/members.tbl
[ERROR] Executor: type mismatch for column 'age' (expected INT, got STRING)
```

---

## 추가 구현 (선택)

| 기능 | 우선순위 | 설명 |
|------|----------|------|
| `DELETE` | 중 | WHERE 조건 기반 행 삭제 |
| `UPDATE` | 중 | WHERE 조건 기반 행 수정 |
| `ORDER BY` | 낮 | 정렬 지원 |
| `LIKE` 연산자 | 낮 | 패턴 매칭 WHERE 조건 |
| `NULL` 처리 | 중 | NULL 값 삽입/비교 |
| `COUNT(*)` | 낮 | 집계 함수 |

---

## 테스트 명세

### 화이트박스 테스트 (Whitebox Testing)

내부 구현 로직을 직접 검증합니다. 소스 코드를 알고 있는 상태에서 분기/경로/상태를 추적합니다.

#### 테스트 파일 구조

```
tests/
├── test_lexer.c       Lexer 내부 상태 및 분기 커버리지
├── test_parser.c      Parser 토큰 소비 경로 및 AST 구조 검증
├── test_schema.c      Schema 파일 파싱 경로 및 타입 변환 검증
├── test_storage.c     Storage 파일 I/O 경로 및 필터링 로직 검증
└── test_executor.c    Executor 내부 분기 (타입 검증, 컬럼 재정렬) 검증
```

#### 커버리지 목표

| 모듈 | 분기 커버리지 목표 | 핵심 검증 경로 |
|------|-------------------|----------------|
| lexer | 90% 이상 | 정상 토큰, 미지원 문자, 미닫힌 문자열, 주석, EOF |
| parser | 85% 이상 | SELECT/INSERT 정상, 문법 오류, 토큰 부족, NULL 반환 |
| schema | 90% 이상 | 정상 로드, 파일 없음, 잘못된 타입 문자열, 주석/빈 줄 |
| storage | 85% 이상 | 파일 없는 첫 INSERT, append INSERT, SELECT 필터 참/거짓 |
| executor | 80% 이상 | 컬럼 수 불일치, 타입 오류, 스키마 없음, 정상 실행 |

#### 화이트박스 테스트 케이스 명세

**Lexer**

| 테스트 ID | 검증 대상 분기 | 입력 | 기대 결과 |
|-----------|---------------|------|-----------|
| WB-L-01 | 키워드 대소문자 분기 | `"select"`, `"SELECT"`, `"Select"` | 모두 `TOKEN_SELECT` |
| WB-L-02 | 두 글자 연산자 분기 | `"<="`, `">="`, `"!="`, `"<>"` | 각각 `TOKEN_LTE`, `TOKEN_GTE`, `TOKEN_NEQ`, `TOKEN_NEQ` |
| WB-L-03 | 문자열 이스케이프 분기 | `"'it''s'"` | value = `it's` |
| WB-L-04 | 미닫힌 문자열 오류 경로 | `"'unclosed"` | `NULL` 반환, stderr 출력 |
| WB-L-05 | EOF 직전 단일 토큰 | `";"` | `TOKEN_SEMICOLON` + `TOKEN_EOF` |
| WB-L-06 | 주석 건너뜀 분기 | `"-- comment\nSELECT"` | `TOKEN_SELECT` 만 존재 |
| WB-L-07 | 실수 숫자 분기 | `"3.14"` | `TOKEN_NUMBER`, value=`"3.14"` |

**Parser**

| 테스트 ID | 검증 대상 분기 | 입력 | 기대 결과 |
|-----------|---------------|------|-----------|
| WB-P-01 | `is_star` 분기 | `SELECT *` | `select_columns.is_star == 1` |
| WB-P-02 | 다중 컬럼 분기 | `SELECT a, b, c FROM t` | `column_count == 3` |
| WB-P-03 | WHERE 없는 분기 | `SELECT * FROM t;` | `where.condition_count == 0` |
| WB-P-04 | WHERE AND 분기 | `WHERE a=1 AND b=2` | `condition_count==2`, `logical_op=="AND"` |
| WB-P-05 | WHERE OR 분기 | `WHERE a=1 OR b=2` | `logical_op=="OR"` |
| WB-P-06 | INSERT 컬럼-값 매핑 | `INSERT INTO t (a,b) VALUES (1,'x')` | `insert_columns.count==2`, `value_count==2` |
| WB-P-07 | 잘못된 키워드 경로 | `SELEC * FROM t;` | `NULL` 반환 |
| WB-P-08 | FROM 누락 경로 | `SELECT * t;` | `NULL` 반환 |

**Storage**

| 테스트 ID | 검증 대상 분기 | 조건 | 기대 결과 |
|-----------|---------------|------|-----------|
| WB-S-01 | 파일 신규 생성 분기 | `.tbl` 미존재 후 `storage_insert` | 헤더 포함 파일 생성 |
| WB-S-02 | 기존 파일 append 분기 | `.tbl` 존재 후 `storage_insert` | 기존 행 유지, 새 행 추가 |
| WB-S-03 | WHERE `=` INT 비교 | `age = 30` | 일치 행만 반환 |
| WB-S-04 | WHERE `>` INT 비교 | `age > 20` | 조건 만족 행만 반환 |
| WB-S-05 | WHERE VARCHAR 비교 | `grade = 'vip'` | 문자열 일치 행만 반환 |
| WB-S-06 | WHERE 조건 불일치 | 없는 값 조건 | `row_count == 0` |
| WB-S-07 | NULL 필드 처리 | nullable 컬럼 빈 값 읽기 | data 필드 = `""` |

---

### 블랙박스 테스트 (Blackbox Testing)

외부 동작만 검증합니다. 내부 구현 없이 CLI 입출력, 파일 생성 여부, 종료 코드를 기준으로 판단합니다.

#### 테스트 파일 구조

```
tests/
├── blackbox/
│   ├── run_tests.sh        전체 블랙박스 테스트 실행 스크립트
│   ├── cases/
│   │   ├── bb_insert.sql   INSERT 테스트 SQL
│   │   ├── bb_select.sql   SELECT 테스트 SQL
│   │   ├── bb_where.sql    WHERE 조건 테스트 SQL
│   │   └── bb_edge.sql     엣지 케이스 SQL
│   └── expected/
│       ├── bb_select.txt   예상 출력
│       └── bb_where.txt    예상 출력
```

#### 블랙박스 테스트 케이스 명세

**정상 동작 (Happy Path)**

| 테스트 ID | 입력 | 기대 출력 | 기대 종료 코드 |
|-----------|------|-----------|---------------|
| BB-01 | `INSERT INTO members (id,name) VALUES (1,'Alice');` (나머지 컬럼은 스키마상 NULL 허용 시 생략 가능) | `1 row inserted.` | `0` |
| BB-02 | `SELECT * FROM members;` (BB-01 이후) | 테이블 형식 출력, `1 row(s) selected.` | `0` |
| BB-03 | `SELECT * FROM members WHERE id = 1;` | Alice 행 포함 | `0` |
| BB-04 | `SELECT * FROM members WHERE id = 99;` | `0 row(s) selected.` | `0` |
| BB-05 | `-f` 옵션으로 SQL 파일 실행 | 정상 출력 | `0` |
| BB-06 | 세미콜론으로 구분된 SQL 2개 연속 실행 | 각 SQL 결과 순서대로 출력 | `0` |

**오류 처리 (Error Path)**

| 테스트 ID | 입력 | 기대 stderr 포함 문자열 | 기대 종료 코드 |
|-----------|------|------------------------|---------------|
| BB-E-01 | `SELECT * FROM nonexistent;` | `[ERROR] Schema:` | `2` |
| BB-E-02 | `SELEC * FROM members;` | `[ERROR] Parser:` | `1` |
| BB-E-03 | `-f missing.sql` | `[ERROR]` 파일 없음 | `3` |
| BB-E-04 | `INSERT INTO members (id) VALUES ('notanumber');` (id가 INT) | `[ERROR] Executor:` | `2` |
| BB-E-05 | `INSERT INTO members (id,name,grade,class,age) VALUES (1,'A');` (컬럼 수 불일치) | `[ERROR] Executor:` | `2` |

**엣지 케이스**

| 테스트 ID | 입력 | 기대 결과 |
|-----------|------|-----------|
| BB-EG-01 | 공백/탭만 있는 SQL 파일 | 오류 없이 종료 (exit 0) |
| BB-EG-02 | 주석만 있는 SQL (`-- comment`) | 오류 없이 종료 |
| BB-EG-03 | 100개 행 연속 INSERT 후 SELECT | 100 row(s) selected. |
| BB-EG-04 | 세미콜론 없이 끝나는 SQL | `[ERROR] Parser:` 또는 무시 |
| BB-EG-05 | 컬럼명 대소문자 혼합 (`Name`, `NAME`) | 대소문자 무관 처리 |
| BB-EG-06 | 값에 파이프 문자 포함 (`'a\|b'`) | 저장 및 조회 정상 동작 |

#### 블랙박스 테스트 실행 방법

```bash
# 전체 블랙박스 테스트 실행
make test-blackbox

# 또는 직접 실행
bash tests/blackbox/run_tests.sh

# 출력 예시
[PASS] BB-01: INSERT single row
[PASS] BB-02: SELECT all rows
[FAIL] BB-E-04: expected exit code 2, got 0
Tests: 14 passed, 1 failed
```

#### 출력 검증 방식

```bash
# 실제 출력을 expected 파일과 diff 비교
./sqlengine -f tests/blackbox/cases/bb_select.sql > actual.txt
diff tests/blackbox/expected/bb_select.txt actual.txt
```
