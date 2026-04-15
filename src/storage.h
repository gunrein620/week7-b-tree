#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>

#include "types.h"

/* out_offset 이 NULL 이 아니면 삽입 직전 파일 오프셋(해당 행의 시작 위치)을
 * 기록한다. B+ 트리 인덱스가 "id → 파일 오프셋" 을 보관하는 데 사용한다. */
int storage_insert(const char *table_name,
                   Row *row,
                   Schema *schema,
                   int64_t *out_offset);

/* 인덱스 히트 시 해당 오프셋에서 단일 행만 읽어 out 에 채운다.
 * 성공 1, 실패 0. */
int storage_read_row_at(const char *table_name,
                        Schema *schema,
                        int64_t offset,
                        Row *out);

ResultSet *storage_select(const char *table_name,
                          Schema *schema,
                          ColumnList *columns,
                          WhereClause *where);
void free_result_set(ResultSet *rs);
int evaluate_condition(Row *row, Schema *schema, Condition *cond);
int evaluate_where(Row *row, Schema *schema, WhereClause *where);

#endif
