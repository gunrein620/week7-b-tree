#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "types.h"

int execute(Statement *stmt);
int execute_insert(Statement *stmt);
int execute_select(Statement *stmt);

#endif
