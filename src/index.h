#ifndef INDEX_H
#define INDEX_H

#include "btree.h"
#include "types.h"

/* 테이블 이름 → B+ 트리(PK 인덱스) 캐시.
 *
 * - index_get_or_build:  캐시에 있으면 반환, 없으면 해당 테이블의 .tbl 파일을
 *                        한 번 풀스캔해 "id → 파일 오프셋" 맵을 구축하고 .idx 로 저장.
 * - index_lookup_offset: 캐시/퍼시스턴트 .idx/.tbl 재빌드 중 가능한 가장 빠른 경로로
 *                        PK 오프셋을 조회한다.
 * - index_invalidate_persisted: 테이블 변경으로 stale 해진 .idx 를 무효화한다.
 * - index_drop_all:      프로세스 종료/테스트 정리에 사용.
 *
 * 현 엔진은 PK 가 INT 한 컬럼인 members 스키마를 가정한다.
 * PK 가 INT 가 아니면 NULL 을 반환한다.
 */
BTree *index_get_or_build(const char *table_name, Schema *schema);
int     index_lookup_offset(const char *table_name,
                            Schema *schema,
                            int32_t key,
                            int64_t *out_offset);
void    index_invalidate_persisted(const char *table_name);
void    index_record_insert(const char *table_name, int32_t key, int64_t row_offset);
void   index_drop_all(void);

/* 테스트/진단용: 해당 테이블의 인덱스를 강제로 해제. */
void   index_drop(const char *table_name);

#endif
