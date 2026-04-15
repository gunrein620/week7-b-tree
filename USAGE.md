# B+ Tree Index 사용 가이드

week7 과제 — week6 SQL 엔진에 붙인 **in-memory B+ 트리 인덱스**의 사용법, 테스트, 시연 시나리오.

## 1. 빌드

```bash
make          # sqlengine 본체 (release)
make debug    # 디버그 심볼
make tools    # tools/gen_members (대량 데이터 생성기)
make test     # 전체 단위/통합 테스트
make clean    # 아티팩트 정리
```

빌드 산출물:
- `./sqlengine`  — SQL 엔진 CLI
- `./tools/gen_members` — 대량 더미 데이터 생성기
- `./build/tests/test_*` — 개별 테스트 실행파일

## 2. CLI 개요

```
./sqlengine [-f file | -e sql] [-d data_dir] [-s schema_dir]
            [--bench <table> [--runs <n>]]
            [--help | --version]
```

| 옵션 | 설명 |
| --- | --- |
| `-f <path>` | 파일의 SQL 스크립트 실행 |
| `-e <sql>`  | 인라인 SQL 실행 |
| `-d <dir>`  | 데이터 디렉터리 (기본 `./data`) |
| `-s <dir>`  | 스키마 디렉터리 (기본 `./schemas`) |
| `--bench <table>` | 해당 테이블에서 인덱스/선형 SELECT 벤치 |
| `--runs <n>` | `--bench` 반복 횟수 (기본 5) |

스키마는 `schemas/members.schema` 가 기본으로 제공된다:

```
id,INT,0,0,1
name,VARCHAR,32,0,0
grade,VARCHAR,16,1,0
class,VARCHAR,16,1,0
age,INT,0,1,0
```

## 3. 기본 사용

### 3.1 인라인 SQL

```bash
# id 자동 부여 (PK 생략)
./sqlengine -e "INSERT INTO members (name, age) VALUES ('alice', 30);"

# id 명시
./sqlengine -e "INSERT INTO members (id, name, age) VALUES (100, 'bob', 25);"

# 인덱스 경로 (O(log n))
./sqlengine -e "SELECT * FROM members WHERE id = 500000;"

# 선형 경로 (O(n)) — 인덱스 없는 컬럼
./sqlengine -e "SELECT * FROM members WHERE name = 'name_0500000';"
```

### 3.2 SQL 스크립트 파일

```bash
./sqlengine -f sql/members_30.sql
make run-f SQL=sql/members_demo.sql
```

## 4. 대량 데이터 생성 (`tools/gen_members`)

```
./tools/gen_members <count> [--mode tbl|sql] [--out path]
```

### 4.1 TBL 모드 (기본, 벤치마크용)

`.tbl` 파일을 직접 기록. 100만 행도 수 초.

```bash
./tools/gen_members 1000000                       # data/members.tbl 에 기록
./tools/gen_members 500000 --out /tmp/members.tbl # 경로 지정
```

기록된 파일은 다음 쿼리 시 `IndexManager` 가 **lazy build** 로 자동 인덱싱한다.
생성된 인덱스는 `data/<table>.idx` 에 저장되어, 이후 새 프로세스에서도 재사용된다.

### 4.2 SQL 모드 (INSERT 경로 검증용)

`INSERT` 문을 N개 출력. `executor → build_insert_row → auto-increment → btree_insert` 전체 경로를 실제로 거친다. 속도는 TBL 모드보다 훨씬 느리므로 **correctness 용**.

```bash
./tools/gen_members 10000 --mode sql --out /tmp/seed.sql
./sqlengine -f /tmp/seed.sql
```

## 5. 벤치마크 실행

```bash
./sqlengine --bench members --runs 5
```

출력 예 (1,000,000 행 기준):

```
[BENCH] table=members rows=1000000 build=92956.0 us
[BENCH] target id=500000 name='name_0500000' runs=5
[BENCH] indexed avg:      49.00 us
[BENCH] linear  avg:  145965.80 us
[BENCH] speedup    :    2978.89x
```

- **build**: `.tbl` 스캔 후 B+ 트리 구축 시간 (lazy build 1회 비용)
- **indexed avg**: `btree_find` + 파일 seek 한 번 (O(log n) + 1 seek)
- **linear avg**: `storage_select` 의 전체 파일 라인스캔
- **speedup**: `linear / indexed`

`--runs` 를 올리면 노이즈가 줄고, 낮추면 빠르게 확인 가능.

