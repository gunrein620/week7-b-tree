#ifndef STORAGE_H
#define STORAGE_H

#include "types.h"

int storage_insert(const char *table_name, Row *row, Schema *schema);
ResultSet *storage_select(const char *table_name,
                          Schema *schema,
                          ColumnList *columns,
                          WhereClause *where);
void free_result_set(ResultSet *rs);
int evaluate_condition(Row *row, Schema *schema, Condition *cond);
int evaluate_where(Row *row, Schema *schema, WhereClause *where);

#endif
