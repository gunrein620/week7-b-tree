# AGENT.md — SQL Engine (C)

## 프로젝트 개요

C언어로 구현한 파일 기반 SQL 처리기입니다.
Oracle SQL 문법 기준의 INSERT/SELECT를 지원하며, 데이터는 파이프(`|`) 구분 텍스트 파일로 저장됩니다.

**도메인 기준**: `types.h`에 `MAX_NAME_LEN` / `MAX_GRADE_LEN` / `MAX_CLASS_LEN` 및 `MemberRecord`(id, name, grade, class, age)를 포함하고, 스키마·저장·예시 SQL은 기본 테이블 **`members`** 에 맞춥니다. 정의 전문은 `PROMPT.md` 「도메인 기준: MEMBERS 레코드」·`API_SPEC.md` 「도메인 기준 (MEMBERS)」와 동일합니다.

## 기술 스택

- **언어**: C (C99)
- **빌드**: GNU Make + GCC
- **저장소**: 파일 기반 (`.tbl` 데이터, `.schema` 스키마)
- **SQL 방언**: Oracle 문법 기준

## 모듈 구조

```
src/
├── main.c        CLI 진입점, 옵션 파싱, 실행 루프
├── types.h       공통 타입 정의 (Token, Statement, Row, Schema, MemberRecord 등)
├── lexer.c/h     토크나이저: SQL 문자열 → Token 배열
├── parser.c/h    파서: Token 배열 → Statement (재귀 하강 방식)
├── schema.c/h    스키마 파일 로더: schemas/{table}.schema 읽기
├── storage.c/h   스토리지 엔진: data/{table}.tbl 파일 I/O
└── executor.c/h  실행기: Statement → schema/storage 호출 + 결과 출력
tests/
├── test_lexer.c
├── test_parser.c
├── test_schema.c
└── test_storage.c
data/             테이블 데이터 파일 (*.tbl)
schemas/          스키마 정의 파일 (*.schema)
sql/              예시 및 테스트용 SQL 파일
```

## 핵심 데이터 흐름

```
SQL 입력 (파일 또는 문자열)
    │
    ▼
tokenize()          lexer.c
    │  Token[]
    ▼
parse()             parser.c
    │  Statement*
    ▼
execute()           executor.c
    ├── schema_load()    schema.c   → schemas/{table}.schema
    ├── storage_insert() storage.c  → data/{table}.tbl (append)
    └── storage_select() storage.c  → data/{table}.tbl (read + filter)
```

## 파일 포맷

### 스키마 파일 (`schemas/{table}.schema`)

`members` 기준 예시:

```
# column_name,type,max_length,nullable,primary_key
id,INT,0,0,1
name,VARCHAR,32,0,0
grade,VARCHAR,16,1,0
class,VARCHAR,16,1,0
age,INT,0,1,0
```

### 데이터 파일 (`data/{table}.tbl`)

`data/members.tbl` 예시:

```
id|name|grade|class|age
1|Alice|vip|advanced|30
2|Bob|normal|basic|22
```

- 첫 줄: 컬럼 헤더 (파이프 구분)
- 이후: 데이터 행 (파이프 구분)
- NULL 값: 빈 필드로 표현 (`||`)

## CLI 사용법

```bash
./sqlengine -f query.sql          # SQL 파일 실행
./sqlengine -e "SELECT * FROM t;" # SQL 직접 실행
./sqlengine                       # 인터랙티브 모드 (sql> 프롬프트)
./sqlengine -d /path/data -s /path/schemas -f query.sql
```

## 빌드 및 테스트

```bash
make          # 빌드
make debug    # 디버그 빌드 (-g -DDEBUG)
make test     # 단위 테스트 실행
make clean    # 빌드 결과물 삭제
```

## 지원 SQL 문법

```sql
-- INSERT
INSERT INTO members (id, name, grade, class, age) VALUES (1, 'Alice', 'vip', 'advanced', 30);

-- SELECT 전체
SELECT * FROM members;

-- SELECT 컬럼 지정
SELECT id, name FROM members;

-- SELECT WHERE (단일 조건)
SELECT * FROM members WHERE grade = 'vip';

-- SELECT WHERE (복합 조건)
SELECT * FROM members WHERE grade = 'vip' AND age >= 25;
SELECT * FROM members WHERE class = 'advanced' OR class = 'basic';

-- 지원 연산자: =  !=  <>  <  >  <=  >=
```

## 코드 작업 시 주의사항

### 함수 추가/수정 시

- 모든 공개 함수는 `types.h`의 타입만 사용 (모듈 간 의존성 최소화)
- 오류 출력 형식 통일: `fprintf(stderr, "[ERROR] ModuleName: message\n")`
- 동적 할당 후 반드시 대응하는 `free_*` 함수 구현
- NULL 포인터 역참조 전 반드시 NULL 체크

### 스토리지 수정 시

- 데이터 파일 포맷(파이프 구분)은 변경하지 말 것 — 기존 `.tbl` 파일과 호환성 유지
- `storage_insert`는 append 모드로만 열기 (기존 데이터 덮어쓰기 금지)
- 파일 오픈 실패 시 즉시 반환 (프로그램 종료 금지)

### 파서 수정 시

- 재귀 하강 방식 유지 (각 문법 요소마다 독립 함수)
- 토큰 소비 전 peek으로 확인하는 패턴 유지
- `WHERE` 절 조건은 `WhereClause` 구조체로만 표현

### 테스트 작성 시

