#ifndef LAMBDA_CALCULUS_INTERPRETER_H
#define LAMBDA_CALCULUS_INTERPRETER_H

#include <inttypes.h>
#include "kickstart.h"
#include "hashmap.h"

#define POOL_SIZE 0x10000000
#define NAME_MAX 16
#define TOKEN_MAX NAME_MAX
#define APPLICATION_STACK_LIMIT 32
#define MAX_REDUCTION_DEPTH 8

CSTR_MAP_DEF(string)

typedef struct expr expr;
typedef struct expr {
	string type_name;
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
	uint8_t typed;
} expr;

CSTR_MAP_DEF(expr)

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
void show_term_unambiguous(expr* const ex);
void show_term_helper(expr* const ex, uint8_t special);
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

CSTR_MAP_DEF(TOKEN)

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

#define PUZZLE_ARG_COUNT 3

typedef struct term_puzzle {
	expr* fun;
	expr* args[PUZZLE_ARG_COUNT];
	expr* results[PUZZLE_ARG_COUNT];
} term_puzzle;

void add_to_universe(interpreter* const inter, char* name, char* eval);
term_puzzle generate_combinator_strike_puzzle(interpreter* const inter);
uint8_t compare_terms_helper(expr* const a, expr* const b, string_map* const map);
uint8_t compare_terms(pool* const mem, expr* const a, expr* const b);
uint8_t term_contained(pool* const mem, expr* const a, expr* const b);
void term_flatten(interpreter* const inter, expr* const term);

typedef string type_string;

MAP_DECL(type_string)

typedef struct grammar grammar;
typedef struct grammar {
	union {
		struct {
			string name;
			string type;
			grammar* expression;
			uint8_t typed;
		} bind;
		struct {
			grammar* left;
			grammar* right;
		} appl;
		struct {
			string name;
			string type; // TODO unused?
		} name;
		struct {
			string name;
			string* params;
			uint64_t param_count;
		} type;
	} data;
	enum {
		BIND_GRAM,
		APPL_GRAM,
		NAME_GRAM,
		TYPE_GRAM
	} tag;
	string* params;
	uint64_t param_count;
	grammar* alts;
	uint64_t alt_count;
} grammar;

MAP_DECL(grammar)

typedef struct type_checker {
	grammar_map parameters;
	type_string_map param_names;
	type_string_map scope;
	type_string_map scope_types;
} type_checker;

uint8_t term_matches_type_worker(pool* const mem, grammar_map* const env, type_checker* const checker, expr* const term, grammar* const type);
uint8_t term_matches_type(pool* const mem, grammar_map* const env, expr* const term, grammar* const type, string* param_args, uint64_t param_arg_count);

#endif
