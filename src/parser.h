#ifndef PARSER_H
#define PARSER_H

#include "types.h"

Statement *parse(Token *tokens, int token_count);
void free_statement(Statement *stmt);

#endif
