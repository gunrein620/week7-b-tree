# edgecase_test

`edgecase_test/`는 기존 `tests/`와 분리된 엣지 케이스 전용 테스트 모음입니다.
기본 회귀 테스트에 바로 넣기보다는, 실제 버그 가능성이 높은 경계 조건을 따로 모아서 확인하는 용도로 사용합니다.

## 왜 따로 분리했나

- 기존 `make test` 흐름은 안정적인 회귀 확인에 집중합니다.
- `edgecase_test`는 현재 구현의 취약점이나 미지원 동작을 일부러 드러내는 테스트를 포함합니다.
- 그래서 이 폴더의 테스트는 실패가 나와도 이상한 것이 아니라, 수정이 필요한 지점을 보여주는 신호일 수 있습니다.

## 실행 방법

프로젝트 루트에서 실행합니다.

macOS / Linux:

```bash
cd /Users/kunwoopark/WS/jungle-12/week7-b-tree
make edge-test
```

PowerShell:

```powershell
cd C:\Users\kjh\Desktop\week7_WD_project\week7-b-tree
make edge-test
```

Git Bash:

```bash
cd /c/Users/kjh/Desktop/week7_WD_project/week7-b-tree
make edge-test
```

개별 테스트는 빌드와 실행을 따로 할 수 있습니다.
직접 실행할 때는 먼저 해당 바이너리를 한 번 빌드해 두는 것이 안전합니다.

### 1. PK 경계값 테스트: `test_pk_boundaries`

확인하는 내용:

- `INT32_MAX` 직접 삽입이 가능한지
- 최대 PK 다음 auto-increment가 overflow 없이 실패하는지
- `WHERE id = 2147483648` 같은 범위 초과 조회가 기존 row를 잘못 찾지 않는지

개별 빌드:

```bash
make build/edgecase_test/test_pk_boundaries
```

개별 실행:

macOS / Linux

```bash
./build/edgecase_test/test_pk_boundaries
```

Windows PowerShell

```powershell
.\build\edgecase_test\test_pk_boundaries.exe
```

### 2. 인덱스 복원력 테스트: `test_index_resilience`

확인하는 내용:

- 손상된 `members.idx`가 있어도 `.tbl` 기준으로 다시 복구되는지
- 프로세스가 살아 있는 동안 `.tbl`이 외부 변경되었을 때 stale in-memory index 문제가 드러나는지

개별 빌드:

```bash
make build/edgecase_test/test_index_resilience
```

개별 실행:

macOS / Linux

```bash
./build/edgecase_test/test_index_resilience
```

Windows PowerShell

```powershell
.\build\edgecase_test\test_index_resilience.exe
```

### 3. 토큰 길이 제한 테스트: `test_token_limits`

확인하는 내용:

- 256자를 넘는 긴 문자열 리터럴이 조용히 잘리지 않고 실패하는지
- 256자를 넘는 긴 식별자가 조용히 잘리지 않고 실패하는지

개별 빌드:

```bash
make build/edgecase_test/test_token_limits
```

개별 실행:

macOS / Linux

```bash
./build/edgecase_test/test_token_limits
```

Windows PowerShell

```powershell
.\build\edgecase_test\test_token_limits.exe
```

빌드 산출물은 `build/edgecase_test/` 아래에 생성됩니다.

## 현재 포함된 테스트

### `test_pk_boundaries.c`

- `INT32_MAX` 직접 삽입이 가능한지 확인
- 최대 PK 이후 auto-increment가 overflow 없이 안전하게 막히는지 확인
- `WHERE id = 2147483648` 같은 범위 초과 조회가 기존 row를 잘못 매칭하지 않는지 확인

### `test_index_resilience.c`

- 손상된 persisted `.idx` 파일이 있어도 `.tbl` 기준으로 다시 복구할 수 있는지 확인
- 프로세스가 살아 있는 동안 `.tbl`이 외부에서 바뀌었을 때, stale in-memory index 문제가 드러나는지 확인

### `test_token_limits.c`

- 256자를 넘는 긴 문자열 리터럴이 조용히 잘리지 않고 명시적으로 실패하는지 확인
- 256자를 넘는 긴 식별자가 조용히 잘리지 않고 명시적으로 실패하는지 확인

## 결과 해석

- `[PASS]`는 현재 구현이 해당 경계 조건을 안전하게 처리했다는 뜻입니다.
- `[FAIL]`는 실제 버그, 누락된 검증, 또는 아직 정의되지 않은 동작이 있다는 뜻입니다.
- 이 스위트는 일부 케이스가 현재 red 상태여도 괜찮습니다. 목적은 숨겨진 문제를 빨리 드러내는 것입니다.

## 참고

- 이 테스트들은 기존 `tests/test_helpers.h`를 재사용합니다.
- `make test`에는 포함되지 않으며, `make edge-test`에서만 실행됩니다.
