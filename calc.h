#ifndef LAMBDA_CALCULUS_INTERPRETER_H
#define LAMBDA_CALCULUS_INTERPRETER_H

#include <inttypes.h>
#include "hashmap.h"

#define POOL_SIZE 0x1000000
#define NAME_MAX 16
#define TOKEN_MAX NAME_MAX
#define APPLICATION_STACK_LIMIT 32

typedef struct string {
	char* str;
	uint64_t len;
} string;

MAP_DEF(string)

typedef struct expr expr;
typedef struct expr {
	union {
		struct {
			string name;
			expr* expression;
		} bind;
		struct {
			expr* left;
			expr* right;
		} appl;
		string name;
	} data;
	enum {
		BIND_EXPR,
		APPL_EXPR,
		NAME_EXPR
	} tag;
} expr;

typedef struct interpreter {
	pool* const mem;
	string next;
} interpreter;

string next_string(interpreter* const inter);
expr* deep_copy(interpreter* const inter, string_map* map, expr* const target);
expr* apply_term(interpreter* const inter, expr* const left, expr* const right);
void show_term(expr* const ex);

#endif
