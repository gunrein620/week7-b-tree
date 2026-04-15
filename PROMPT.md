# SQL Engine - 구현 프롬프트

## 프로젝트 개요

C언어로 Oracle SQL 문법 기반의 파일 저장소 SQL 처리기를 구현합니다.
입력(SQL 파일 또는 문자열) → 렉싱 → 파싱 → 실행 → 파일 I/O의 전체 파이프라인을 구축합니다.

---

## 도메인 기준: MEMBERS 레코드

이 프로젝트는 아래 **고정 정의**를 `types.h`에 포함하고, 스키마·저장·실행 로직을 이 기준에 맞춰 구현합니다.

- `MAX_NAME_LEN`, `MAX_GRADE_LEN`, `MAX_CLASS_LEN`: MEMBERS 테이블 컬럼 길이·검증·버퍼 크기의 기준
- `MemberRecord`: MEMBERS 테이블의 논리적 한 행 레이아웃 (필드 순서·타입·총 크기 72바이트 고정 레코드 관점의 참조 모델)

```c
#include <stdint.h>

// 테이블 컬럼 제약 조건에 맞춘 상수 정의
#define MAX_NAME_LEN 32
#define MAX_GRADE_LEN 16
#define MAX_CLASS_LEN 16

// MEMBERS 테이블 레코드 구조체
// 총 크기: 4(id) + 32(name) + 16(grade) + 16(class) + 4(age) = 72 bytes
typedef struct {
    int32_t id;                 // PK: 회원 번호
    char name[MAX_NAME_LEN];    // 이름
    char grade[MAX_GRADE_LEN];  // 회원 등급 (vip / normal)
    char class[MAX_CLASS_LEN];  // 수강반 (advanced / middle / basic)
    int32_t age;                // 나이
} MemberRecord;
```

위 내용은 **최종 `types.h`의 헤더 가드(`#ifndef TYPES_H` … `#endif`) 안**에 두고, 그 아래(또는 위)에 SQL 엔진 공통 타입(`Token`, `Schema`, `Row` 등)을 같은 파일에서 정의합니다. 멤버 이름 `class`는 C99에서는 허용되나 C++과 혼용 빌드 시 예약어와 충돌할 수 있으므로 프로젝트는 C 전용으로 두는 것을 권장합니다.

---

## Phase 1: 기반 구조 및 타입 정의

### 프롬프트

```
C언어로 SQL 엔진의 공통 타입을 정의하는 types.h 헤더 파일을 작성해줘.

요구사항:
- 문서 상단 「도메인 기준: MEMBERS 레코드」에 적힌 상수(MAX_NAME_LEN 등) 및 MemberRecord typedef를 반드시 포함하고, 이후 단계(스키마 예시·INSERT/SELECT·저장)에서 MEMBERS 테이블은 이 레이아웃·길이 제한을 기준으로 맞출 것
- Oracle SQL 문법 기준
- 지원 문 타입: SELECT, INSERT (향후 DELETE/UPDATE 확장 가능 구조)
- 토큰 타입 열거형 (키워드, 연산자, 리터럴, 구두점 포함)
- 컬럼 타입 열거형: INT, VARCHAR, FLOAT, DATE
- 구조체 정의:
  - Token: type, value(256), line
  - ColumnDef: name, type, max_length, nullable, is_primary_key
  - Schema: table_name, columns 배열(최대 32), column_count
  - Row: data 배열(최대 32 컬럼, 각 256바이트), column_count
  - Condition: column_name, operator(=,!=,<,>,<=,>=), value
  - WhereClause: conditions 배열(최대 16), condition_count, logical_op(AND/OR)
  - ColumnList: names 배열, count, is_star
  - Statement: type, table_name, select_columns, where, insert_columns, values, value_count
  - ResultSet: schema 포인터, rows 배열, row_count
- 매크로 상수: MAX_COLUMNS=32, MAX_ROWS=10000, MAX_CONDITIONS=16, MAX_TOKEN_LEN=256

구현 언어: C (C99 표준)
헤더 가드 포함
```

---

## Phase 2: Lexer (토크나이저)

### 프롬프트

