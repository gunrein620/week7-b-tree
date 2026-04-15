#ifndef BTREE_H
#define BTREE_H

#include <stdint.h>
#include <stddef.h>

/* 메모리 기반 B+ 트리.
 * Key:   int32_t (레코드의 기본키 id)
 * Value: int64_t (.tbl 파일 내 해당 행의 시작 바이트 오프셋)
 */

typedef struct BTree BTree;
typedef int (*BTreeVisitFn)(int32_t key, int64_t offset, void *ctx);

BTree  *btree_create(void);
void    btree_free(BTree *tree);

/* 성공 0, 중복 키 -1, 메모리 오류 -2 */
int     btree_insert(BTree *tree, int32_t key, int64_t offset);

/* 발견 1 (out_offset 채움), 없음 0 */
int     btree_find(BTree *tree, int32_t key, int64_t *out_offset);

/* 비어 있으면 0 반환. 자동 증가 ID 계산에 사용. */
int32_t btree_max_key(BTree *tree);

size_t  btree_size(BTree *tree);
int     btree_visit_in_order(BTree *tree, BTreeVisitFn visitor, void *ctx);

#endif
