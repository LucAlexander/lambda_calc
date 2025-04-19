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

MAP_DEF(expr)

typedef struct interpreter {
	pool* const mem;
	string next;
	expr_map universe;
} interpreter;

string next_string(interpreter* const inter);
expr* deep_copy(interpreter* const inter, string_map* map, expr* const target);
expr* deep_copy_replace(interpreter* const inter, string_map* map, expr* const target, char* replace, expr* const replace_term);
expr* apply_term(interpreter* const inter, expr* const left, expr* const right);
uint8_t reduce_step(interpreter* const inter, expr* const expression, uint8_t max_depth);
void show_term_helper(expr* const ex, expr* const left);
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
uint8_t self_applies(expr* const e);
expr* generate_strike_term(interpreter* const inter);
void generate_strike_puzzle(interpreter* const inter);
expr* generate_strike_puzzle_helper(interpreter* const inter, uint8_t max_depth);

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
expr* build_and(interpreter* const inter);
expr* build_or(interpreter* const inter);
expr* build_not(interpreter* const inter);
expr* build_succ(interpreter* const inter);
expr* build_add(interpreter* const inter);
expr* build_mul(interpreter* const inter);
expr* build_exp(interpreter* const inter);
expr* build_T(interpreter* const inter);
expr* build_F(interpreter* const inter);
expr* build_cons(interpreter* const inter);

typedef enum TOKEN {
	LAMBDA_TOKEN='\\',
	BIND_TOKEN='.',
	OPEN_PAREN_TOKEN='(',
	CLOSE_PAREN_TOKEN=')',
	IDENTIFIER_TOKEN=100,
	NATURAL_TOKEN,
	S_TOKEN,
	K_TOKEN,
	I_TOKEN,
	B_TOKEN,
	C_TOKEN,
	W_TOKEN,
	A_TOKEN,
	T_TOKEN,
	M_TOKEN,
	AND_TOKEN,
	OR_TOKEN,
	NOT_TOKEN,
	SUCC_TOKEN,
	ADD_TOKEN,
	MUL_TOKEN,
	EXP_TOKEN,
	TRUE_TOKEN,
	FALSE_TOKEN,
	CONS_TOKEN,
} TOKEN;

MAP_DEF(TOKEN)

typedef struct token {
	union {
		string name;
		uint64_t nat;
	} data;
	TOKEN tag;
} token;

typedef struct parser {
	interpreter* const inter;
	string_map* names;
	TOKEN_map* combinators;
	pool* token_pool;
	token* tokens;
	uint64_t token_count;
	uint64_t token_index;
} parser;

uint8_t lex_natural(parser* const parse, char* cstr, uint64_t i, token* t);
uint8_t lex_identifier(parser* const parse, char* cstr, uint64_t i, token* t);
void lex_cstr(parser* const parse, char* cstr);
void show_tokens(parser* const parse);
expr* parse_lambda(parser* const parse, uint8_t paren);
expr* parse_body_term(parser* const parse, token* t, uint8_t paren);
expr* parse_term_recursive(parser* const parse, uint8_t paren);
void populate_combinators(TOKEN_map* map);
expr* parse_term(char* cstr, interpreter* const inter);

void add_to_universe(interpreter* const inter, char* name, char* eval);
void generate_combinator_strike_puzzle(interpreter* const inter);

#endif