```
C언어로 SQL Lexer(토크나이저)를 구현해줘.

파일: lexer.h, lexer.c

요구사항:
- 함수 시그니처:
  Token* tokenize(const char* sql, int* token_count);
  void free_tokens(Token* tokens);

- Oracle SQL 기준으로 다음을 인식:
  - 키워드: SELECT, INSERT, INTO, FROM, WHERE, VALUES, AND, OR, NULL
  - 연산자: =, !=, <>, <, >, <=, >=
  - 구두점: ( ) , ; *
  - 식별자: 영문자/숫자/언더스코어, 대소문자 무관
  - 문자열 리터럴: 단일 따옴표 ('...'), 내부 이스케이프 '' 처리
  - 숫자 리터럴: 정수 및 소수점 포함 실수
  - 공백/탭/줄바꿈 무시
  - 한 줄 주석: -- 이후 줄끝까지 무시

- 오류 처리:
  - 닫히지 않은 문자열 리터럴: stderr에 오류 출력 후 NULL 반환
  - 미지원 문자: stderr 경고 후 건너뜀

- 동적 배열로 토큰 수집 (realloc 사용)
- 마지막 토큰은 TOKEN_EOF

구현 언어: C (C99), stdlib/string/ctype 사용 가능
types.h를 include하여 Token, TokenType 사용
```

---

## Phase 3: Parser

### 프롬프트

```
C언어로 SQL Parser를 구현해줘.

파일: parser.h, parser.c

요구사항:
- 함수 시그니처:
  Statement* parse(Token* tokens, int token_count);
  void free_statement(Statement* stmt);

- 지원 문법 (Oracle 기준):

  INSERT:
    INSERT INTO table_name (col1, col2, ...) VALUES (val1, val2, ...);

  SELECT:
    SELECT * FROM table_name;
    SELECT col1, col2 FROM table_name;
    SELECT col1, col2 FROM table_name WHERE condition;
    WHERE: col op val [AND|OR col op val ...]
    op: =, !=, <>, <, >, <=, >=

- 파서 구조: 재귀 하강(Recursive Descent) 방식 권장
- 내부 상태: 현재 토큰 인덱스를 전역 또는 구조체로 관리
- peek/advance 헬퍼 함수 구현
- 오류 시 stderr에 "[ERROR] Parser: ..." 형식으로 출력 후 NULL 반환
- 성공 시 malloc으로 Statement 동적 할당하여 반환

구현 언어: C (C99)
types.h, lexer.h include
```

---

## Phase 4: Schema Manager

### 프롬프트

```
C언어로 스키마 파일 관리자를 구현해줘.

파일: schema.h, schema.c

요구사항:
- 함수 시그니처:
  Schema* schema_load(const char* table_name);
  void schema_free(Schema* schema);
  int schema_get_column_index(Schema* schema, const char* column_name);
  ColumnType schema_parse_type(const char* type_str);

- 스키마 파일 경로: schemas/{table_name}.schema
  (환경변수 또는 전역 변수로 경로 오버라이드 가능)

- 스키마 파일 포맷:
  # 주석은 # 로 시작
  # 형식: column_name,type,max_length,nullable,primary_key
  id,INT,0,0,1
  name,VARCHAR,100,0,0
  salary,INT,0,1,0

- MEMBERS 테이블용 `schemas/members.schema`(또는 동일 컬럼 집합)는 MemberRecord 필드 순서(id, name, grade, class, age) 및 MAX_NAME_LEN / MAX_GRADE_LEN / MAX_CLASS_LEN에 맞는 max_length·PK(id)로 작성할 것

- 타입 문자열 파싱: INT, VARCHAR, FLOAT, DATE → ColumnType 열거형
- 빈 줄, 주석 줄 무시
- 오류 시 stderr에 "[ERROR] Schema: ..." 출력 후 NULL 반환

구현 언어: C (C99)
types.h include
```

---

## Phase 5: Storage Engine

### 프롬프트