- 각 테스트 함수는 `[PASS]`/`[FAIL]` 형식으로 출력
- 임시 파일 사용 시 테스트 종료 후 반드시 삭제
- 실패해도 이후 테스트는 계속 실행 (early exit 금지)
- 화이트박스 테스트는 내부 함수를 직접 호출하여 반환값/상태 검증
- 블랙박스 테스트는 `./sqlengine` 바이너리만 사용, 내부 함수 직접 호출 금지

## 테스트 전략

### 화이트박스 테스트 (Whitebox)

소스 코드의 분기와 내부 상태를 검증합니다. 테스트 대상 함수를 직접 호출합니다.

```
tests/
├── test_lexer.c     — tokenize() 분기 커버리지 (대소문자, 연산자, 오류 경로)
├── test_parser.c    — parse() AST 구조 검증 (is_star, WHERE 조건, 오류 경로)
├── test_schema.c    — schema_load() 파싱 경로 (정상, 파일 없음, 잘못된 타입)
├── test_storage.c   — storage_insert/select 분기 (신규/append, 필터 참/거짓)
└── test_executor.c  — execute() 내부 검증 (타입 오류, 컬럼 불일치, 정상 실행)
```

**핵심 원칙**
- 각 테스트는 하나의 분기/경로만 검증 (단일 책임)
- 임시 파일 경로: `/tmp/wb_test_<모듈>_<케이스번호>.tbl`
- 오류 경로 검증 시 반환값(`NULL`, `-1`)과 stderr 출력 모두 확인

**빌드 및 실행**
```bash
make test           # 모든 화이트박스 단위 테스트 실행
./tests/test_lexer  # 개별 테스트 실행
```

---

### 블랙박스 테스트 (Blackbox)

`./sqlengine` 바이너리의 외부 동작만 검증합니다. 내부 구현 지식 없이 CLI 입출력과 종료 코드로 판단합니다.

```
tests/blackbox/
├── run_tests.sh          — 전체 블랙박스 테스트 실행
├── cases/
│   ├── bb_insert.sql     — INSERT 테스트 SQL
│   ├── bb_select.sql     — SELECT 테스트 SQL
│   ├── bb_where.sql      — WHERE 조건 테스트 SQL
│   └── bb_edge.sql       — 엣지 케이스 SQL
└── expected/
    ├── bb_select.txt     — 예상 stdout 출력
    └── bb_where.txt      — 예상 stdout 출력
```

**테스트 카테고리**

| 카테고리 | 검증 항목 |
|----------|-----------|
| Happy Path | 정상 INSERT/SELECT, 파일 실행, 다중 SQL |
| Error Path | 문법 오류(exit 1), 실행 오류(exit 2), 파일 오류(exit 3) |
| Edge Case | 공백 파일, 주석 파일, 대량 데이터, 대소문자 혼합 |

**핵심 원칙**
- 각 테스트 전 `data/*.tbl` 초기화 (이전 테스트 영향 차단)
- stdout/stderr 분리 검증 (`2>/tmp/stderr.txt`)
- 종료 코드(`$?`)와 출력 내용 모두 검증

**빌드 및 실행**
```bash
make test-blackbox          # 블랙박스 테스트 실행
bash tests/blackbox/run_tests.sh  # 직접 실행
```

---

### 테스트 실행 순서 (권장)

```
1. make test              화이트박스 단위 테스트 (모듈 수준 검증)
2. make test-blackbox     블랙박스 기능 테스트 (CLI 수준 검증)
3. 수동 인터랙티브 테스트  sql> 프롬프트에서 직접 SQL 입력
```

### 테스트 실패 시 대응

| 실패 유형 | 확인 위치 | 대응 방법 |
|-----------|-----------|-----------|
| 화이트박스 Lexer 실패 | `lexer.c` 해당 분기 | 토큰 인식 로직 수정 |
| 화이트박스 Parser 실패 | `parser.c` 파싱 함수 | peek/advance 흐름 점검 |
| 블랙박스 exit 코드 불일치 | `main.c` 종료 코드 | 오류 전파 경로 추적 |
| 블랙박스 출력 형식 불일치 | `executor.c` 출력 함수 | 테이블 출력 포맷 수정 |

## 확장 포인트

향후 DELETE/UPDATE 추가 시:

1. `types.h`: `StatementType`에 `STMT_DELETE`, `STMT_UPDATE` 추가
2. `parser.c`: `parse_delete()`, `parse_update()` 함수 추가
3. `storage.c`: `storage_delete()`, `storage_update()` 함수 추가
   - 파일 전체를 읽어 필터링 후 임시 파일에 쓰고, 원본 교체
4. `executor.c`: `execute_delete()`, `execute_update()` 추가
5. `execute()` 디스패치 함수에 새 case 추가

임시 파일 교체 패턴 (DELETE/UPDATE용):
```c
// data/{table}.tbl.tmp 에 쓰고
// rename("data/{table}.tbl.tmp", "data/{table}.tbl")
```

## 알려진 제약사항

- MEMBERS 컬럼 문자열은 `MAX_NAME_LEN` / `MAX_GRADE_LEN` / `MAX_CLASS_LEN`을 넘지 않도록 저장·검증 정책을 `PROMPT.md`와 맞출 것
- 스키마와 테이블 파일은 사전에 존재해야 함 (`CREATE TABLE` 미구현)
- 단일 SQL 문에서 WHERE 절의 AND/OR 혼합 사용 시 우선순위 없이 순서대로 처리
- 최대 컬럼 수: 32 (`MAX_COLUMNS`)
- 최대 행 수: 10,000 (`MAX_ROWS`, `storage_select` 메모리 한계)
- 중첩 쿼리(서브쿼리), JOIN, 집계 함수 미지원
