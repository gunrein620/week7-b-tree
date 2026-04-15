#ifndef LEXER_H
#define LEXER_H

#include "types.h"

Token *tokenize(const char *sql, int *token_count);
void free_tokens(Token *tokens);

#endif