```
C언어로 파일 기반 스토리지 엔진을 구현해줘.

파일: storage.h, storage.c

요구사항:
- 함수 시그니처:
  int storage_insert(const char* table_name, Row* row, Schema* schema);
  ResultSet* storage_select(const char* table_name, Schema* schema,
                             ColumnList* columns, WhereClause* where);
  void free_result_set(ResultSet* rs);

- 데이터 파일 경로: data/{table_name}.tbl
  (전역 변수로 경로 오버라이드 가능)

- 데이터 파일 포맷 (CSV 기반):
  첫 줄: 파이프(|) 구분 컬럼 헤더
  이후: 파이프 구분 데이터 행
  예:
    id|name|salary|dept
    1|Alice|50000|IT

- storage_insert:
  - 파일이 없으면 헤더 포함 새로 생성
  - 파일이 있으면 데이터 행 추가 (append)
  - Row 데이터는 Schema 컬럼 순서에 맞게 정렬하여 저장
  - MEMBERS 테이블 행은 도메인 기준(MemberRecord, MAX_* 길이)을 넘지 않도록 검증·절단 정책을 일관되게 적용할 것

- storage_select:
  - 파일에서 모든 행 읽기
  - WhereClause 조건으로 필터링
  - ColumnList로 컬럼 선택 (is_star=1이면 전체)
  - 조건 평가: INT는 숫자 비교, VARCHAR는 문자열 비교
  - ResultSet 동적 할당 반환

- WHERE 필터링 함수:
  int evaluate_condition(Row* row, Schema* schema, Condition* cond);
  int evaluate_where(Row* row, Schema* schema, WhereClause* where);

- 오류 시 stderr에 "[ERROR] Storage: ..." 출력

구현 언어: C (C99)
types.h, schema.h include
```

---

## Phase 6: Executor

### 프롬프트

```
C언어로 SQL 실행기를 구현해줘.

파일: executor.h, executor.c

요구사항:
- 함수 시그니처:
  int execute(Statement* stmt);

- 내부 함수:
  int execute_insert(Statement* stmt);
  int execute_select(Statement* stmt);

- execute_insert 동작:
  1. schema_load()로 스키마 로드
  2. INSERT 문의 컬럼-값 매핑 검증
     - 컬럼 수 일치 확인
     - 각 컬럼이 스키마에 존재하는지 확인
     - 기본 타입 호환성 검사 (INT → 숫자 문자열인지)
  3. Statement의 컬럼 순서를 스키마 순서로 재정렬하여 Row 구성
  4. storage_insert() 호출
  5. 성공 시 "1 row inserted." 출력

- execute_select 동작:
  1. schema_load()로 스키마 로드
  2. SELECT 컬럼 목록 검증 (스키마에 존재하는지)
  3. storage_select() 호출
  4. 결과를 표 형식으로 출력:
     +----+-------+--------+
     | id | name  | salary |
     +----+-------+--------+
     |  1 | Alice |  50000 |
     +----+-------+--------+
     N row(s) selected.
  5. 컬럼 너비는 데이터에 맞게 동적 계산

- 반환: 0 = 성공, -1 = 실패

구현 언어: C (C99)
types.h, parser.h, schema.h, storage.h include
```

---

## Phase 7: CLI 및 main

### 프롬프트

```
C언어로 SQL 엔진의 CLI 진입점을 구현해줘.

파일: main.c

요구사항:
- 옵션 파싱 (getopt 또는 수동 파싱):
  -f <file>   : SQL 파일 실행
  -e <sql>    : SQL 문자열 직접 실행
  -d <dir>    : 데이터 디렉토리 경로 지정 (기본: ./data)
  -s <dir>    : 스키마 디렉토리 경로 지정 (기본: ./schemas)
  --help      : 도움말 출력
  --version   : 버전 출력 (1.0.0)

- SQL 파일 처리:
  - 파일 전체를 읽어 세미콜론(;)으로 여러 SQL 문 분리
  - 각 문을 순서대로 실행
  - 한 문이 실패해도 이후 문 계속 실행 (오류 출력 후 계속)

- 인터랙티브 모드 (옵션 없이 실행):
  - 프롬프트: "sql> "
  - 한 줄씩 입력 받기
  - "exit" 또는 "quit" 입력 시 종료
  - 세미콜론으로 끝날 때까지 여러 줄 입력 허용

- 실행 파이프라인: tokenize() → parse() → execute()
- 각 단계 후 메모리 해제

- 종료 코드:
  0: 정상
  1: 파싱 오류
  2: 실행 오류
  3: 파일 I/O 오류

구현 언어: C (C99), POSIX 환경
types.h, lexer.h, parser.h, executor.h include
```