## 6. 테스트

```bash
make test
```

- 8 개 테스트 실행파일, 총 40+ 개 케이스
- 프레임워크 없이 C 함수 기반 (`tests/test_helpers.h`)
- 각 테스트는 `/tmp/sqlengine_*` 에 격리된 워크스페이스를 만들고 정리함

### 6.1 단위 테스트 — `tests/test_btree.c`

| 케이스 | 검증 |
| --- | --- |
| `empty_tree` | 빈 트리에서 `find` 실패, `size=0`, `max_key=0` |
| `single_insert_find` | 단일 삽입/조회 |
| `duplicate_rejected` | 중복 키는 `-1` |
| `sequential_insert_and_find` | 1..5000 순차 삽입 + 전수 조회 (split 반복) |
| `reverse_insert_and_find` | 3000..1 역순 삽입 + 전수 조회 |
| `random_like_insert` | 결정적 의사 순열로 4096 삽입 |
| `split_boundaries` | ORDER(64) 경계 주변 삽입/조회, 부재 키 조회 |

### 6.2 통합 테스트 — `tests/test_index_integration.c`

| 케이스 | 검증 |
| --- | --- |
| `auto_increment` | id 미지정 INSERT 가 1, 2, 3 순으로 자동 부여 |
| `explicit_id_and_duplicate` | 명시 id 허용 + 중복은 btree 기반 O(log n) 거부 |
| `lazy_build_and_read` | `.tbl` 만 있는 상태에서 lazy build + `storage_read_row_at` |
| `index_select_matches_linear` | 인덱스 경로와 선형 경로 결과가 동일 |

### 6.3 개별 테스트 실행

```bash
./build/tests/test_btree
./build/tests/test_index_integration
./build/tests/test_storage
```

## 7. 시연 시나리오

### 7.1 "100만 건 스피드 비교" (핵심 데모)

```bash
# 1) 빌드
make && make tools

# 2) 100만 행 준비 (~3초)
./tools/gen_members 1000000

# 3) 벤치 실행
./sqlengine --bench members --runs 5

# 4) REPL 체감 비교
./sqlengine -e "SELECT * FROM members WHERE id = 7777777;"       # 즉시
./sqlengine -e "SELECT * FROM members WHERE name = 'name_7777777';"  # 체감되는 지연
```

기대: 인덱스 경로 수십 μs, 선형 경로 100ms 이상, **2000~3000x speedup**.

### 7.2 "Auto-increment + 중복 차단" (INSERT 경로 데모)

```bash
# 깨끗한 상태에서 시작
rm -f data/members.tbl

# 1..3 자동 부여
./sqlengine -e "INSERT INTO members (name, age) VALUES ('alice', 30);"
./sqlengine -e "INSERT INTO members (name, age) VALUES ('bob', 25);"
./sqlengine -e "INSERT INTO members (name, age) VALUES ('carol', 22);"

# 명시 id 삽입 (100)
./sqlengine -e "INSERT INTO members (id, name, age) VALUES (100, 'explicit', 40);"

# 다음 자동 부여는 101 (btree_max_key + 1)
./sqlengine -e "INSERT INTO members (name, age) VALUES ('next', 31);"
./sqlengine -e "SELECT * FROM members;"

# 중복 id 100 은 거부됨 (btree_find → O(log n))
./sqlengine -e "INSERT INTO members (id, name, age) VALUES (100, 'dup', 99);"
# → [ERROR] Executor: duplicate primary key for column 'id'
```

### 7.3 "SQL 스크립트로 INSERT 경로 전체 검증"

```bash
./tools/gen_members 10000 --mode sql --out /tmp/seed.sql
rm -f data/members.tbl
./sqlengine -f /tmp/seed.sql >/dev/null
./sqlengine --bench members --runs 3
```

실제 `executor` 경로로 10000 건이 들어가면서 `btree_insert` 가 10000 번 호출된다. 이후 벤치로 인덱스가 올바르게 구축되었는지 확인.

### 7.4 "lazy build 단독 시연"

```bash
# .tbl 만 존재하는 상태에서 엔진이 자동으로 인덱스 구축
rm -f data/members.tbl
./tools/gen_members 500000
./sqlengine -e "SELECT * FROM members WHERE id = 123456;"
# 첫 쿼리에서 IndexManager 가 한 번 풀스캔하여 트리를 만들고 .idx 를 저장한 뒤 O(log n) 조회
# 이후 프로세스들은 .idx 를 바로 사용하므로 다시 .tbl 전체를 스캔하지 않는다
```

