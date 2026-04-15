#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "types.h"

int executor_set_output_enabled(int enabled);
int execute(Statement *stmt);
int execute_insert(Statement *stmt);
int execute_select(Statement *stmt);

#endif