---

## Phase 8: Makefile

### 프롬프트

```
C 프로젝트용 Makefile을 작성해줘.

프로젝트 구조:
- src/ 디렉토리에 .c/.h 파일
- tests/ 디렉토리에 테스트 파일
- 빌드 결과물: ./sqlengine

요구사항:
- 컴파일러: gcc
- 표준: C99
- 경고: -Wall -Wextra
- 디버그 빌드: -g -DDEBUG
- 릴리스 빌드: -O2

타겟:
- all: 기본 빌드 (sqlengine)
- debug: 디버그 빌드
- test: 단위 테스트 빌드 및 실행
- clean: 빌드 결과물 삭제
- run-f: ./sqlengine -f 로 SQL 파일 실행 (SQL=파일경로 변수)
- help: 타겟 목록 출력

테스트 타겟:
- tests/ 디렉토리의 test_*.c 파일 각각을 독립 실행파일로 빌드
- make test 시 모든 테스트 실행 후 패스/실패 요약

의존성 자동 추적 (.d 파일 방식)
data/, schemas/ 디렉토리 자동 생성
```

---

## Phase 9: 단위 테스트

### 프롬프트

```
C언어로 SQL 엔진 단위 테스트 파일들을 작성해줘.
외부 테스트 프레임워크 없이 순수 C로 작성 (assert.h 사용 가능).

test_lexer.c:
- 빈 입력 토크나이징
- SELECT 문 토크나이징 및 토큰 타입/값 검증
- INSERT 문 토크나이징
- 문자열 리터럴 (작은따옴표, 이스케이프 '')
- 모든 비교 연산자 인식 (=, !=, <>, <, >, <=, >=)
- 주석(--) 무시
- 대소문자 무관 키워드 인식

test_parser.c:
- SELECT * FROM table
- SELECT col1, col2 FROM table WHERE col = 'value'
- INSERT INTO table (col1, col2) VALUES (1, 'name')
- WHERE절 AND/OR 조건
- 잘못된 문법 → NULL 반환 확인

test_storage.c:
- 임시 디렉토리에 스키마/데이터 파일 생성
- INSERT 후 .tbl 파일 내용 검증
- SELECT 전체 행 읽기
- SELECT WHERE 조건 필터링
- 테스트 종료 후 임시 파일 정리

test_schema.c:
- 스키마 파일 로드 및 컬럼 속성 검증
- 존재하지 않는 테이블 로드 → NULL 반환
- column index 조회 (존재/비존재)

각 테스트 파일:
- main() 함수 포함 (독립 실행)
- PASS/FAIL 출력 형식: "[PASS] test_name" / "[FAIL] test_name: reason"
- 최종 요약: "Tests: N passed, M failed"
- 실패 시 exit(1) 반환
```

---

## Phase 10: 통합 테스트 SQL 파일

### 프롬프트

```
SQL 엔진 통합 테스트를 위한 SQL 파일들과 예시 스키마를 작성해줘.

1. schemas/employees.schema:
   컬럼: id(INT,PK), name(VARCHAR 100), salary(INT), dept(VARCHAR 50)

2. schemas/products.schema:
   컬럼: product_id(INT,PK), product_name(VARCHAR 200), price(FLOAT), stock(INT)

3. sql/test_insert.sql:
   employees 테이블에 5개 행 INSERT (다양한 부서, 급여)

4. sql/test_select.sql:
   - SELECT * FROM employees
   - SELECT id, name FROM employees
   - SELECT * FROM employees WHERE dept = 'IT'
   - SELECT * FROM employees WHERE salary > 45000
   - SELECT * FROM employees WHERE dept = 'IT' AND salary >= 50000

5. sql/test_edge.sql (엣지 케이스):
   - 특수문자 포함 문자열 INSERT
   - 최대 길이에 근접한 값
   - NULL 값 처리 (nullable 컬럼)
   - 같은 행 중복 INSERT
```

---

## Phase 11: 화이트박스 테스트 (Whitebox Testing)

### 프롬프트

