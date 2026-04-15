# week7-b-tree

Jungle 12기 7주차 과제 — **B+ 트리 인덱스**를 기존 SQL 엔진에 얹어,
`WHERE id = ?` 쿼리를 선형 탐색이 아닌 인덱스 탐색으로 처리하도록 만드는 프로젝트.

## 왜 만들었나

데이터베이스 강의에서 "인덱스를 쓰면 빠르다"는 말을 수도 없이 들었지만,
실제로 **얼마나 빠른지, 왜 빠른지** 숫자로 본 적이 없었다.

6주차에서 파일 기반 SQL 엔진을 직접 구현하면서 `WHERE id = ?` 쿼리가
레코드 수만큼 파일을 통째로 읽는다는 걸 알게 됐다.
100만 건이 쌓이면 한 번의 조회에도 전체를 다 뒤진다.

그래서 7주차 목표는 단순하다.
**같은 SQL 엔진 위에 B+ 트리 인덱스를 직접 얹고, 속도 차이를 눈으로 확인한다.**

DBMS 교과서의 그림이 아니라, 내 손으로 짠 코드로 O(N) vs O(log N)을 재보는 것.

## 프로젝트 핵심

이 프로젝트의 핵심은 **B+ 트리가 SQL 엔진에 어떻게 연결되는가**다.

```
INSERT 할 때마다  →  id와 파일 오프셋을 B+ 트리에 등록
WHERE id = ? 할 때  →  B+ 트리로 오프셋을 찾아 그 위치만 읽기
```

인덱스가 없으면 `storage_select()`가 `.tbl` 파일을 처음부터 끝까지 읽는다(O(N)).
인덱스가 있으면 `btree_find()`로 파일 오프셋을 꺼내 `storage_read_row_at()`으로 딱 한 줄만 읽는다(O(log N)).

구현의 핵심 결정 세 가지:

1. **B+ 트리를 메모리에만 둔다** — 디스크 기반 페이지 관리를 생략하고 탐색 알고리즘 자체에 집중
2. **인덱스는 lazy build** — 최초 조회 시점에 `.tbl` 풀스캔으로 한 번만 구축, 이후 캐시 재사용
3. **기존 인터페이스 최소 변경** — `storage_insert()`에 `out_offset` 파라미터만 추가해 6주차 코드를 거의 그대로 유지

## 목표

실제 DBMS에서 쓰이는 디스크 기반 B+ 트리를 **메모리 기반으로 단순화**해 구현하고,
6주차에서 만든 SQL 처리기와 매끄럽게 연동한다.

- 테이블에 레코드 INSERT 시 ID가 자동 부여되고, 해당 ID가 B+ 트리에 등록된다.
- `SELECT ... WHERE id = ?` 는 B+ 트리를 통해 O(log N)으로 탐색한다.
- 100만 건 이상 대용량 INSERT 후, **ID 기준 SELECT(인덱스)** 와
  **다른 필드 기준 SELECT(선형 탐색)** 의 실행 시간을 비교한다.
- 구현 언어는 **C** 이며, 6주차 SQL 엔진을 그대로 사용한다.

## 중점 포인트

1. `WHERE id = ?` 조건에서 B+ 트리를 어떻게 태울지 (executor 훅 포인트 설계)
2. 100만+ 레코드를 쉽게 생성하고 벤치마크할 수 있는 도구/스크립트
3. 기존 SQL 처리기와의 매끄러운 연결 (스토리지·실행기 인터페이스 최소 변경)

## 개발 환경 / 빌드

6주차 SQL 엔진(`week6-c-sql-engine`)을 이 디렉토리로 가져와 베이스로 사용한다.

```bash
make            # sqlengine 빌드 (릴리스)
make debug      # 디버그 빌드
make test       # 유닛 테스트 실행
make clean      # 빌드 아티팩트 정리
```

현재 상태: 6주차 SQL 엔진 이식 완료, 전체 유닛 테스트 통과. B+ 트리 모듈은 미구현.

## 디렉토리 구조