## 8. 아키텍처 연결 지점 (핵심 로직 포인터)

| 파일 | 역할 | 주요 함수 |
| --- | --- | --- |
| `src/btree.c` | B+ 트리 (order 64, leaf 연결 리스트) | `btree_insert`, `btree_find`, `btree_max_key`, `split_leaf`, `split_internal` |
| `src/index.c` | 테이블→트리 캐시, lazy build | `index_get_or_build`, `build_from_file` |
| `src/storage.c` | `.tbl` I/O + 오프셋 반환 | `storage_insert` (out_offset), `storage_read_row_at` |
| `src/executor.c` | auto-increment, PK 중복 검사, 인덱스 SELECT 경로 | `auto_assign_pk`, `ensure_primary_key_is_unique`, `try_index_select`, `execute_insert`, `execute_select` |
| `src/main.c` | CLI, `--bench` | `run_benchmark` |
| `tools/gen_members.c` | 대량 더미 데이터 생성기 | `write_row_tbl`, `write_row_sql` |

### 요청이 흐르는 순서

**`SELECT * FROM members WHERE id = 500000`**
1. `main.c → run_sql_script → run_single_statement`
2. `lexer → parser → execute → execute_select`
3. `try_index_select` 가 "단일 동등 + PK" 판정
4. `index_get_or_build` 로 트리 확보 (최초 1회만 풀스캔하여 lazy build)
5. `btree_find(500000)` → 파일 오프셋 반환
6. `storage_read_row_at` 로 단일 행만 `fseek` + `fgets`
7. 결과 출력

**`INSERT INTO members (name, age) VALUES ('x', 30)`** (id 생략)
1. `execute_insert → build_insert_row`
2. PK 미지정 → `auto_assign_pk` → `btree_max_key + 1`
3. 중복 검사 스킵 (자동증가는 유일성 보장)
4. `storage_insert(..., &offset)` → 파일 append + `ftell`
5. `btree_insert(id, offset)` 로 인덱스 등록

## 9. 트러블슈팅

| 증상 | 원인 / 해결 |
| --- | --- |
| `make test` 실패 | `make clean && make test` 로 클린 빌드 |
| 벤치 speedup 이 작음 | `.tbl` 이 작은 경우. `tools/gen_members 1000000` 로 확장 |
| `INSERT` 가 `type mismatch` 에러 | `INT` 컬럼에 문자열 넣는 등 스키마 위반. `schemas/members.schema` 확인 |
| 인덱스가 생기지 않음 | PK 가 `INT` 가 아니면 인덱스 생성 스킵 (현재 엔진 제약) |
| `data/members.tbl` 손상 / 오래됨 | 삭제 후 재생성: `rm data/members.tbl && ./tools/gen_members 1000000` |

## 10. 현재 제약사항

- **하이브리드 인덱스**: 프로세스 내부 B+ 트리 캐시에 더해 `data/<table>.idx` 를 저장한다.
  새 프로세스는 이 파일을 이용해 PK 조회를 바로 수행하고, INSERT 가 발생하면 .idx 를 무효화한 뒤 다음 조회 시 재생성한다.
- **INT PK 전용**: 다른 타입 PK 는 인덱싱하지 않음
- **단일 동등 WHERE 만 최적화**: `WHERE id = K` 한정. `BETWEEN`, `IN`, `>=` 등은 선형 스캔
- **`ResultSet.rows[MAX_ROWS=10000]`**: 풀스캔 결과가 10000 을 넘으면 잘림. 단일 매치 벤치에서는 영향 없음

## 11. 자주 쓰는 명령어

### 11.1 100만 데이터 생성

```bash
./tools/gen_members 1000000
```

### 11.2 벤치마크 실행

```bash
./sqlengine --bench members --runs 5
```

### 11.3 `name` 값으로 선형 탐색

```bash
./sqlengine -e "SELECT * FROM members WHERE name = 'name_0777777';"
```

### 11.4 `id` 값으로 B+ 트리 탐색

```bash
./sqlengine -e "SELECT * FROM members WHERE id = 777777;"
```

### 11.5 두 경로를 한 번에 비교

```bash
time ./sqlengine -e "SELECT * FROM members WHERE id = 777777; SELECT * FROM members WHERE name = 'name_0777777';"
```