```
C언어로 SQL 엔진의 화이트박스(내부 구현 검증) 단위 테스트를 작성해줘.
소스 코드의 내부 분기, 경로, 상태를 직접 검증한다.

파일: tests/test_whitebox.c (또는 모듈별 test_*.c에 WB 케이스 추가)

테스트 대상 및 케이스:

[Lexer 분기 커버리지]
- WB-L-01: "select" / "SELECT" / "Select" → 모두 TOKEN_SELECT
- WB-L-02: "<=" / ">=" / "!=" / "<>" → TOKEN_LTE / TOKEN_GTE / TOKEN_NEQ / TOKEN_NEQ
- WB-L-03: "'it''s'" (이스케이프) → value = "it's"
- WB-L-04: "'unclosed" (미닫힌 문자열) → tokenize() NULL 반환 + stderr 출력
- WB-L-05: ";" 단일 입력 → TOKEN_SEMICOLON + TOKEN_EOF
- WB-L-06: "-- comment\nSELECT" → TOKEN_SELECT 만 존재 (주석 건너뜀)
- WB-L-07: "3.14" → TOKEN_NUMBER, value = "3.14"

[Parser 분기 커버리지]
- WB-P-01: "SELECT * FROM t;" → select_columns.is_star == 1
- WB-P-02: "SELECT a, b, c FROM t;" → column_count == 3
- WB-P-03: "SELECT * FROM t;" → where.condition_count == 0
- WB-P-04: "SELECT * FROM t WHERE a=1 AND b=2;" → condition_count==2, logical_op=="AND"
- WB-P-05: "SELECT * FROM t WHERE a=1 OR b=2;" → logical_op=="OR"
- WB-P-06: "INSERT INTO t (a,b) VALUES (1,'x');" → insert_columns.count==2, value_count==2
- WB-P-07: "SELEC * FROM t;" → parse() NULL 반환
- WB-P-08: "SELECT * t;" (FROM 누락) → parse() NULL 반환

[Storage 분기 커버리지]
- WB-S-01: .tbl 파일 없는 상태에서 storage_insert → 헤더+데이터 파일 신규 생성 확인
- WB-S-02: .tbl 파일 존재 상태에서 storage_insert → 기존 행 유지, 새 행 추가
- WB-S-03: WHERE salary = 50000 (INT 동등 비교) → 정확히 해당 행만 반환
- WB-S-04: WHERE salary > 45000 (INT 범위 비교) → 조건 만족 행만 반환
- WB-S-05: WHERE dept = 'IT' (VARCHAR 비교) → 문자열 일치 행만 반환
- WB-S-06: WHERE id = 9999 (없는 값) → row_count == 0
- WB-S-07: nullable 컬럼에 빈 값 저장 후 읽기 → data 필드 == ""

[Executor 분기 커버리지]
- WB-E-01: INSERT 컬럼 수 > VALUES 수 → execute() -1 반환 + stderr
- WB-E-02: INT 컬럼에 문자열 값 삽입 → execute() -1 반환
- WB-E-03: 존재하지 않는 테이블 INSERT → execute() -1 반환
- WB-E-04: SELECT 컬럼이 스키마에 없음 → execute() -1 반환
- WB-E-05: 정상 INSERT → 0 반환 + "1 row inserted." stdout 출력
- WB-E-06: 정상 SELECT → 0 반환 + 테이블 출력

구현 조건:
- 각 케이스는 독립 실행 (테스트 간 상태 공유 금지)
- 임시 파일은 /tmp/wb_test_* 경로 사용 후 테스트 종료 시 삭제
- 출력 형식: "[PASS] WB-L-01: keyword case insensitive"
- 최종 요약: "Whitebox Tests: N passed, M failed"
- 실패 시 exit(1)

구현 언어: C (C99), types.h/lexer.h/parser.h/schema.h/storage.h/executor.h include
```

---

## Phase 12: 블랙박스 테스트 (Blackbox Testing)

### 프롬프트