```
week7-b-tree/
├── src/                        # SQL 엔진 소스
│   ├── main.c                  # 진입점 · REPL 루프
│   ├── types.h                 # 공용 타입 정의 (Token, Statement, Schema, Row …)
│   ├── lexer.c / lexer.h       # 어휘 분석 — SQL 문자열 → Token 스트림
│   ├── parser.c / parser.h     # 구문 분석 — Token 스트림 → Statement
│   ├── executor.c / executor.h # 실행기 — Statement → INSERT/SELECT 동작
│   ├── storage.c / storage.h   # 파일 기반 스토리지 (.tbl 고정폭 레코드)
│   ├── schema.c / schema.h     # 스키마 파일 로드 · 관리
│   ├── config.c / config.h     # 데이터 경로 등 전역 설정
│   ├── btree.c / btree.h       # 메모리 기반 B+ 트리 (key: id → value: offset)
│   └── index.c / index.h       # 인덱스 캐시 (테이블명 → BTree*, lazy build)
├── tests/                      # 유닛 테스트
│   ├── test_btree.c            # B+ 트리 단독 테스트
│   ├── test_index_integration.c# 인덱스 통합 테스트
│   ├── test_executor.c         # 실행기 테스트
│   ├── test_storage.c          # 스토리지 테스트
│   ├── test_lexer.c            # 어휘 분석기 테스트
│   ├── test_parser.c           # 파서 테스트
│   ├── test_schema.c           # 스키마 테스트
│   ├── test_cli.c              # CLI E2E 테스트
│   └── test_helpers.h          # 공용 테스트 유틸리티
├── tools/
│   └── gen_members.c           # 대용량 레코드 생성기 (벤치마크용)
├── sql/                        # 예제 SQL 스크립트
│   ├── members_30.sql
│   └── members_demo.sql
├── schemas/                    # 테이블 스키마 정의 파일
│   └── members.schema
├── data/                       # 런타임 데이터 (자동 생성)
│   └── members.tbl
├── assets/                     # 문서 이미지 · 다이어그램
├── build/                      # 컴파일 아티팩트 (자동 생성)
├── Makefile
├── README.md
├── USAGE.md
└── API_SPEC.md
```

## 아키텍처

### 전체 처리 흐름

![sqlengine 아키텍처](assets/sqlengine_architecture_v2.svg)

### 모듈 역할 요약

| 모듈 | 파일 | 역할 |
|------|------|------|
| **Lexer** | `lexer.c` | SQL 문자열을 Token 스트림으로 변환 |
| **Parser** | `parser.c` | Token 스트림을 `Statement` 구조체로 파싱 |
| **Executor** | `executor.c` | `Statement`를 실제 INSERT/SELECT 동작으로 실행 |
| **Storage** | `storage.c` | `.tbl` 파일에 고정폭 레코드를 읽기/쓰기 |
| **Schema** | `schema.c` | `.schema` 파일을 파싱해 `Schema` 구조체로 로드 |
| **BTree** | `btree.c` | 메모리 기반 B+ 트리 (`id → 파일 오프셋`) |
| **Index** | `index.c` | 테이블별 BTree 캐시, 첫 접근 시 lazy build |
| **Config** | `config.c` | 데이터 디렉토리 경로 등 전역 설정 관리 |

### INSERT 흐름

```
INSERT 문
  └─► executor_insert()
        ├─► storage_insert()  ← .tbl 에 레코드 append, 파일 오프셋 반환
        └─► index_get_or_build() → btree_insert(id, offset)
```

### SELECT (WHERE id = ?) 흐름

```
SELECT ... WHERE id = N
  └─► executor_select()
        ├─► index_get_or_build()   ← 인덱스 캐시 조회 (없으면 full-scan 빌드)
        ├─► btree_find(id)         ← O(log N) 탐색 → 파일 오프셋 획득
        └─► storage_read_row_at(offset)  ← 해당 오프셋 단일 행 읽기
```

## 로드맵

- [x] 6주차 SQL 엔진 이식 및 빌드·테스트 확인
- [ ] B+ 트리 모듈 설계 및 단독 구현 + 유닛 테스트
- [ ] INSERT 시 인덱스 자동 등록 (storage/executor 연동)
- [ ] `WHERE id = ?` 경로에서 인덱스 탐색 사용
- [ ] 100만+ 레코드 생성기 및 벤치마크 스크립트
- [ ] 인덱스 사용 vs 선형 탐색 성능 비교 리포트
- [ ] 추가 차별화 요소 (범위 검색, 복합 인덱스, persist 등 검토)

