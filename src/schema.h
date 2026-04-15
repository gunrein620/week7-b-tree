#ifndef SCHEMA_H
#define SCHEMA_H

#include "types.h"

Schema *schema_load(const char *table_name);
void schema_free(Schema *schema);
int schema_get_column_index(Schema *schema, const char *column_name);
ColumnType schema_parse_type(const char *type_str);

#endif