```
SQL 엔진의 블랙박스(외부 동작 검증) 테스트 스크립트를 작성해줘.
내부 구현을 모르는 상태에서 CLI 입출력과 종료 코드만으로 검증한다.

파일: tests/blackbox/run_tests.sh

전제 조건:
- ./sqlengine 바이너리 빌드 완료
- schemas/employees.schema 존재 (id INT PK, name VARCHAR 100, salary INT, dept VARCHAR 50)
- 각 테스트 전 data/ 디렉토리 초기화

테스트 케이스:

[Happy Path]
BB-01: INSERT 단일 행
  입력: INSERT INTO employees (id,name,salary,dept) VALUES (1,'Alice',50000,'IT');
  기대: stdout에 "1 row inserted." 포함, exit 0

BB-02: SELECT 전체 행
  전제: BB-01 실행 후
  입력: SELECT * FROM employees;
  기대: "Alice" 포함 테이블 출력, "1 row(s) selected." 포함, exit 0

BB-03: SELECT WHERE 조건 일치
  입력: SELECT * FROM employees WHERE id = 1;
  기대: Alice 행 존재, exit 0

BB-04: SELECT WHERE 조건 불일치
  입력: SELECT * FROM employees WHERE id = 99;
  기대: "0 row(s) selected." 포함, exit 0

BB-05: SQL 파일 실행 (-f 옵션)
  입력 파일: 유효한 INSERT + SELECT SQL
  기대: 정상 출력, exit 0

BB-06: 다중 SQL 문 연속 실행
  입력: INSERT 2개 + SELECT 1개 (세미콜론 구분, 한 파일)
  기대: "2 row(s) selected." 포함, exit 0

[Error Path]
BB-E-01: 존재하지 않는 테이블
  입력: SELECT * FROM nonexistent;
  기대: stderr에 "[ERROR] Schema:" 포함, exit 2

BB-E-02: SQL 문법 오류
  입력: SELEC * FROM employees;
  기대: stderr에 "[ERROR] Parser:" 포함, exit 1

BB-E-03: 없는 SQL 파일 지정
  실행: ./sqlengine -f /tmp/missing_99999.sql
  기대: stderr에 오류 메시지, exit 3

BB-E-04: INT 컬럼에 문자열 값 삽입
  입력: INSERT INTO employees (id,name,salary,dept) VALUES ('notnum','A',0,'IT');
  기대: stderr에 "[ERROR] Executor:" 포함, exit 2

BB-E-05: 컬럼-값 수 불일치
  입력: INSERT INTO employees (id,name) VALUES (1,'A',9999);
  기대: stderr에 "[ERROR]" 포함, exit 2

[Edge Case]
BB-EG-01: 공백만 있는 SQL 파일
  기대: 오류 없이 exit 0

BB-EG-02: 주석만 있는 SQL 파일 (-- comment)
  기대: 오류 없이 exit 0

BB-EG-03: 100개 행 INSERT 후 SELECT
  기대: "100 row(s) selected." 포함, exit 0

BB-EG-04: 컬럼명 대소문자 혼합 (NAME, name, Name)
  기대: 대소문자 무관하게 동일 결과, exit 0

스크립트 구현 조건:
- 각 테스트 전 data/ 디렉토리 정리 (rm -f data/*.tbl)
- 출력 검증: stdout/stderr를 변수에 저장 후 grep으로 확인
- 종료 코드 검증: $? 로 확인
- 출력 형식: "[PASS] BB-01: INSERT single row" / "[FAIL] BB-01: expected exit 0, got 2"
- 최종 요약: "Blackbox Tests: N passed, M failed"
- 전체 실패 시 스크립트 exit 1

bash 문법 사용, POSIX 호환
```

---

## 전체 통합 빌드 프롬프트

위 Phase들을 완성한 후 전체 빌드가 안 될 경우 사용:

```
다음 C 프로젝트의 빌드 오류를 수정해줘.

프로젝트: 파일 기반 SQL 엔진
파일 목록: types.h, lexer.h/c, parser.h/c, schema.h/c, storage.h/c, executor.h/c, main.c
빌드 명령: gcc -std=c99 -Wall -Wextra src/*.c -o sqlengine

오류 내용:
[여기에 빌드 오류 붙여넣기]

수정 시 주의:
- 함수 시그니처 변경 최소화
- 기존 데이터 파일 포맷 유지
- API_SPEC.md의 인터페이스 준수
```
