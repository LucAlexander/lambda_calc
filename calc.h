#ifndef LAMBDA_CALCULUS_INTERPRETER_H
#define LAMBDA_CALCULUS_INTERPRETER_H

#include <inttypes.h>
#include "hashmap.h"

#define POOL_SIZE 0x10000000
#define NAME_MAX 16
#define TOKEN_MAX NAME_MAX
#define APPLICATION_STACK_LIMIT 32
#define MAX_REDUCTION_DEPTH 8

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
expr* deep_copy_replace(interpreter* const inter, string_map* map, expr* const target, char* replace, expr* const replace_term);
expr* apply_term(interpreter* const inter, expr* const left, expr* const right);
uint8_t reduce_step(interpreter* const inter, expr* const expression, uint8_t max_depth);
void show_term(expr* const ex);
expr* generate_term_internal(interpreter* const inter, string* const assoc, uint64_t current_index, uint64_t current_depth, uint64_t max_depth);
expr* generate_term(interpreter* const inter, uint64_t depth);
expr* generate_entropic_term_internal(interpreter* const inter, string* const assoc, uint64_t current_index, uint64_t current_depth, uint64_t base_depth, uint64_t max_depth);
expr* generate_entropic_term(interpreter* const inter, uint64_t base_depth, uint64_t max_depth);
void rebase_worker(interpreter* const inter, string_map* const map, expr* const expression);
void rebase_term(interpreter* const inter, expr* const expression);
expr* rebase_worker_copy(interpreter* const inter, string_map* const map, expr* const expression);
expr* rebase_term_copy(interpreter* const inter, expr* const expression);
void generate_entropic_puzzle(interpreter* const inter, uint8_t f_comp, uint8_t base_comp, uint8_t arg_count, uint8_t arg_comp, uint8_t necessary_depth);
void generate_puzzle(interpreter* const inter, uint8_t f_comp, uint8_t base_comp, uint8_t arg_count, uint8_t arg_comp, uint8_t necessary_depth);
uint8_t term_depth(expr* const expression);
uint8_t term_bind_depth(expr* const expression);

expr* build_s(interpreter* const inter);
expr* build_k(interpreter* const inter);
expr* build_i(interpreter* const inter);
expr* build_b(interpreter* const inter);
expr* build_c(interpreter* const inter);
expr* build_w(interpreter* const inter);
expr* build_a(interpreter* const inter);
expr* build_t(interpreter* const inter);
expr* build_m(interpreter* const inter);
expr* build_nat(interpreter* const inter, uint8_t n);

#endif
