#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <ctype.h>

#include "calc.h"

CSTR_MAP_IMPL(string)
CSTR_MAP_IMPL(TOKEN)
CSTR_MAP_IMPL(expr)

MAP_IMPL(grammar_ptr)
MAP_IMPL(type_string)
MAP_IMPL(uint8_t)

MAP_IMPL(simple_type)
MAP_IMPL(parameter_string)

string
next_string(interpreter* const inter){
	string new = {
		.str = pool_request(inter->mem, inter->next.len+1),
		.len = inter->next.len
	};
	strncpy(new.str, inter->next.str, inter->next.len);
	uint8_t grow = 0;
	for (uint64_t i = 0;i<inter->next.len;++i){
		if (inter->next.str[i] < 'z'){
			inter->next.str[i] += 1;
			break;
		}
		inter->next.str[i] = 'a';
		if (i == inter->next.len-1){
			grow = 1;
		}
	}
	if (grow == 1){
		inter->next.str[inter->next.len] = 'a';
		inter->next.len += 1;
		pool_request(inter->mem, 1);
		inter->next.str[inter->next.len] = '\0';
	}
	if (expr_map_access(&inter->universe, new.str) != NULL){
		return next_string(inter);
	}
	return new;
}

expr*
deep_copy(interpreter* const inter, string_map* map, expr* const target){
	string_map new_map;
	if (map == NULL){
		new_map = string_map_init(inter->mem);
		map = &new_map;
	}
	expr* new = pool_request(inter->mem, sizeof(expr));
	new->tag = target->tag;
	switch (target->tag){
	case BIND_EXPR:
		new->data.bind.name = next_string(inter);
		string_map_insert(map, target->data.bind.name.str, &new->data.bind.name);
		new->data.bind.expression = deep_copy(inter, map, target->data.bind.expression);
		return new;
	case APPL_EXPR:
		new->data.appl.left = deep_copy(inter, map, target->data.appl.left);
		new->data.appl.right = deep_copy(inter, map, target->data.appl.right);
		return new;
	case NAME_EXPR:
		string* access = string_map_access(map, target->data.name.str);
		if (access == NULL){
			new->data.name = target->data.name;
			return new;
		}
		new->data.name = *access;
		return new;
	}
	return new;
}

expr*
deep_copy_replace(interpreter* const inter, string_map* map, expr* const target, char* replace, expr* const replace_term){
	expr* new = pool_request(inter->mem, sizeof(expr));
	new->tag = target->tag;
	switch (target->tag){
	case BIND_EXPR:
		new->data.bind.name = next_string(inter);
		string_map_insert(map, target->data.bind.name.str, &new->data.bind.name);
		new->data.bind.expression = deep_copy_replace(inter, map, target->data.bind.expression, replace, replace_term);
		return new;
	case APPL_EXPR:
		new->data.appl.left = deep_copy_replace(inter, map, target->data.appl.left, replace, replace_term);
		new->data.appl.right = deep_copy_replace(inter, map, target->data.appl.right, replace, replace_term);
		return new;
	case NAME_EXPR:
		if (strncmp(replace, target->data.name.str, target->data.name.len) == 0){
			expr* replaced = deep_copy(inter, NULL, replace_term);
			*new = *replaced;
			return new;
		}
		string* access = string_map_access(map, target->data.name.str);
		if (access == NULL){
			new->data.name = target->data.name;
			return new;
		}
		new->data.name = *access;
		return new;
	}
	return new;
}

expr*
apply_term(interpreter* const inter, expr* const left, expr* const right){
	if (left->tag == NAME_EXPR){
		expr* saved = expr_map_access(&inter->universe, left->data.name.str);
		if (saved != NULL){
			return apply_term(inter, saved, right);
		}
	}
	if (left->tag != BIND_EXPR){
		return NULL;
	}
	simple_type* applied_type;
	if (left->typed == 1){
		if (right->typed == 0){
			return NULL;
		}
		if (left->simple == NULL || right->simple == NULL){
			return NULL;
		}
		if (left->simple->tag != FUNCTION_TYPE){
			return NULL;
		}
		parameter_diff node = {
			.next = NULL
		};
		if (simple_type_compare_parametric_differences(inter->mem, left->simple->data.function.left, right->simple, &node, NULL) == 0){
			return NULL;
		}
		simple_type_map replacement_map = simple_type_map_init(inter->mem);
		if (node.next != NULL){
			parameter_diff* iter = node.next;
			while (iter != NULL){
				simple_type_map_insert(&replacement_map, iter->parameter, *iter->diff);
				iter = iter->next;
			}
		}
		applied_type = pool_request(inter->mem, sizeof(simple_type));
		deep_copy_simple_type_replace_multiple(inter->mem, left->simple->data.function.right, applied_type, &replacement_map);
	}
	char* target = left->data.bind.name.str;
	string_map new_map = string_map_init(inter->mem);
	expr* new = deep_copy_replace(inter, &new_map, left->data.bind.expression, target, right);
	if (left->typed == 1){
		new->typed = 1;
		new->simple = applied_type;
	}
	return new;
}

uint8_t
reduce_step(interpreter* const inter, expr* const expression, uint8_t max_depth){
	if (max_depth == 0){
		return 0;
	}
	switch (expression->tag){
	case BIND_EXPR:
		return reduce_step(inter, expression->data.bind.expression, max_depth-1);
	case APPL_EXPR:
		if (reduce_step(inter, expression->data.appl.left, max_depth-1) == 0){
			if (reduce_step(inter, expression->data.appl.right, max_depth-1) == 0){
				if (expression->data.appl.left->tag == NAME_EXPR){
					expr* term = expr_map_access(&inter->universe, expression->data.appl.left->data.name.str);
					if (term == NULL){
						return 0;
					}
					expr* standin = apply_term(inter, term, expression->data.appl.right);
					*expression = *standin;
					return 1;
				}
				expr* new = apply_term(inter, expression->data.appl.left, expression->data.appl.right);
				if (new == NULL){
					return 0;
				}
				*expression = *new;
			}
		}
		return 1;
	case NAME_EXPR:
		return 0;
	}
	return 0;
}

void
show_term_unambiguous(expr* const ex){
	switch (ex->tag){
	case BIND_EXPR:
		printf("(%s:", ex->data.bind.name.str);
		show_term_unambiguous(ex->data.bind.expression);
		printf(")");
		return;
case APPL_EXPR:
		printf("(");
		show_term_unambiguous(ex->data.appl.left);
		printf(" ");
		show_term_unambiguous(ex->data.appl.right);
		printf(")");
		return;
	case NAME_EXPR:
		printf("%s", ex->data.name.str);
		return;
	}
}

void
show_term_helper(expr* const ex, uint8_t special){
	switch (ex->tag){
	case BIND_EXPR:
		printf("\\%s.", ex->data.bind.name.str);
		show_term(ex->data.bind.expression);
		return;
case APPL_EXPR:
		if (special == 0){
			printf("(");
		}
		if (ex->data.appl.left->tag == APPL_EXPR){
			show_term_helper(ex->data.appl.left, 1);
		}
else{
			show_term(ex->data.appl.left);
		}
		printf(" ");
		show_term(ex->data.appl.right);
		if (special == 0){
			printf(")");
		}
		return;
	case NAME_EXPR:
		printf("%s", ex->data.name.str);
		return;
	}
}

void
show_term(expr* const ex){
	show_term_helper(ex, 0);
}

expr*
generate_term_internal(interpreter* const inter, string* const assoc, uint64_t current_index, uint64_t current_depth, uint64_t max_depth){
	if (current_depth == max_depth){
		uint64_t index = rand() % current_index;
		expr* name = pool_request(inter->mem, sizeof(expr));
		name->tag = NAME_EXPR;
		name->data.name = assoc[index];
		return name;
	}
	expr* new = pool_request(inter->mem, sizeof(expr));
	uint8_t max = 3;
	if (current_index == 0){
max = 1;
	}
	switch (rand() % max){
	case 0:
		new->tag = BIND_EXPR;
		new->data.bind.name = next_string(inter);
		assoc[current_index] = new->data.bind.name;
		new->data.bind.expression = generate_term_internal(inter, assoc, current_index+1, current_depth+1, max_depth);
		return new;
case 1:
		new->tag = APPL_EXPR;
		new->data.appl.left = generate_term_internal(inter, assoc, current_index, current_depth+1, max_depth);
new->data.appl.right = generate_term_internal(inter, assoc, current_index, current_depth+1, max_depth);
		return new;
	case 2:
		uint64_t index = rand() % current_index;
		new->tag = NAME_EXPR;
		new->data.name = assoc[index];
		return new;
	}
	return new;
}

expr*
generate_term(interpreter* const inter, uint64_t depth){
	if (depth == 0){
		return NULL;
	}
	string* assoc = pool_request(inter->mem, sizeof(string)*depth);
	return generate_term_internal(inter, assoc, 0, 0, depth);
}

expr*
generate_entropic_term_internal(interpreter* const inter, string* const assoc, uint64_t current_index, uint64_t current_depth, uint64_t base_depth, uint64_t max_depth){
	if (current_depth == max_depth){
		uint64_t index = rand() % current_index;
		expr* name = pool_request(inter->mem, sizeof(expr));
		name->tag = NAME_EXPR;
name->data.name = assoc[index];
		return name;
	}
	expr* new = pool_request(inter->mem, sizeof(expr));
	if (current_depth < base_depth){
		new->tag = BIND_EXPR;
		new->data.bind.name = next_string(inter);
		assoc[current_index] = new->data.bind.name;
		new->data.bind.expression = generate_entropic_term_internal(inter, assoc, current_index+1, current_depth+1, base_depth, max_depth);
		return new;
	}
	uint64_t choice = rand() % 3;
	if (choice != 1){
		choice = rand() % 3;
	}
	switch (choice){
	case 0:
		new->tag = BIND_EXPR;
		new->data.bind.name = next_string(inter);
		assoc[current_index] = new->data.bind.name;
		new->data.bind.expression = generate_entropic_term_internal(inter, assoc, current_index+1, current_depth+1, base_depth, max_depth);
		return new;
	case 1:
		new->tag = APPL_EXPR;
		new->data.appl.left = generate_entropic_term_internal(inter, assoc, current_index, current_depth+1, base_depth, max_depth);
		new->data.appl.right = generate_entropic_term_internal(inter, assoc, current_index, current_depth+1, base_depth, max_depth);
		return new;
	case 2:
		uint64_t index = rand() % current_index;
		new->tag = NAME_EXPR;
		new->data.name = assoc[index];
		return new;
	}
	return new;
}

expr*
generate_entropic_term(interpreter* const inter, uint64_t base_depth, uint64_t max_depth){
	if (base_depth == 0 || max_depth == 0){
		return NULL;
	}
	string* assoc = pool_request(inter->mem, sizeof(string)*max_depth);
	return generate_entropic_term_internal(inter, assoc, 0, 0, base_depth, max_depth);
}

void
rebase_worker(interpreter* const inter, string_map* const map, expr* const expression){
	switch (expression->tag){
	case BIND_EXPR:
		string new = next_string(inter);
		string_map_insert(map, expression->data.bind.name.str, &new);
		expression->data.bind.name = new;
		rebase_worker(inter, map, expression->data.bind.expression);
		return;
	case APPL_EXPR:
		rebase_worker(inter, map, expression->data.appl.left);
		rebase_worker(inter, map, expression->data.appl.right);
		return;
	case NAME_EXPR:
		string* access = string_map_access(map, expression->data.name.str);
		if (access != NULL){
			expression->data.name = *access;
		}
		return;
	}
}

void
rebase_term(interpreter* const inter, expr* const expression){
	if (inter->next.str == NULL){
		inter->next.str = pool_request(inter->mem, NAME_MAX);
	}
	inter->next.str[0] = 'a';
	inter->next.str[1] = '\0';
	inter->next.len = 1;
	string_map map = string_map_init(inter->mem);
	rebase_worker(inter, &map, expression);
}

expr*
rebase_copy_worker(interpreter* const inter, string_map* const map, expr* const expression){
	expr* new = pool_request(inter->mem, sizeof(expr));
	new->tag = expression->tag;
	switch (expression->tag){
	case BIND_EXPR:
		string new_str = next_string(inter);
		new->data.bind.name = new_str;
		string_map_insert(map, expression->data.bind.name.str, &new_str);
		new->data.bind.expression = rebase_copy_worker(inter, map, expression->data.bind.expression);
		return new;
	case APPL_EXPR:
		new->data.appl.left = rebase_copy_worker(inter, map, expression->data.appl.left);
		new->data.appl.right = rebase_copy_worker(inter, map, expression->data.appl.right);
		return new;
	case NAME_EXPR:
		string* access = string_map_access(map, expression->data.name.str);
		if (access != NULL){
			new->data.name = *access;
			return new;
		}
		new->data.name.str = pool_request(inter->mem, expression->data.name.len+1);
		strncpy(new->data.name.str, expression->data.name.str, expression->data.name.len);
		return new;
	}
	return NULL;
}

void
reset_universe(interpreter* const inter){
	inter->next.str[0] = 'a';
	inter->next.str[1] = '\0';
	inter->next.len = 1;
	expr_map_empty(&inter->universe);
}

expr*
rebase_term_copy(interpreter* const inter, expr* const expression){
	if (inter->next.str == NULL){
		inter->next.str = pool_request(inter->mem, NAME_MAX);
	}
	reset_universe(inter);
	string_map map = string_map_init(inter->mem);
	return rebase_copy_worker(inter, &map, expression);
}

interpreter
interpreter_init(pool* const mem){
	interpreter inter = {
		.mem=mem,
		.next.str = pool_request(mem, NAME_MAX),
		.universe = expr_map_init(mem)
	};
	reset_universe(&inter);
	return inter;
}

void
generate_puzzle(interpreter* const inter, uint8_t f_comp, uint8_t base_comp, uint8_t arg_count, uint8_t arg_comp, uint8_t necessary_depth){
	reset_universe(inter);
	expr* f;
	while (1){
		f = generate_term(inter, f_comp);
		if (term_bind_depth(f) < arg_count){
			continue;
		}
		if (term_depth(f) < base_comp){
			continue;
		}
		break;
	}
	rebase_term(inter, f);
	show_term(f);
	printf("\n");
	expr* z = f;
	while (arg_count > 0){
		expr* arg = generate_term(inter, arg_comp);
		while (term_depth(arg) < necessary_depth){
			arg = generate_term(inter, arg_comp);
		}
		show_term(arg);
		printf("\n");
		z = apply_term(inter, z, arg);
		arg_count -= 1;
	}
	uint8_t reductions = 8;
	while (reduce_step(inter, z, MAX_REDUCTION_DEPTH) != 0 && (reductions-- > 0)){ }
	rebase_term(inter, z);
	show_term(z);
	printf("\n");
}

void
generate_entropic_puzzle(interpreter* const inter, uint8_t f_comp, uint8_t base_comp, uint8_t arg_count, uint8_t arg_comp, uint8_t necessary_depth){
	reset_universe(inter);
	expr* f;
	while (1){
		f = generate_entropic_term(inter, 1, f_comp);
		if (term_bind_depth(f) < arg_count){
			continue;
		}
		if (term_depth(f) < base_comp){
			continue;
		}
		break;
	}
	rebase_term(inter, f);
	show_term(f);
	printf("\n");
	expr* z = f;
	while (arg_count > 0){
		expr* arg = generate_entropic_term(inter, 1, arg_comp);
		while (term_depth(arg) < necessary_depth){
			arg = generate_entropic_term(inter, 1, arg_comp);
		}
		show_term(arg);
		printf("\n");
		z = apply_term(inter, z, arg);
		arg_count -= 1;
	}
	uint8_t reductions = 8;
	while (reduce_step(inter, z, MAX_REDUCTION_DEPTH) != 0 && (reductions-- > 0)){ }
	rebase_term(inter, z);
	show_term(z);
	printf("\n");
}

void
generate_fuzz_puzzle(interpreter* const inter, uint8_t f_comp, uint8_t base_comp, uint8_t arg_count, uint8_t arg_comp, uint8_t necessary_depth, uint8_t base_depth){
	reset_universe(inter);
	expr* f;
	while (1){
		f = generate_entropic_term(inter, base_depth, f_comp);
		if (term_bind_depth(f) < arg_count){
			continue;
		}
		if (term_depth(f) < base_comp){
			continue;
		}
		break;
	}
	rebase_term(inter, f);
	printf("Function (hide): ");
	show_term(f);
	printf("\n");
	uint8_t arg_reset = arg_count;
	for (uint8_t i = 0;i<4;++i){
		printf("Set %u\n", i);
		expr* z = deep_copy(inter, NULL, f);
		while (arg_count > 0){
			expr* arg = generate_entropic_term(inter, base_depth, arg_comp);
			while (term_depth(arg) < necessary_depth){
				arg = generate_entropic_term(inter, base_depth, arg_comp);
			}
			printf("arg : ");
			show_term(arg);
			printf("\n");
			z = apply_term(inter, z, arg);
			arg_count -= 1;
		}
		arg_count = arg_reset;
		uint8_t reductions = 8;
		while (reduce_step(inter, z, MAX_REDUCTION_DEPTH) != 0 && (reductions-- > 0)){ }
		rebase_term(inter, z);
		printf("Partial reduction (answer): ");
		show_term(z);
		printf("\n");
	}
}

expr*
generate_strike_puzzle_helper(interpreter* const inter, uint8_t max_depth){
	uint8_t choice = rand() % 30;
	if (max_depth == 0){
		choice = rand() % 19;
	}
	expr* (*f[])(interpreter* const) = {
		build_s, build_k, build_i,
		build_b, build_c, build_w,
		build_a, build_t, build_m,
		build_and, build_or, build_not,
		build_succ, build_add, build_mul,
		build_exp, build_T, build_F,
		build_cons
	};
	if (choice < 19){
		return f[choice](inter);
	}
	if (choice < 22){
		choice -= 19;
		return build_nat(inter, choice);
	}
	expr* apply = pool_request(inter->mem, sizeof(expr));
	apply->tag = APPL_EXPR;
	apply->data.appl.left = generate_strike_puzzle_helper(inter, max_depth-1);
	apply->data.appl.right = generate_strike_puzzle_helper(inter, max_depth-1);
	return apply;
}

void
generate_strike_puzzle(interpreter* const inter){
	reset_universe(inter);
	printf("f: ");
	expr* f = generate_strike_term(inter);
	show_term(f);
	printf("\n");
	uint8_t args = term_bind_depth(f);
	while (args-- > 0){
		expr* arg = generate_strike_term(inter);
		printf("arg: ");
		show_term(arg);
		printf("\n");
		f = apply_term(inter, f, arg);
	}
	uint8_t reductions = 8;
	while (reduce_step(inter, f, MAX_REDUCTION_DEPTH) != 0 && (reductions-- > 0)){}
	rebase_term(inter, f);
	printf("Final: ");
	show_term(f);
	printf("\n");
}

expr*
generate_strike_term(interpreter* const inter){
	expr* initial = pool_request(inter->mem, sizeof(expr));
	while (1){
		initial->tag = APPL_EXPR;
		initial->data.appl.left = generate_strike_puzzle_helper(inter, 1);
		initial->data.appl.right = generate_strike_puzzle_helper(inter, 1);
		uint8_t reductions = 8;
		uint8_t appl_break = 0;
		while (reduce_step(inter, initial, MAX_REDUCTION_DEPTH) != 0 && (reductions-- > 0)){
			if (self_applies(initial) == 1){
				appl_break = 1;
				break;
			}
		}
		if (appl_break == 1){
			continue;
		}
		if (term_depth(initial) - term_bind_depth(initial) < 2){
			continue;
		}
		if (initial->tag != BIND_EXPR){
			continue;
		}
		break;
	}
	return initial;
}

uint8_t
self_applies(expr* const e){
	switch (e->tag){
	case BIND_EXPR:
		return self_applies(e->data.bind.expression);
	case APPL_EXPR:
		if (e->data.appl.left->tag == NAME_EXPR
		 && e->data.appl.right->tag == NAME_EXPR){
			if (strncmp(e->data.appl.left->data.name.str, e->data.appl.right->data.name.str, e->data.appl.left->data.name.len) == 0){
				return 1;
			}
		}
		return 0;
	case NAME_EXPR:
		return 0;
	}
	return 0;
}

uint8_t
term_depth(expr* const expression){
	switch (expression->tag){
	case BIND_EXPR:
		return 1 + term_depth(expression->data.bind.expression);
	case APPL_EXPR:
		uint8_t a = term_depth(expression->data.appl.left);
		uint8_t b = term_depth(expression->data.appl.right);
		if (a>b){
			return a+1;
		}
		return b+1;
	case NAME_EXPR:
		return 0;
	}
	return 0;
}

uint8_t
term_bind_depth(expr* const expression){
	switch(expression->tag){
	case BIND_EXPR:
		return 1+term_bind_depth(expression->data.bind.expression);
	default:
		return 0;
	}
	return 0;
}

expr* build_s(interpreter* const inter){
	expr* x = pool_request(inter->mem, sizeof(expr));
	x->tag = BIND_EXPR;
	expr* y = pool_request(inter->mem, sizeof(expr));
	y->tag = BIND_EXPR;
	expr* z = pool_request(inter->mem, sizeof(expr));
	z->tag = BIND_EXPR;
	x->data.bind.name = next_string(inter);
	x->data.bind.expression = y;
	y->data.bind.name = next_string(inter);
	y->data.bind.expression = z;
	z->data.bind.name = next_string(inter);
	expr* xzyz = pool_request(inter->mem, sizeof(expr));
	xzyz->tag = APPL_EXPR;
	expr* xz = pool_request(inter->mem, sizeof(expr));
	xz->tag = APPL_EXPR;
	expr* yz = pool_request(inter->mem, sizeof(expr));
	yz->tag = APPL_EXPR;
	z->data.bind.expression = xzyz;
	xzyz->data.appl.left = xz;
	xzyz->data.appl.right = yz;
	expr* inner_x = pool_request(inter->mem, sizeof(expr));
	inner_x->tag = NAME_EXPR;
	inner_x->data.name = x->data.bind.name;
	expr* inner_y = pool_request(inter->mem, sizeof(expr));
	inner_y->tag = NAME_EXPR;
	inner_y->data.name = y->data.bind.name;
	expr* inner_zl = pool_request(inter->mem, sizeof(expr));
	inner_zl->tag = NAME_EXPR;
	inner_zl->data.name = z->data.bind.name;
	expr* inner_zr = deep_copy(inter, NULL, inner_zl);
	xz->data.appl.left = inner_x;
	xz->data.appl.right = inner_zl;
	yz->data.appl.left = inner_y;
	yz->data.appl.right = inner_zr;
	return x;
}

expr* build_k(interpreter* const inter){
	expr* x = pool_request(inter->mem, sizeof(expr));
	x->tag = BIND_EXPR;
	expr* y = pool_request(inter->mem, sizeof(expr));
	y->tag = BIND_EXPR;
	x->data.bind.name = next_string(inter);
	x->data.bind.expression = y;
	y->data.bind.name = next_string(inter);
	expr* x_inner = pool_request(inter->mem, sizeof(expr));
	x_inner->tag = NAME_EXPR;
	x_inner->data.name = x->data.bind.name;
	y->data.bind.expression = x_inner;
	return x;
}

expr* build_i(interpreter* const inter){
	expr* x = pool_request(inter->mem, sizeof(expr));
	x->tag = BIND_EXPR;
	x->data.bind.name = next_string(inter);
	expr* x_inner = pool_request(inter->mem, sizeof(expr));
	x_inner->tag = NAME_EXPR;
	x_inner->data.name = x->data.bind.name;
	x->data.bind.expression = x_inner;
	return x;
}

expr* build_b(interpreter* const inter){
	expr* x = pool_request(inter->mem, sizeof(expr));
	x->tag = BIND_EXPR;
	x->data.bind.name = next_string(inter);
	expr* y = pool_request(inter->mem, sizeof(expr));
	y->tag = BIND_EXPR;
	y->data.bind.name = next_string(inter);
	expr* z = pool_request(inter->mem, sizeof(expr));
	z->tag = BIND_EXPR;
	z->data.bind.name = next_string(inter);
	x->data.bind.expression = y;
	y->data.bind.expression = z;
	expr* xyz = pool_request(inter->mem, sizeof(expr));
	xyz->tag = APPL_EXPR;
	expr* x_inner = pool_request(inter->mem, sizeof(expr));
	x_inner->tag = NAME_EXPR;
	x_inner->data.name = x->data.bind.name;
	expr* y_inner = pool_request(inter->mem, sizeof(expr));
	y_inner->tag = NAME_EXPR;
	y_inner->data.name = y->data.bind.name;
	expr* z_inner = pool_request(inter->mem, sizeof(expr));
	z_inner->tag = NAME_EXPR;
	z_inner->data.name = z->data.bind.name;
	expr* yz = pool_request(inter->mem, sizeof(expr));
	yz->tag = APPL_EXPR;
	yz->data.appl.left = y_inner;
	yz->data.appl.right = z_inner;
	xyz->data.appl.left = x_inner;
	xyz->data.appl.right = yz;
	z->data.bind.expression = xyz;
	return x;
}

expr* build_c(interpreter* const inter){
	expr* x = pool_request(inter->mem, sizeof(expr));
	x->tag = BIND_EXPR;
	x->data.bind.name = next_string(inter);
	expr* y = pool_request(inter->mem, sizeof(expr));
	y->tag = BIND_EXPR;
	y->data.bind.name = next_string(inter);
	expr* z = pool_request(inter->mem, sizeof(expr));
	z->tag = BIND_EXPR;
	z->data.bind.name = next_string(inter);
	x->data.bind.expression = y;
	y->data.bind.expression = z;
	expr* xzy = pool_request(inter->mem, sizeof(expr));
	z->data.bind.expression = xzy;
	xzy->tag = APPL_EXPR;
	expr* x_inner = pool_request(inter->mem, sizeof(expr));
	x_inner->tag = NAME_EXPR;
	x_inner->data.name = x->data.bind.name;
	expr* y_inner = pool_request(inter->mem, sizeof(expr));
	y_inner->tag = NAME_EXPR;
	y_inner->data.name = y->data.bind.name;
	expr* z_inner = pool_request(inter->mem, sizeof(expr));
	z_inner->tag = NAME_EXPR;
	z_inner->data.name = z->data.bind.name;
	expr* xz = pool_request(inter->mem, sizeof(expr));
	xz->tag = APPL_EXPR;
	xz->data.appl.left = x_inner;
	xz->data.appl.right = z_inner;
	xzy->data.appl.left = xz;
	xzy->data.appl.right = y_inner;
	return x;
}

expr* build_w(interpreter* const inter){
	expr* x = pool_request(inter->mem, sizeof(expr));
	x->tag = BIND_EXPR;
	x->data.bind.name = next_string(inter);
	expr* y = pool_request(inter->mem, sizeof(expr));
	y->tag = BIND_EXPR;
	y->data.bind.name = next_string(inter);
	x->data.bind.expression = y;
	expr* x_inner = pool_request(inter->mem, sizeof(expr));
	x_inner->tag = NAME_EXPR;
	x_inner->data.name = x->data.bind.name;
	expr* y_inner = pool_request(inter->mem, sizeof(expr));
	y_inner->tag = NAME_EXPR;
	y_inner->data.name = y->data.bind.name;
	expr* yr_inner = pool_request(inter->mem, sizeof(expr));
	yr_inner->tag = NAME_EXPR;
	yr_inner->data.name = y->data.bind.name;
	expr* xy = pool_request(inter->mem, sizeof(expr));
	xy->tag = APPL_EXPR;
	xy->data.appl.left = x_inner;
	xy->data.appl.right = y_inner;
	expr* xyy = pool_request(inter->mem, sizeof(expr));
	xyy->tag = APPL_EXPR;
	xyy->data.appl.left = xy;
	xyy->data.appl.right= yr_inner;
	y->data.bind.expression = xyy;
	return x;
}

expr* build_a(interpreter* const inter){
	expr* x = pool_request(inter->mem, sizeof(expr));
	x->tag = BIND_EXPR;
	expr* y = pool_request(inter->mem, sizeof(expr));
	y->tag = BIND_EXPR;
	x->data.bind.name = next_string(inter);
	x->data.bind.expression = y;
	y->data.bind.name = next_string(inter);
	expr* y_inner = pool_request(inter->mem, sizeof(expr));
	y_inner->tag = NAME_EXPR;
	y_inner->data.name = y->data.bind.name;
	y->data.bind.expression = y_inner;
	return x;
}

expr* build_t(interpreter* const inter){
	expr* x = pool_request(inter->mem, sizeof(expr));
	x->tag = BIND_EXPR;
	x->data.bind.name = next_string(inter);
	expr* y = pool_request(inter->mem, sizeof(expr));
	y->tag = BIND_EXPR;
	y->data.bind.name = next_string(inter);
	x->data.bind.expression = y;
	expr* x_inner = pool_request(inter->mem, sizeof(expr));
	x_inner->tag = NAME_EXPR;
	x_inner->data.name = x->data.bind.name;
	expr* y_inner = pool_request(inter->mem, sizeof(expr));
	y_inner->tag = NAME_EXPR;
	y_inner->data.name = y->data.bind.name;
	expr* yx = pool_request(inter->mem, sizeof(expr));
	yx->tag = APPL_EXPR;
	yx->data.appl.left = y_inner;
	yx->data.appl.right = x_inner;
	y->data.bind.expression = yx;
	return x;
}

expr* build_m(interpreter* const inter){
	expr* x = pool_request(inter->mem, sizeof(expr));
	x->tag = BIND_EXPR;
	x->data.bind.name = next_string(inter);
	expr* x_inner = pool_request(inter->mem, sizeof(expr));
	x_inner->tag = NAME_EXPR;
	x_inner->data.name = x->data.bind.name;
	expr* xr_inner = pool_request(inter->mem, sizeof(expr));
	xr_inner->tag = NAME_EXPR;
	xr_inner->data.name = x->data.bind.name;
	expr* xx = pool_request(inter->mem, sizeof(expr));
	xx->tag = APPL_EXPR;
	xx->data.appl.left = x_inner;
	xx->data.appl.right = xr_inner;
	x->data.bind.expression = xx;
	return x;
}

expr* build_nat(interpreter* const inter, uint8_t n){
	expr* s = pool_request(inter->mem, sizeof(expr));
	s->tag = BIND_EXPR;
	s->data.bind.name = next_string(inter);
	expr* z = pool_request(inter->mem, sizeof(expr));
	z->tag = BIND_EXPR;
	z->data.bind.name = next_string(inter);
	s->data.bind.expression = z;
	z->data.bind.expression = pool_request(inter->mem, sizeof(expr));
	expr* last = z->data.bind.expression;
	for (uint8_t i = 0;i<n;++i){
		last->tag = APPL_EXPR;
		expr* left = pool_request(inter->mem, sizeof(expr));
		left->tag = NAME_EXPR;
		left->data.name = s->data.bind.name;
		last->data.appl.left = left;
		last->data.appl.right = pool_request(inter->mem, sizeof(expr));
		last = last->data.appl.right;
	}
	last->tag = NAME_EXPR;
	last->data.name = z->data.bind.name;
	return s;
}

expr* build_and(interpreter* const inter){
	expr* x = pool_request(inter->mem, sizeof(expr));
	x->tag = BIND_EXPR;
	x->data.bind.name = next_string(inter);
	expr* y = pool_request(inter->mem, sizeof(expr));
	y->tag = BIND_EXPR;
	y->data.bind.name = next_string(inter);
	x->data.bind.expression = y;
	expr* xy = pool_request(inter->mem, sizeof(expr));
	xy->tag = APPL_EXPR;
	expr* y_inner = pool_request(inter->mem, sizeof(expr));
	y_inner->tag = NAME_EXPR;
	y_inner->data.name = y->data.bind.name;
	expr* x_inner = pool_request(inter->mem, sizeof(expr));
	x_inner->tag = NAME_EXPR;
	x_inner->data.name = x->data.bind.name;
	xy->data.appl.left = x_inner;
	xy->data.appl.right = y_inner;
	expr* a = build_a(inter);
	expr* xya = pool_request(inter->mem, sizeof(expr));
	xya->tag = APPL_EXPR;
	xya->data.appl.left = xy;
	xya->data.appl.right = a;
	y->data.bind.expression = xya;
	return x;
}

expr* build_or(interpreter* const inter){
	expr* x = pool_request(inter->mem, sizeof(expr));
	x->tag = BIND_EXPR;
	x->data.bind.name = next_string(inter);
	expr* y = pool_request(inter->mem, sizeof(expr));
	y->tag = BIND_EXPR;
	y->data.bind.name = next_string(inter);
	x->data.bind.expression = y;
	expr* xk = pool_request(inter->mem, sizeof(expr));
	xk->tag = APPL_EXPR;
	expr* k = build_k(inter);
	expr* y_inner = pool_request(inter->mem, sizeof(expr));
	y_inner->tag = NAME_EXPR;
	y_inner->data.name = y->data.bind.name;
	expr* x_inner = pool_request(inter->mem, sizeof(expr));
	x_inner->tag = NAME_EXPR;
	x_inner->data.name = x->data.bind.name;
	xk->data.appl.left = x_inner;
	xk->data.appl.right = k;
	expr* xky = pool_request(inter->mem, sizeof(expr));
	xky->tag = APPL_EXPR;
	xky->data.appl.left = xk;
	xky->data.appl.right = y_inner;
	y->data.bind.expression = xky;
	return x;
}

expr* build_not(interpreter* const inter){
	expr* x = pool_request(inter->mem, sizeof(expr));
	x->tag = BIND_EXPR;
	x->data.bind.name = next_string(inter);
	expr* xa = pool_request(inter->mem, sizeof(expr));
	xa->tag = APPL_EXPR;
	expr* a = build_a(inter);
	expr* x_inner = pool_request(inter->mem, sizeof(expr));
	x_inner->tag = NAME_EXPR;
	x_inner->data.name = x->data.bind.name;
	xa->data.appl.left = x_inner;
	xa->data.appl.right = a;
	expr* k = build_k(inter);
	expr* xak = pool_request(inter->mem, sizeof(expr));
	xak->tag = APPL_EXPR;
	xak->data.appl.left = xa;
	xak->data.appl.right = k;
	x->data.bind.expression = xak;
	return x;
}

expr* build_succ(interpreter* const inter){
	expr* n = pool_request(inter->mem, sizeof(expr));
	expr* s = pool_request(inter->mem, sizeof(expr));
	expr* z = pool_request(inter->mem, sizeof(expr));
	n->tag = BIND_EXPR;
	s->tag = BIND_EXPR;
	z->tag = BIND_EXPR;
	n->data.bind.name = next_string(inter);
	s->data.bind.name = next_string(inter);
	z->data.bind.name = next_string(inter);
	n->data.bind.expression = s;
	s->data.bind.expression = z;
	expr* s_outer = pool_request(inter->mem, sizeof(expr));
	s_outer->tag = NAME_EXPR;
	s_outer->data.name = s->data.bind.name;
	expr* n_inner = pool_request(inter->mem, sizeof(expr));
	expr* s_inner = pool_request(inter->mem, sizeof(expr));
	expr* z_inner = pool_request(inter->mem, sizeof(expr));
	n_inner->tag = NAME_EXPR;
	s_inner->tag = NAME_EXPR;
	z_inner->tag = NAME_EXPR;
	n_inner->data.name = n->data.bind.name;
	s_inner->data.name = s->data.bind.name;
	z_inner->data.name = z->data.bind.name;
	expr* ns = pool_request(inter->mem, sizeof(expr));
	ns->tag = APPL_EXPR;
	ns->data.appl.left = n_inner;
	ns->data.appl.right = s_inner;
	expr* nsz = pool_request(inter->mem, sizeof(expr));
	nsz->tag = APPL_EXPR;
	nsz->data.appl.left = ns;
	nsz->data.appl.right = z_inner;
	expr* snsz = pool_request(inter->mem, sizeof(expr));
	snsz->tag = APPL_EXPR;
	snsz->data.appl.left = s_outer;
	snsz->data.appl.right = nsz;
	z->data.bind.expression = snsz;
	return n;
}

expr* build_add(interpreter* const inter){
	expr* x = pool_request(inter->mem, sizeof(expr));
	x->tag = BIND_EXPR;
	x->data.bind.name = next_string(inter);
	expr* y = pool_request(inter->mem, sizeof(expr));
	y->tag = BIND_EXPR;
	y->data.bind.name = next_string(inter);
	x->data.bind.expression = y;
	expr* xs = pool_request(inter->mem, sizeof(expr));
	xs->tag = APPL_EXPR;
	expr* s = build_succ(inter);
	expr* y_inner = pool_request(inter->mem, sizeof(expr));
	y_inner->tag = NAME_EXPR;
	y_inner->data.name = y->data.bind.name;
	expr* x_inner = pool_request(inter->mem, sizeof(expr));
	x_inner->tag = NAME_EXPR;
	x_inner->data.name = x->data.bind.name;
	xs->data.appl.left = x_inner;
	xs->data.appl.right = s;
	expr* xsy = pool_request(inter->mem, sizeof(expr));
	xsy->tag = APPL_EXPR;
	xsy->data.appl.left = xs;
	xsy->data.appl.right = y_inner;
	y->data.bind.expression = xsy;
	return x;
}

expr* build_mul(interpreter* const inter){
	expr* x = pool_request(inter->mem, sizeof(expr));
	x->tag = BIND_EXPR;
	x->data.bind.name = next_string(inter);
	expr* y = pool_request(inter->mem, sizeof(expr));
	y->tag = BIND_EXPR;
	y->data.bind.name = next_string(inter);
	x->data.bind.expression = y;
	expr* add = build_add(inter);
	expr* zero = build_nat(inter, 0);
	expr* x_inner = pool_request(inter->mem, sizeof(expr));
	x_inner->tag = NAME_EXPR;
	x_inner->data.name = x->data.bind.name;
	expr* y_inner = pool_request(inter->mem, sizeof(expr));
	y_inner->tag = NAME_EXPR;
	y_inner->data.name = y->data.bind.name;
	expr* addy = pool_request(inter->mem, sizeof(expr));
	addy->tag = APPL_EXPR;
	addy->data.appl.left = add;
	addy->data.appl.right = y_inner;
	expr* xaddy = pool_request(inter->mem, sizeof(expr));
	xaddy->tag = APPL_EXPR;
	xaddy->data.appl.left = x_inner;
	xaddy->data.appl.right = addy;
	expr* xaddy0 = pool_request(inter->mem, sizeof(expr));
	xaddy0->tag = APPL_EXPR;
	xaddy0->data.appl.left = xaddy;
	xaddy0->data.appl.right = zero;
	y->data.bind.expression = xaddy0;
	return x;
}

expr* build_exp(interpreter* const inter){
	return build_t(inter);
}

expr* build_T(interpreter* const inter){
	return build_k(inter);
}

expr* build_F(interpreter* const inter){
	return build_a(inter);
}

expr* build_cons(interpreter* const inter){
	expr* x = pool_request(inter->mem, sizeof(expr));
	expr* y = pool_request(inter->mem, sizeof(expr));
	expr* z = pool_request(inter->mem, sizeof(expr));
	x->tag = BIND_EXPR;
	y->tag = BIND_EXPR;
	z->tag = BIND_EXPR;
	x->data.bind.name = next_string(inter);
	y->data.bind.name = next_string(inter);
	z->data.bind.name = next_string(inter);
	x->data.bind.expression = y;
	y->data.bind.expression = z;
	expr* x_inner = pool_request(inter->mem, sizeof(expr));
	expr* y_inner = pool_request(inter->mem, sizeof(expr));
	expr* z_inner = pool_request(inter->mem, sizeof(expr));
	x_inner->tag = NAME_EXPR;
	y_inner->tag = NAME_EXPR;
	z_inner->tag = NAME_EXPR;
	x_inner->data.name = x->data.bind.name;
	y_inner->data.name = y->data.bind.name;
	z_inner->data.name = z->data.bind.name;
	expr* zx = pool_request(inter->mem, sizeof(expr));
	zx->tag = APPL_EXPR;
	zx->data.appl.left = z_inner;
	zx->data.appl.right = x_inner;
	expr* zxy = pool_request(inter->mem, sizeof(expr));
	zxy->tag = APPL_EXPR;
	zxy->data.appl.left = zx;
	zxy->data.appl.right = y_inner;
	z->data.bind.expression = zxy;
	return x;
}

void
test_puzzles(){
	printf("Puzzles\n");
	pool mem = pool_alloc(POOL_SIZE, POOL_DYNAMIC);
	interpreter inter = interpreter_init(&mem);
	printf("1\n");
	generate_puzzle(&inter, 6, 4, 4, 4, 3);
	pool_dealloc(&mem);
}

void
test_entropic_puzzles(){
	pool mem = pool_alloc(POOL_SIZE, POOL_DYNAMIC);
	interpreter inter = interpreter_init(&mem);
	printf("Entropic puzzles\n");
	printf("1\n");
	generate_entropic_puzzle(&inter, 6, 4, 4, 4, 3);
	pool_dealloc(&mem);
}

void
test_fuzz_puzzle(){
	pool mem = pool_alloc(POOL_SIZE, POOL_DYNAMIC);
	interpreter inter = interpreter_init(&mem);
	printf("Fuzz puzzles\n");
	printf("1\n");
	generate_fuzz_puzzle(&inter, 6, 4, 2, 4, 3, 2);
	pool_dealloc(&mem);
}

void
test_strike_puzzle(){
	pool mem = pool_alloc(POOL_SIZE, POOL_DYNAMIC);
	interpreter inter = interpreter_init(&mem);
	printf("Strike puzzles\n");
	printf("1\n");
	generate_strike_puzzle(&inter);
	pool_dealloc(&mem);
}

uint8_t
lex_natural(parser* const parse, char* cstr, uint64_t i, token* t){
	t->data.nat = 0;
	t->tag = NATURAL_TOKEN;
	char c = cstr[i];
	while (c != '\0'){
		if (isdigit(c) == 0){
			break;
		}
		t->data.nat *= 10;
		t->data.nat += (c-48);
		i += 1;
		c = cstr[i];
	}
	return i;
}

uint8_t
lex_identifier(parser* const parse, char* cstr, uint64_t i, token* t){
	t->data.name.str = &cstr[i];
t->data.name.len = 0;
	t->tag = IDENTIFIER_TOKEN;
	char c = cstr[i];
	while (c != '\0'){
		if (!isalnum(c) && c != '_' && c != '`'){
			break;
		}
		t->data.name.len += 1;
		i += 1;
		c = cstr[i];
	}
	char* name  = t->data.name.str;
	uint64_t size = t->data.name.len;
	char save = name[size];
	name[size] = '\0';
	char* new = pool_request(parse->inter->mem, t->data.name.len+1);
	strncpy(new, t->data.name.str, t->data.name.len);
	t->data.name.str = new;
	TOKEN* tok = TOKEN_map_access(parse->combinators, name);
	if (tok != NULL){
		t->tag = *tok;
	}
	name[size] = save;
	return i;
}

void
lex_cstr(parser* const parse, char* cstr){
	uint64_t i = 0;
	char c = cstr[i];
	parse->tokens = pool_request(parse->token_pool, sizeof(token));
	while (c != '\0'){
		token* t = &parse->tokens[parse->token_count++];
		switch (c){
		case ' ':
		case '\n':
		case '\r':
		case '\t':
			i += 1;
			c = cstr[i];
			parse->token_count -= 1;
			continue;
		case LAMBDA_TOKEN:
		case BIND_TOKEN:
		case OPEN_PAREN_TOKEN:
		case CLOSE_PAREN_TOKEN:
			t->tag = c;
			pool_request(parse->token_pool, sizeof(token));
			i += 1;
			c = cstr[i];
			continue;
		}
		if (isdigit(c)){
			i = lex_natural(parse, cstr, i, t);
		}
		else if (isalpha(c) || c == '_' || c == '`'){
			i = lex_identifier(parse, cstr, i, t);
		}
		else {
			fprintf(stderr, "Unexpected character '%c'\n", c);
			return;
		}
		c = cstr[i];
		pool_request(parse->token_pool, sizeof(token));
	}
}

void
show_tokens(parser* const parse){
	for (uint64_t i = 0;i<parse->token_count;++i){
		token t = parse->tokens[i];
		printf("[ ");
		switch (t.tag){
		case LAMBDA_TOKEN:
		case BIND_TOKEN:
		case OPEN_PAREN_TOKEN:
		case CLOSE_PAREN_TOKEN:
			printf("'%c'", t.tag);
			break;
		case IDENTIFIER_TOKEN:
			char* name = t.data.name.str;
			uint64_t size = t.data.name.len;
			char save = name[size];
			name[size] = '\0';
			printf("IDENTIFIER %lu '%s'", size, name);
			name[size] = save;
			break;
		case NATURAL_TOKEN:
			printf("NAT %lu", t.data.nat);
			break;
		default:
			printf("COMBINATOR %u", t.tag);
			break;
		}
		printf(" ] ");
	}
	printf("\n");
}

expr*
parse_lambda(parser* const parse, uint8_t paren){
	expr* term = pool_request(parse->inter->mem, sizeof(expr));
	token* t = &parse->tokens[parse->token_index];
	if (t->tag != IDENTIFIER_TOKEN){
		fprintf(stderr, "Expected identifier to bind\n");
		return NULL;
	}
	parse->token_index += 1;
	term->tag = BIND_EXPR;
	term->data.bind.name = next_string(parse->inter);
	string_map_insert(parse->names, t->data.name.str, &term->data.bind.name);
	t = &parse->tokens[parse->token_index];
	parse->token_index += 1;
	if (t->tag != BIND_TOKEN){
		fprintf(stderr, "Expected bind token after lambda identifier\n");
		return NULL;
	}
	term->data.bind.expression = parse_term_recursive(parse, paren);
	return term;
}

expr*
parse_body_term(parser* const parse, token* t, uint8_t paren){
	expr* outer;
	switch (t->tag){
	case LAMBDA_TOKEN:
		return parse_lambda(parse, paren);
	case BIND_TOKEN:
		fprintf(stderr, "Unexpected bind token\n");
		return NULL;
	case OPEN_PAREN_TOKEN:
		outer = parse_term_recursive(parse, 1);
		if (parse->tokens[parse->token_index].tag != CLOSE_PAREN_TOKEN){
			fprintf(stderr, "Expected ) to end expression\n");
			return NULL;
		}
		parse->token_index += 1;
		return outer;
	case CLOSE_PAREN_TOKEN:
		fprintf(stderr, "Unexpected )\n");
		return NULL;
	case IDENTIFIER_TOKEN:
		outer = pool_request(parse->inter->mem, sizeof(expr));
		outer->tag = NAME_EXPR;
		char* s = t->data.name.str;
		uint64_t size = t->data.name.len;
		char save = s[size];
		s[size] = '\0';
		string* name = string_map_access(parse->names, s);
		if (name != NULL){
			s[size] = save;
			outer->data.name = *name;
			return outer;
		}
		expr* found_term = expr_map_access(&parse->inter->universe, s);
		if (found_term != NULL){
			s[size] = save;
			outer->data.name.str = s;
			outer->data.name.len = size;
			return outer;
		}
		if (parse->bind_check == 0){
			s[size] = save;
			outer->data.name.str = s;
			outer->data.name.len = size;
			return outer;
		}
		fprintf(stderr, "Unknown bound name %s\n", t->data.name.str);
		return NULL;
	case NATURAL_TOKEN:
		return build_nat(parse->inter, t->data.nat);
	case S_TOKEN:
		return build_s(parse->inter);
	case K_TOKEN:
		return build_k(parse->inter);
	case I_TOKEN:
		return build_i(parse->inter);
	case B_TOKEN:
		return build_b(parse->inter);
	case C_TOKEN:
		return build_c(parse->inter);
	case W_TOKEN:
		return build_w(parse->inter);
	case A_TOKEN:
		return build_a(parse->inter);
	case T_TOKEN:
		return build_t(parse->inter);
	case M_TOKEN:
		return build_m(parse->inter);
	case AND_TOKEN:
		return build_and(parse->inter);
	case OR_TOKEN:
		return build_or(parse->inter);
	case NOT_TOKEN:
		return build_not(parse->inter);
	case SUCC_TOKEN:
		return build_succ(parse->inter);
	case ADD_TOKEN:
		return build_add(parse->inter);
	case MUL_TOKEN:
		return build_mul(parse->inter);
	case EXP_TOKEN:
		return build_exp(parse->inter);
	case TRUE_TOKEN:
		return build_T(parse->inter);
	case FALSE_TOKEN:
		return build_F(parse->inter);
	case CONS_TOKEN:
		return build_cons(parse->inter);
	}
	return NULL;
}

expr*
parse_term_recursive(parser* const parse, uint8_t paren){
	expr* outer;
	uint8_t initial = 1;
	while (parse->token_index < parse->token_count){
		token* t = &parse->tokens[parse->token_index];
		parse->token_index += 1;
		if (initial == 1){
			if (t->tag == CLOSE_PAREN_TOKEN){
				fprintf(stderr, "Empty expression\n");
				return NULL;
			}
			outer = parse_body_term(parse, t, paren);
			if (outer == NULL){
				return NULL;
			}
			initial = 0;
		}
		else {
			if (t->tag == CLOSE_PAREN_TOKEN){
				if (paren == 1){
					parse->token_index -= 1;
					return outer;
				}
			}
			expr* new_outer = pool_request(parse->inter->mem, sizeof(expr));
			new_outer->tag = APPL_EXPR;
			new_outer->data.appl.left = outer;
			outer = new_outer;
			outer->data.appl.right = parse_body_term(parse, t, paren);
			if (outer->data.appl.right == NULL){
				return NULL;
			}
		}
	}
	return outer;
}

void
populate_combinators(TOKEN_map* map){
	uint64_t count = CONS_TOKEN-NATURAL_TOKEN;
	TOKEN* tokens = pool_request(map->mem, sizeof(TOKEN)* count);
	for (uint8_t i = 0;i<count;++i){
		tokens[i] = i+S_TOKEN;
	}
	TOKEN_map_insert(map, "`S", tokens++);
	TOKEN_map_insert(map, "`K", tokens++);
	TOKEN_map_insert(map, "`I", tokens++);
	TOKEN_map_insert(map, "`B", tokens++);
	TOKEN_map_insert(map, "`C", tokens++);
	TOKEN_map_insert(map, "`W", tokens++);
	TOKEN_map_insert(map, "`A", tokens++);
	TOKEN_map_insert(map, "`T", tokens++);
	TOKEN_map_insert(map, "`M", tokens++);
	TOKEN_map_insert(map, "`AND", tokens++);
	TOKEN_map_insert(map, "`OR", tokens++);
	TOKEN_map_insert(map, "`NOT", tokens++);
	TOKEN_map_insert(map, "`SUCC", tokens++);
	TOKEN_map_insert(map, "`ADD", tokens++);
	TOKEN_map_insert(map, "`MUL", tokens++);
	TOKEN_map_insert(map, "`EXP", tokens++);
	TOKEN_map_insert(map, "`TRUE", tokens++);
	TOKEN_map_insert(map, "`FALSE", tokens++);
	TOKEN_map_insert(map, "`CONS", tokens++);
}

expr*
parse_term(char* cstr, interpreter* const inter){
	string_map names = string_map_init(inter->mem);
	TOKEN_map combinators = TOKEN_map_init(inter->mem);
	populate_combinators(&combinators);
	pool tokens = pool_alloc(POOL_SIZE, POOL_STATIC);
	parser parse = {
		.inter = inter,
		.names = &names,
		.combinators = &combinators,
		.token_pool = &tokens,
		.token_count = 0,
		.token_index = 0,
		.bind_check = 1
	};
	lex_cstr(&parse, cstr);
#ifdef DEBUG
	show_tokens(&parse);
#endif
	expr* term = parse_term_recursive(&parse, 0);
#ifdef DEBUG
	printf("Parsed term: ");
	show_term(term);
	printf("\n");
#endif
	pool_dealloc(&tokens);
	return term;
}

expr*
parse_grammar(char* cstr, interpreter* const inter){
	string_map names = string_map_init(inter->mem);
	TOKEN_map combinators = TOKEN_map_init(inter->mem);
	populate_combinators(&combinators);
	pool tokens = pool_alloc(POOL_SIZE, POOL_STATIC);
	parser parse = {
		.inter = inter,
		.names = &names,
		.combinators = &combinators,
		.token_pool = &tokens,
		.token_count = 0,
		.token_index = 0,
		.bind_check = 0
	};
	lex_cstr(&parse, cstr);
#ifdef DEBUG
	show_tokens(&parse);
#endif
	expr* term = parse_term_recursive(&parse, 0);
#ifdef DEBUG
	printf("Parsed term: ");
	show_term(term);
	printf("\n");
#endif
	pool_dealloc(&tokens);
	return term;
}
void
add_to_universe(interpreter* const inter, char* name, char* eval){
	uint64_t len = strnlen(name, NAME_MAX);
	uint64_t eval_len = strnlen(eval, NAME_MAX*4);
	char* term_name = pool_request(inter->mem, len+1);
	strncpy(term_name, name, len);
	char* term_eval = pool_request(inter->mem, eval_len+1);
	strncpy(term_eval, eval, eval_len);
	expr* term = parse_term(term_eval, inter);
	if (term == NULL){
		return;
	}
	expr_map_insert(&inter->universe, term_name, term);
}

expr*
generate_combinator_term(interpreter* const inter, char** items, uint64_t count, uint64_t max_width, uint64_t min_width){
	uint64_t width = (rand() % (max_width - min_width)) + min_width;
	expr* f = pool_request(inter->mem, sizeof(expr));
	expr* outer = f;
	while (width > 0) {
		outer->tag = APPL_EXPR;
		uint64_t choice = rand()% count;
		char* token = items[choice];
		uint64_t len = strnlen(token, NAME_MAX);
		char* copy = pool_request(inter->mem, len+1);
		strncpy(copy, token, len);
		expr* name = pool_request(inter->mem, sizeof(expr));
		name->tag = NAME_EXPR;
		name->data.name.str = copy;
		name->data.name.len = len;
		if (width == 1){
			*outer = *name;
			break;
		}
		switch (rand()%2){
		case 0:
			outer->data.appl.left = name;
			outer->data.appl.right = pool_request(inter->mem, sizeof(expr));
			outer = outer->data.appl.right;
			break;
		case 1:
			outer->data.appl.right = name;
			outer->data.appl.left = pool_request(inter->mem, sizeof(expr));
			outer = outer->data.appl.left;
			break;
		}
		width -= 1;
	}
	return f;
}

term_puzzle
generate_combinator_strike_puzzle(interpreter* const inter){
	reset_universe(inter);
	add_to_universe(inter, "flip", "T");
	add_to_universe(inter, "const", "K");
	add_to_universe(inter, "cons", "CONS");
	add_to_universe(inter, "id", "I");
	add_to_universe(inter, "and", "AND");
	add_to_universe(inter, "or", "OR");
	add_to_universe(inter, "not", "NOT");
	add_to_universe(inter, "true", "TRUE");
	add_to_universe(inter, "false", "FALSE");
	add_to_universe(inter, "succ", "SUCC");
	add_to_universe(inter, "add", "ADD");
	add_to_universe(inter, "mul", "MUL");
	add_to_universe(inter, "exp", "EXP");
	add_to_universe(inter, "0", "FALSE");
	add_to_universe(inter, "1", "\\x.\\y.x y");
	add_to_universe(inter, "2", "\\x.\\y.x (x y)");
	add_to_universe(inter, "s", "S");
	add_to_universe(inter, "compose", "\\x.\\y.\\z.x (y z)");
	add_to_universe(inter, "if", "\\x.\\y.\\z.x y z");
	char* items[] = {
		"flip", "const", "cons", "id", "and", "or", "not", "true", "false",
		"succ", "add", "mul", "exp", "0", "1", "2", "s", "compose", "if"
	};
	uint64_t count = 18;
	uint64_t min_term_depth = 1;
	uint64_t max_term_depth = 4;
	uint64_t found_depth = max_term_depth+1;
	uint64_t other_found_depth = 0;
	term_puzzle p;
	while (found_depth > max_term_depth || other_found_depth < min_term_depth){
		found_depth = 0;
		other_found_depth = min_term_depth+1;
		expr* f = generate_combinator_term(inter, items, count, 5, 3);
		p.fun = f;
		rebase_term(inter, f);
		for (uint64_t i = 0;i<PUZZLE_ARG_COUNT;++i){
			expr* copy = deep_copy(inter, NULL, f);
			expr* arg = generate_combinator_term(inter, items, count, max_term_depth, min_term_depth);
			p.args[i] = deep_copy(inter, NULL, arg);
			expr* applied = pool_request(inter->mem, sizeof(expr));
			applied->tag = APPL_EXPR;
			applied->data.appl.left=copy;
			applied->data.appl.right=arg;
			int8_t reductions = 8;
			while (reduce_step(inter, applied, MAX_REDUCTION_DEPTH) != 0 && (reductions-- > 0)){}
			rebase_term(inter, applied);
			p.results[i] = applied;
			uint8_t depth = term_depth(applied);
			if (depth > found_depth){
				found_depth = depth;
			}
			if (depth < other_found_depth){
				other_found_depth = depth;
			}
		}
		if (compare_terms(inter->mem, p.args[0], p.args[1]) == 1){
			found_depth = max_term_depth+1;
			continue;
		}
		if (compare_terms(inter->mem, p.results[0], p.results[1]) == 1){
			found_depth = max_term_depth+1;
			continue;
		}
		for (uint64_t i = 0;i<PUZZLE_ARG_COUNT;++i){
			if (term_contained(inter->mem, p.args[i], p.results[i]) == 1){
				found_depth = max_term_depth+1;
				break;
			}
		}
	}
	for (uint64_t i = 0;i<PUZZLE_ARG_COUNT;++i){
		printf("f ");
		show_term(p.args[i]);
		printf(" -> ");
		show_term(p.results[i]);
		printf("\n");
		printf("f ");
		show_term_unambiguous(p.args[i]);
		printf(" -> ");
		show_term_unambiguous(p.results[i]);
		printf("\n");
	}
	printf("f: \033[8m");
	show_term(p.fun);
	printf("\033[0m\n");
	printf("f: \033[8m");
	show_term_unambiguous(p.fun);
	printf("\033[0m\n");
	printf("\n");
	return p;
}

uint8_t
compare_terms_helper(expr* const a, expr* const b, string_map* const map){
	if (a->tag != b->tag){
		return 0;
	}
	switch (a->tag){
	case BIND_EXPR:
		char* copy = pool_request(map->mem, a->data.bind.name.len+1);
		strncpy(copy, a->data.bind.name.str, a->data.bind.name.len);
		string_map_insert(map, copy, &b->data.bind.name);
		return compare_terms_helper(a->data.bind.expression, b->data.bind.expression, map);
	case APPL_EXPR:
		return compare_terms_helper(a->data.appl.left, b->data.appl.left, map)
		     & compare_terms_helper(a->data.appl.right, b->data.appl.right, map);
	case NAME_EXPR:
		string* isb = string_map_access(map, a->data.name.str);
		if (isb == NULL){
			if (a->data.name.len != b->data.name.len){
				return 0;
			}
			return !strncmp(a->data.name.str, b->data.name.str, a->data.name.len);
		}
		if (isb->len != b->data.name.len){
			return 0;
		}
		return !strncmp(isb->str, b->data.name.str, isb->len);
	}
	return 0;
}

uint8_t
compare_terms(pool* const mem, expr* const a, expr* const b){
	pool_save(mem);
	string_map map = string_map_init(mem);
	uint8_t result = compare_terms_helper(a, b, &map);
	pool_load(mem);
	return result;
}

uint8_t
term_contained(pool* const mem, expr* const a, expr* const b){
	switch (b->tag){
	case BIND_EXPR:
		if (compare_terms(mem, a, b) == 1){
			return 1;
		}
		if (compare_terms(mem, a, b->data.bind.expression) == 1){
			return 1;
		}
		return term_contained(mem, a, b->data.bind.expression);
	case APPL_EXPR:
		if (compare_terms(mem, a, b->data.appl.left) == 1){
			return 1;
		}
		if (compare_terms(mem, a, b->data.appl.right) == 1){
			return 1;
		}
		return term_contained(mem, a, b->data.appl.left)
			 | term_contained(mem, a, b->data.appl.right);
	case NAME_EXPR:
		return compare_terms(mem, a, b);
	}
	return 0;
}

void term_flatten(interpreter* const inter, expr* const term){
	switch (term->tag){
	case BIND_EXPR:
		term_flatten(inter, term->data.bind.expression);
		return;
	case APPL_EXPR:
		term_flatten(inter, term->data.appl.left);
		term_flatten(inter, term->data.appl.right);
		return;
	case NAME_EXPR:
		char* name = term->data.name.str;
		uint64_t size = term->data.name.len;
		char save = name[size];
		name[size] = '\0';
		expr* expression = expr_map_access(&inter->universe, term->data.name.str);
		name[size] = save;
		if (expression != NULL){
			*term = *expression;
		}
		return;
	}
}

uint8_t
term_matches_type_worker(pool* const mem, grammar_ptr_map* const env, type_checker* const checker, expr* const term, grammar* const type){
	switch (type->tag){
	case BIND_GRAM:
		if (term->tag != BIND_EXPR){
			return 0;
		}
		type_string_map_insert(&checker->scope, term->data.bind.name, type->data.bind.name);
		if (term->typed == 1 && type->data.bind.typed == 1){
			type_string_map_insert(&checker->scope_types, term->type_name, type->data.bind.type);
		}
		return term_matches_type_worker(mem, env, checker, term->data.bind.expression, type->data.bind.expression);
	case APPL_GRAM:
		if (term->tag != APPL_EXPR){
			return 0;
		}
		return term_matches_type_worker(mem, env, checker, term->data.appl.left, type->data.appl.left)
			 & term_matches_type_worker(mem, env, checker, term->data.appl.right, type->data.appl.right);
	case NAME_GRAM:
		if (term->tag != NAME_EXPR){
			return 0;
		}
		string* name = type_string_map_access(&checker->scope, term->data.name);
		if (name == NULL){
			return 0;
		}
		if (string_compare(name, &type->data.name.name) != 0){
			return 0;
		}
		return 1;
	case TYPE_GRAM:
		if (term->tag == NAME_EXPR){
			string* term_type = type_string_map_access(&checker->scope_types, term->data.name);
			if (string_compare(term_type, &type->data.type.name) == 0){
				return 1;
			}
		}
		grammar** target = grammar_ptr_map_access(env, type->data.type.name);
		if (target == NULL){
			target = grammar_ptr_map_access(&checker->parameters, type->data.type.name);
			if (target == NULL){
				return 0;
			}
		}
		string* applied_params = pool_request(mem, sizeof(string)*type->data.type.param_count);
		for (uint64_t i = 0;i<type->data.type.param_count;++i){
			string newname = type->params[i];
			grammar** applied = grammar_ptr_map_access(env, newname);
			if (applied == NULL){
				string* param_name = type_string_map_access(&checker->param_names, type->params[i]);
				if (param_name == NULL){
					return 0;
				}
				newname = *param_name;
			}
			applied_params[i] = newname;
		}
		return term_matches_type(mem, env, term, *target, applied_params, type->data.type.param_count);
	}
	return 0;
}

uint8_t
term_matches_type(pool* const mem, grammar_ptr_map* const env, expr* const term, grammar* const type, string* param_args, uint64_t param_arg_count){
	pool_save(mem);
	type_checker checker = {
		.parameters = grammar_ptr_map_init(mem),
		.param_names = type_string_map_init(mem),
		.scope = type_string_map_init(mem),
		.scope_types = type_string_map_init(mem)
	};
	if (type->param_count < param_arg_count){
		return 0;
	}
	for (uint64_t i = 0;i<param_arg_count;++i){
		grammar** expansion = grammar_ptr_map_access(env, param_args[i]);
		if (expansion != NULL){
			grammar_ptr_map_insert(&checker.parameters, type->params[i], *expansion);
		}
		type_string_map_insert(&checker.param_names, type->params[i], param_args[i]);
	}
	uint8_t res = term_matches_type_worker(mem, env, &checker, term, type);
	if (res == 0){
		for (uint64_t i = 0;i<type->alt_count;++i){
			res = term_matches_type_worker(mem, env, &checker, term, &type->alts[i]);
			if (res == 1){
				break;
			}
		}
		return res;
	}
	pool_load(mem);
	return res;
}

void
create_bind_grammar(grammar* const type){
	type->tag = BIND_GRAM;
	type->params = NULL;
	type->param_count = 0;
	type->alts = NULL;
	type->alt_count = 0;
	type->alt_capacity = 0;
}

void
create_appl_grammar(grammar* const type){
	type->tag = APPL_GRAM;
	type->params = NULL;
	type->param_count = 0;
	type->alts = NULL;
	type->alt_count = 0;
	type->alt_capacity = 0;
}

void
create_name_grammar(grammar* const type){
	type->tag = NAME_GRAM;
	type->params = NULL;
	type->param_count = 0;
	type->alts = NULL;
	type->alt_count = 0;
	type->alt_capacity = 0;
}

void
create_type_grammar(grammar* const type){
	type->tag = TYPE_GRAM;
	type->params = NULL;
	type->param_count = 0;
	type->alts = NULL;
	type->alt_count = 0;
	type->alt_capacity = 0;
}

void
type_add_alt_worker(pool* const mem, grammar_ptr_map* const env, uint8_t_map* const param_map, grammar* const type, expr* const term, param_list* recursive_params){
	switch (term->tag){
	case BIND_EXPR:
		create_bind_grammar(type);
		type->data.bind.name = term->data.bind.name;
		type->data.bind.typed = 0;
		type->data.bind.expression = pool_request(mem, sizeof(grammar));
		type_add_alt_worker(mem, env, param_map, type->data.bind.expression, term->data.bind.expression, NULL);
		break;
	case APPL_EXPR:
		create_appl_grammar(type);
		type->data.appl.left = pool_request(mem, sizeof(grammar));
		type->data.appl.right = pool_request(mem, sizeof(grammar));
		if (term->data.appl.right->tag == NAME_EXPR){
			if (recursive_params == NULL){
				recursive_params = pool_request(mem, sizeof(param_list));
				recursive_params->name = term->data.appl.right;
				recursive_params->next = NULL;
				recursive_params->len = 1;
				recursive_params->used = 0;
			}
			else{
				param_list* node = pool_request(mem, sizeof(param_list));
				node->name = term->data.appl.right;
				node->next = recursive_params;
				node->len = recursive_params->len + 1;
				node->used = 0;
				recursive_params = node;
			}
		}
		type_add_alt_worker(mem, env, param_map, type->data.appl.left, term->data.appl.left, recursive_params);
		if (recursive_params->used == 1){
			break;
		}
		type_add_alt_worker(mem, env, param_map, type->data.appl.right, term->data.appl.right, NULL);
		break;
	case NAME_EXPR:
		uint8_t* param = uint8_t_map_access(param_map, term->data.name);
		if (param == NULL){
			grammar** envtype = grammar_ptr_map_access(env, term->data.name);
			if (envtype != NULL){
				create_type_grammar(type);
				type->data.type.name = term->data.name;
				return;
			}
			create_name_grammar(type);
			type->data.name.name = term->data.name;
			return;
		}
		create_type_grammar(type);
		type->data.type.name = term->data.name;
		type->data.type.params = NULL;
		type->data.type.param_count = 0;
		if (recursive_params != NULL){
			uint64_t len = recursive_params->len;
			expr* list_term = recursive_params->name;
			type->data.type.params = pool_request(mem, len*sizeof(string));
			uint64_t i = 0;
			for (i = 0;i<len;++i){
				uint8_t* param = uint8_t_map_access(param_map, term->data.name);
				if (param == NULL){
					grammar** env_type = grammar_ptr_map_access(env, list_term->data.name);
					if (env_type == NULL){
						break;
					}
				}
				type->data.type.params[i] = list_term->data.name;
				recursive_params->used = 1;
				type->data.type.param_count += 1;
				recursive_params = recursive_params->next;
			}
		}
		break;
	}
}

void
type_add_alt(pool* const mem, grammar_ptr_map* const env, string* const name, grammar* const type, expr* const term){
	uint8_t_map param_map = uint8_t_map_init(mem);
	for (uint8_t i = 0;i<type->param_count;++i){
		uint8_t_map_insert(&param_map, type->params[i], 1);
	}
	if (type->alts == NULL){
		type->alt_capacity = 2;
		type->alt_count = 0;
		grammar* newalts = pool_request(mem, sizeof(grammar)*type->alt_capacity);
		type->alts = newalts;
		uint64_t param_count = type->param_count;
		string* params = type->params;
		grammar_ptr_map_insert(env, *name, type);
		type_add_alt_worker(mem, env, &param_map, type, term, NULL);
		type->params = params;
		type->param_count = param_count;
		type->alts = newalts;
	}
	else{
		if (type->alt_count == type->alt_capacity){
			type->alt_capacity *= 2;
			grammar* newalts = pool_request(mem, sizeof(grammar)*type->alt_capacity);
			for (uint64_t i = 0;i<type->alt_count;++i){
				newalts[i] = type->alts[i];
			}
		}
		create_type_grammar(&type->alts[type->alt_count]);
		type_add_alt_worker(mem, env, &param_map, &type->alts[type->alt_count], term, NULL);
		type->alt_count += 1;
	}
}

void
show_grammar(grammar* const type){
	switch (type->tag){
	case BIND_GRAM:
		printf("\\");
		string_print(&type->data.bind.name);
		if (type->data.bind.typed == 1){
			printf(": ");
			string_print(&type->data.bind.type);
		}
		printf(".");
		show_grammar(type->data.bind.expression);
		break;
	case APPL_GRAM:
		printf("(");
		show_grammar(type->data.appl.left);
		printf(" ");
		show_grammar(type->data.appl.right);
		printf(")");
		break;
	case NAME_GRAM:
		string_print(&type->data.name.name);
		break;
	case TYPE_GRAM:
		printf("[");
		string_print(&type->data.type.name);
		for (uint64_t i = 0;i<type->data.type.param_count;++i){
			printf(" ");
			string_print(&type->data.type.params[i]);
		}
		printf("]");
	}
}

void
show_simple_type(simple_type* const type){
	switch (type->tag){
	case SUM_TYPE:
		string_print(&type->data.sum.name);
		for (uint64_t i = 0;i<type->data.product.member_count;++i){
			printf(" ");
			show_simple_type(&type->data.product.members[i]);
			if (i+1 != type->data.product.member_count){
				printf(" | ");
			}
		}
		break;
	case PRODUCT_TYPE:
		string_print(&type->data.product.name);
		for (uint64_t i = 0;i<type->data.sum.alt_count;++i){
			printf(" ");
			show_simple_type(&type->data.sum.alts[i]);
		}
		break;
	case FUNCTION_TYPE:
		printf("(");
		show_simple_type(type->data.function.left);
		printf(" -> ");
		show_simple_type(type->data.function.right);
		printf(")");
		break;
	case PARAMETER_TYPE:
		printf("[");
		string_print(&type->data.parameter);
		printf("]");
		break;
	}
}

void
show_simple_type_def(simple_type* const type){
	switch (type->tag){
	case SUM_TYPE:
		string_print(&type->data.sum.name);
		for (uint64_t i = 0;i<type->parameter_count;++i){
			printf(" ");
			string_print(&type->parameters[i]);
		}
		printf(" = ");
		for (uint64_t i = 0;i<type->data.sum.alt_count;++i){
			show_simple_type(&type->data.sum.alts[i]);
			if (i+1 != type->data.sum.alt_count){
				printf(" | ");
			}
		}
		break;
	default:
		printf("Unexpected start to type def\n");
	}
}

void
deep_copy_simple_type(pool* const mem, simple_type* const target, simple_type* const new){
	*new = *target;
	switch (target->tag){
	case SUM_TYPE:
		new->data.sum.alts = pool_request(mem, sizeof(simple_type)*target->data.sum.alt_count);
		for (uint64_t i = 0;i<target->data.sum.alt_count;++i){
			deep_copy_simple_type(mem, &target->data.sum.alts[i], &new->data.sum.alts[i]);
		}
		break;
	case PRODUCT_TYPE:
		new->data.product.members = pool_request(mem, sizeof(simple_type)*target->data.product.member_count);
		for (uint64_t i = 0;i<target->data.product.member_count;++i){
			deep_copy_simple_type(mem, &target->data.product.members[i], &new->data.product.members[i]);
		}
		break;
	case FUNCTION_TYPE:
		new->data.function.left = pool_request(mem, sizeof(simple_type));
		new->data.function.right = pool_request(mem, sizeof(simple_type));
		deep_copy_simple_type(mem, target->data.function.left, new->data.function.left);
		deep_copy_simple_type(mem, target->data.function.right, new->data.function.right);
		break;
	default:
		break;
	}
}

void
deep_copy_simple_type_replace(pool* const mem, simple_type* const target, simple_type* const new, string replacee, simple_type* const replacement){
	*new = *target;
	switch (target->tag){
	case SUM_TYPE:
		new->data.sum.alts = pool_request(mem, sizeof(simple_type)*target->data.sum.alt_count);
		for (uint64_t i = 0;i<target->data.sum.alt_count;++i){
			deep_copy_simple_type(mem, &target->data.sum.alts[i], &new->data.sum.alts[i]);
		}
		break;
	case PRODUCT_TYPE:
		new->data.product.members = pool_request(mem, sizeof(simple_type)*target->data.product.member_count);
		for (uint64_t i = 0;i<target->data.product.member_count;++i){
			deep_copy_simple_type(mem, &target->data.product.members[i], &new->data.product.members[i]);
		}
		break;
	case FUNCTION_TYPE:
		new->data.function.left = pool_request(mem, sizeof(simple_type));
		new->data.function.right = pool_request(mem, sizeof(simple_type));
		deep_copy_simple_type(mem, target->data.function.left, new->data.function.left);
		deep_copy_simple_type(mem, target->data.function.right, new->data.function.right);
		break;
	case PARAMETER_TYPE:
		if (string_compare(&target->data.parameter, &replacee) == 0){
			simple_type fill;
			deep_copy_simple_type(mem, replacement, &fill);
			*new = fill;
		}
		break;
	default:
		break;
	}
}

uint8_t
simple_type_compare(pool* const mem, simple_type* const left, simple_type* const right, parameter_string_map* params){
	parameter_string_map p;
	if (params == NULL){
		p = parameter_string_map_init(mem);
		params = &p;
	}
	if (left->tag != right->tag){
		return 0;
	}
	if (left->parameter_count != right->parameter_count){
		return 0;
	}
	for (uint64_t i = 0;i<left->parameter_count;++i){
		parameter_string_map_insert(params, left->parameters[i], right->parameters[i]);
	}
	switch (left->tag){
	case SUM_TYPE:
		if (left->data.sum.alt_count != right->data.sum.alt_count){
			return 0;
		}
		for (uint64_t i = 0;i<left->data.sum.alt_count;++i){
			if (simple_type_compare(mem, &left->data.sum.alts[i], &right->data.sum.alts[i], params) == 0){
				return 0;
			}
		}
		return 1;
	case PRODUCT_TYPE:
		if (left->data.product.member_count != right->data.product.member_count){
			return 0;
		}
		for (uint64_t i = 0;i<left->data.product.member_count;++i){
			if (simple_type_compare(mem, &left->data.product.members[i], &right->data.product.members[i], params) == 0){
				return 0;
			}
		}
		return 1;
	case FUNCTION_TYPE:
		return simple_type_compare(mem, left->data.function.left, right->data.function.left, params)
		     | simple_type_compare(mem, left->data.function.right, right->data.function.right, params);
	case PARAMETER_TYPE:
		parameter_string* converted = parameter_string_map_access(params, left->data.parameter);
		if (string_compare(converted, &right->data.parameter) != 0){
			return 0;
		}
		return 1;
	}
	return 0;
}

uint8_t
simple_type_compare_parametric_differences(pool* const mem, simple_type* const left, simple_type* const right, parameter_diff* const node, parameter_string_map* params){
	parameter_string_map p;
	if (params == NULL){
		p = parameter_string_map_init(mem);
		params = &p;
	}
	if (left->tag != right->tag){
		if (left->tag == PARAMETER_TYPE){
			parameter_string* converted = parameter_string_map_access(params, left->data.parameter);
			if (converted != NULL){
				return 0;
			}
			parameter_diff* next = node->next;
			parameter_diff* newnode = pool_request(mem, sizeof(parameter_diff));
			newnode->parameter = left->data.parameter;
			newnode->diff = right;
			newnode->next = next;
			node->next = newnode;
			return 1;
		}
		return 0;
	}
	if (left->parameter_count != right->parameter_count){
		return 0;
	}
	for (uint64_t i = 0;i<left->parameter_count;++i){
		parameter_string_map_insert(params, left->parameters[i], right->parameters[i]);
	}
	switch (left->tag){
	case SUM_TYPE:
		if (left->data.sum.alt_count != right->data.sum.alt_count){
			return 0;
		}
		for (uint64_t i = 0;i<left->data.sum.alt_count;++i){
			if (simple_type_compare_parametric_differences(mem, &left->data.sum.alts[i], &right->data.sum.alts[i], node, params) == 0){
				return 0;
			}
		}
		return 1;
	case PRODUCT_TYPE:
		if (left->data.product.member_count != right->data.product.member_count){
			return 0;
		}
		for (uint64_t i = 0;i<left->data.product.member_count;++i){
			if (simple_type_compare_parametric_differences(mem, &left->data.product.members[i], &right->data.product.members[i], node, params) == 0){
				return 0;
			}
		}
		return 1;
	case FUNCTION_TYPE:
		return simple_type_compare_parametric_differences(mem, left->data.function.left, right->data.function.left, node, params)
		     | simple_type_compare_parametric_differences(mem, left->data.function.right, right->data.function.right, node, params);
	case PARAMETER_TYPE:
		parameter_string* converted = parameter_string_map_access(params, left->data.parameter);
		if (string_compare(converted, &right->data.parameter) != 0){
			return 0;
		}
		return 1;
	}
	return 0;
}

void deep_copy_simple_type_replace_multiple(pool* const mem, simple_type* const target, simple_type* const new, simple_type_map* const replacement_map){
	*new = *target;
	switch (target->tag){
	case SUM_TYPE:
		new->data.sum.alts = pool_request(mem, sizeof(simple_type)*target->data.sum.alt_count);
		for (uint64_t i = 0;i<target->data.sum.alt_count;++i){
			deep_copy_simple_type(mem, &target->data.sum.alts[i], &new->data.sum.alts[i]);
		}
		break;
	case PRODUCT_TYPE:
		new->data.product.members = pool_request(mem, sizeof(simple_type)*target->data.product.member_count);
		for (uint64_t i = 0;i<target->data.product.member_count;++i){
			deep_copy_simple_type(mem, &target->data.product.members[i], &new->data.product.members[i]);
		}
		break;
	case FUNCTION_TYPE:
		new->data.function.left = pool_request(mem, sizeof(simple_type));
		new->data.function.right = pool_request(mem, sizeof(simple_type));
		deep_copy_simple_type(mem, target->data.function.left, new->data.function.left);
		deep_copy_simple_type(mem, target->data.function.right, new->data.function.right);
		break;
	case PARAMETER_TYPE:
		simple_type* replacement = simple_type_map_access(replacement_map, target->data.parameter);
		if (replacement != NULL){
			deep_copy_simple_type(mem, replacement, new);
		}
		break;
	default:
		break;
	}
}

simple_type**
create_constructor_types(pool* const mem, simple_type* type, uint64_t* len){
	*len = 0;
	simple_type* base;
	simple_type* focus;
	simple_type** buffer;
	switch (type->tag){
	case SUM_TYPE:
		buffer = pool_request(mem, sizeof(simple_type)*type->data.sum.alt_count);
		for (uint64_t k = 0;k<type->data.sum.alt_count;++k){
			uint64_t size;
			simple_type** intermediate = create_constructor_types(mem, &type->data.sum.alts[k], &size);
			for (uint64_t i = 0;i<size;++i){
				buffer[*len] = intermediate[i];
				*len += 1;
			}
		}
		return buffer;
	case PRODUCT_TYPE:
		base = pool_request(mem, sizeof(simple_type));
		focus = base;
		for (uint64_t i = 0;i<type->data.product.member_count;++i){
			focus->tag = FUNCTION_TYPE;
			focus->data.function.left = pool_request(mem, sizeof(simple_type));
			focus->data.function.right = pool_request(mem, sizeof(simple_type));
			simple_type* left = focus->data.function.left;
			deep_copy_simple_type(mem, &type->data.product.members[i], left);
			focus = focus->data.function.right;
		}
		deep_copy_simple_type(mem, focus, type);
		buffer = pool_request(mem, sizeof(simple_type));
		buffer[*len] = base;
		*len += 1;
		return buffer;
	default:
		return NULL;
	}
	return NULL;
}

uint8_t
lex_type_identifier(type_parser* const parse, char* cstr, uint64_t i, type_token* t){
	t->name.str = &cstr[i];
	t->name.len = 0;
	t->tag = TYPE_IDENTIFIER_TOKEN;
	char c = cstr[i];
	while (c != '\0'){
		if (!isalnum(c) && c != '_'){
			break;
		}
		t->name.len += 1;
		i += 1;
		c = cstr[i];
	}
	return i;
}

void
lex_type(type_parser* const parse, char* cstr){
	uint64_t i = 0;
	char c = cstr[i];
	parse->tokens = pool_request(parse->token_pool, sizeof(type_token));
	while (c != '\0'){
		type_token* t = &parse->tokens[parse->token_count++];
		switch (c){
		case ' ':
		case '\n':
		case '\r':
		case '\t':
			i += 1;
			c = cstr[i];
			parse->token_count -= 1;
			continue;
		case TYPE_PAREN_CLOSE_TOKEN:
		case TYPE_PAREN_OPEN_TOKEN:
		case TYPE_ALT_TOKEN:
		case TYPE_EQ_TOKEN:
			t->tag = c;
			i += 1;
			c = cstr[i];
			pool_request(parse->token_pool, sizeof(type_token));
			continue;
		}
		if (c == '-'){
			if (cstr[i+1] == '>'){
				t->tag = TYPE_IMPL_TOKEN;
				i += 2;
				c = cstr[i];
				pool_request(parse->token_pool, sizeof(type_token));
				continue;
			}
		}
		if (isalpha(c) || c == '_'){
			i = lex_type_identifier(parse, cstr, i, t);
			c = cstr[i];
			pool_request(parse->token_pool, sizeof(type_token));
			continue;
		}
		fprintf(stderr, "Unexpected character '%c'\n", c);
	}
}

uint8_t
parse_product_def(type_parser* const parse, simple_type* focus, uint8_t nested){
	type_token* t = &parse->tokens[parse->token_index];
	parse->token_index += 1;
	if (t->tag != TYPE_IDENTIFIER_TOKEN){
		return 1;
	}
	focus->tag = PRODUCT_TYPE;
	focus->data.product.name = t->name;
	focus->data.product.member_count = 0;
	uint64_t member_capacity = 2;
	focus->data.product.members = pool_request(parse->inter->mem, sizeof(simple_type)*member_capacity);
	while (parse->token_index < parse->token_count){
		if (focus->data.product.member_count == member_capacity){
			member_capacity *= 2;
			simple_type* newmembers = pool_request(parse->inter->mem, sizeof(simple_type)*member_capacity);
			for (uint64_t i = 0;i<focus->data.product.member_count;++i){
				newmembers[i] = focus->data.product.members[i];
			}
			focus->data.product.members = newmembers;
		}
		t = &parse->tokens[parse->token_index];
		parse->token_index += 1;
		if (t->tag == TYPE_ALT_TOKEN){
			parse->token_index -= 1;
			return 0;
}
		if (t->tag == TYPE_PAREN_OPEN_TOKEN){
			if (parse_product_def(parse, &focus->data.product.members[focus->data.product.member_count], 1) != 0){
				return 1;
			}
			focus->data.product.member_count += 1;
		}
		else if (t->tag == TYPE_PAREN_CLOSE_TOKEN){
			if (nested == 0){
				fprintf(stderr, "Unexpected ) in unnested type definition expression\n");
				return 1;
			}
			return 0;
		}
		else if (t->tag == TYPE_IDENTIFIER_TOKEN){
			simple_type* member = &focus->data.product.members[focus->data.product.member_count];
			member->tag = PRODUCT_TYPE;
			member->data.product.name = t->name;
			member->data.product.member_count = 0;
			focus->data.product.member_count += 1;
		}
		else if (t->tag == TYPE_IMPL_TOKEN){
			simple_type* left = pool_request(parse->inter->mem, sizeof(simple_type));
			*left = *focus;
			focus->tag = FUNCTION_TYPE;
			focus->data.function.left = left;
			focus->data.function.right = pool_request(parse->inter->mem, sizeof(simple_type));
			if (parse_product_def(parse, focus->data.function.right, nested) != 0){
				return 1;
			}
			return 0;
		}
		t = &parse->tokens[parse->token_index];
		if (t->tag == TYPE_IMPL_TOKEN){
			parse->token_index += 1;
			simple_type* left = pool_request(parse->inter->mem, sizeof(simple_type));
			*left = *focus;
			focus->tag = FUNCTION_TYPE;
			focus->data.function.left = left;
			focus->data.function.right = pool_request(parse->inter->mem, sizeof(simple_type));
			if (parse_product_def(parse, focus->data.function.right, nested) != 0){
				return 1;
			}
			return 0;
		}
	}
	return 0;
}

simple_type*
parse_type_def_recursive(type_parser* const parse){
	simple_type* focus = pool_request(parse->inter->mem, sizeof(simple_type));
	simple_type* base = focus;
	focus->tag = SUM_TYPE;
	focus->data.sum.alt_count = 1;
	type_token* t = &parse->tokens[parse->token_index];
	parse->token_index += 1;
	if (t->tag != TYPE_IDENTIFIER_TOKEN){
		fprintf(stderr, "Expected name for outer type definition\n");
		return NULL;
	}
	focus->data.sum.name = t->name;
	uint64_t token_index_save = parse->token_index;
	focus->parameter_count = 0;
	t = &parse->tokens[parse->token_index];
	parse->token_index += 1;
	while (t->tag != TYPE_EQ_TOKEN && parse->token_index < parse->token_count){
		focus->parameter_count += 1;
		t = &parse->tokens[parse->token_index];
		parse->token_index += 1;
	}
	if (parse->token_index >= parse->token_count){
		fprintf(stderr, "No type definition found for name and parameter set\n");
		return NULL;
	}
	parse->token_index = token_index_save;
	t = &parse->tokens[parse->token_index];
	parse->token_index += 1;
	focus->parameters = pool_request(parse->inter->mem, sizeof(string)*focus->parameter_count);
	for (uint64_t i = 0;i<focus->parameter_count;++i){
		focus->parameters[i] = t->name;
		t = &parse->tokens[parse->token_index];
		parse->token_index += 1;
	}
	uint64_t alt_capacity = 2;
	focus->data.sum.alts = pool_request(parse->inter->mem, sizeof(simple_type)*alt_capacity);
	focus = &base->data.sum.alts[0];
	while (parse->token_index < parse->token_count){
		uint8_t err = parse_product_def(parse, focus, 0);
		if (err != 0){
			return NULL;
		}
		if (parse->token_index >= parse->token_count){
			return base;
		}
		t = &parse->tokens[parse->token_index];
		parse->token_index += 1;
		if (t->tag == TYPE_ALT_TOKEN){
			base->data.sum.alt_count += 1;
			if (base->data.sum.alt_count == alt_capacity){
				alt_capacity *= 2;
				simple_type* members = pool_request(parse->inter->mem, sizeof(simple_type)*alt_capacity);
				for (uint64_t i = 0;i<base->data.sum.alt_count;++i){
					members[i] = base->data.sum.alts[i];
				}
				base->data.sum.alts = members;
			}
			focus = &base->data.sum.alts[base->data.sum.alt_count-1];
		}
	}
	return base;
}

uint8_t simple_type_constructors_unique_worker(interpreter* const inter, simple_type* const type){
	expr* cons;
	char* name;
	uint64_t size;
	switch (type->tag){
	case PRODUCT_TYPE:
		size = type->data.product.name.len;
		name = pool_request(inter->mem, size+1);
		strncpy(name, type->data.product.name.str, size);
		name[size] = '\0';
		cons = expr_map_access(&inter->universe, name);
		if (cons == NULL){
			return 1;
		}
		fprintf(stderr, "Duplicate constructor defined ");
		string_print(&type->data.product.name);
		printf("\n");
		return 0;
	case SUM_TYPE:
	case FUNCTION_TYPE:
	case PARAMETER_TYPE:
		break;
	}
	fprintf(stderr, "Unexpected top level alt\n");
	return 1;
}

uint8_t simple_type_constructors_unique(interpreter* const inter, simple_type* const type){
	switch (type->tag){
	case SUM_TYPE:
		for (uint64_t i = 0;i<type->data.sum.alt_count;++i){
			if (simple_type_constructors_unique_worker(inter, &type->data.sum.alts[i]) == 0){
				return 0;
			}
			create_constructor(inter, &type->data.sum.alts[i], type->data.sum.alt_count, i);
		}
		return 1;
	default:
		fprintf(stderr, "Unexpected first level type defintion type\n");
		break;
	}
	return 1;
}

void
create_constructor(interpreter* const inter, simple_type* const type, uint64_t alt_count, uint64_t alt_selector){
	switch (type->tag){
	case PRODUCT_TYPE:
		expr* focus = pool_request(inter->mem, sizeof(expr));
		expr* base = focus;
		for (uint64_t i = 0;i<type->data.product.member_count;++i){
			focus->tag = BIND_EXPR;
			focus->data.bind.name = next_string(inter);
			focus->data.bind.expression = pool_request(inter->mem, sizeof(expr));
			focus->typed = 1;
			focus->simple = pool_request(inter->mem, sizeof(simple_type));
			deep_copy_simple_type(inter->mem, &type->data.product.members[i], focus->simple);
			focus = focus->data.bind.expression;
		}
		string alt_name;
		for (uint64_t i = 0;i<alt_count;++i){
			focus->tag = BIND_EXPR;
			focus->data.bind.name = next_string(inter);
			focus->data.bind.expression = pool_request(inter->mem, sizeof(expr));
			focus->typed = 0;
			if (i == alt_selector){
				alt_name = focus->data.bind.name;
			}
			focus = focus->data.bind.expression;
		}
		focus->tag = NAME_EXPR;
		focus->data.name = alt_name;
		expr* rebase = base;
		for (uint64_t i = 0;i<type->data.product.member_count;++i){
			expr* appl = pool_request(inter->mem, sizeof(expr));
			appl->tag = APPL_EXPR;
			expr temp = *focus;
			*focus = *appl;
			*appl = temp;
			focus->data.appl.left = appl;
			focus->data.appl.right = pool_request(inter->mem, sizeof(expr));
			focus = focus->data.appl.right;
			focus->tag = NAME_EXPR;
			focus->typed = 1;
			focus->simple = pool_request(inter->mem, sizeof(simple_type));
			deep_copy_simple_type(inter->mem, &type->data.product.members[i], focus->simple);
			focus->data.name = rebase->data.bind.name;
			rebase = rebase->data.bind.expression;
		}
		char* name = pool_request(inter->mem, type->data.product.name.len+1);
		strncpy(name,type->data.product.name.str,type->data.product.name.len);
		name[type->data.product.name.len] = '\0';
		expr_map_insert(&inter->universe, name, base);
		break;
	default:
		break;
	}
}

simple_type*
parse_type_def(char* cstr, interpreter* const inter){
	pool tokens = pool_alloc(POOL_SIZE, POOL_STATIC);
	type_parser parse = {
		.inter = inter,
		.token_pool = &tokens,
		.token_count = 0,
		.token_index = 0
	};
	lex_type(&parse, cstr);
#ifdef DEBUG
	show_type_tokens(&parse);
	printf("\n");
#endif
	simple_type* result = parse_type_def_recursive(&parse);
	if (simple_type_constructors_unique(inter, result) == 1){
		simple_type_map_insert(&inter->types, result->data.sum.name, *result);
	}
	else {
		return NULL;
	}
#ifdef DEBUG
	show_simple_type_def(result);
	printf("\n");
#endif
	pool_dealloc(&tokens);
	return result;
}

simple_type*
parse_type_use_recursive(type_parser* const parse, uint8_t nested){
	simple_type* focus = pool_request(parse->inter->mem, sizeof(simple_type));
	simple_type* base = focus;
	type_token* t = &parse->tokens[parse->token_index];
	parse->token_index += 1;
	uint64_t member_capacity = 2;
	if (t->tag == TYPE_IDENTIFIER_TOKEN){
		focus->tag = PRODUCT_TYPE;
		focus->data.product.name = t->name;
		focus->data.product.members = pool_request(parse->inter->mem, sizeof(simple_type)*member_capacity);
		focus->data.product.member_count = 0;
		focus = &base->data.product.members[0];
	}
	else if (t->tag == TYPE_PAREN_OPEN_TOKEN){
		base->tag = FUNCTION_TYPE;
		base->data.function.left = parse_type_use_recursive(parse, 1);
		if (parse->token_index == parse->token_count){
			return base->data.function.left;
		}
		parse->token_index += 1;
		base->data.function.right = parse_type_use_recursive(parse, nested);
		return base;
	}
	else{
		fprintf(stderr, "Expected identifier or nested function for type use\n");
		return NULL;
	}
	while (parse->token_index < parse->token_count){
		t = &parse->tokens[parse->token_index];
		parse->token_index += 1;
		if (base->data.product.member_count == member_capacity){
			member_capacity *= 2;
			simple_type* members = pool_request(parse->inter->mem, sizeof(simple_type)*member_capacity);
			for (uint64_t i = 0;i<base->data.product.member_count;++i){
				members[i] = base->data.product.members[i];
			}
			base->data.product.members = members;
		}
		if (t->tag == TYPE_IDENTIFIER_TOKEN){
			focus->tag = PRODUCT_TYPE;
			focus->data.product.name = t->name;
			focus->data.product.members = NULL;
			focus->data.product.member_count = 0;
			base->data.product.member_count += 1;
			focus = &base->data.product.members[base->data.product.member_count-1];
		}
		else if (t->tag == TYPE_IMPL_TOKEN){
			simple_type* left = pool_request(parse->inter->mem, sizeof(simple_type));
			*left = *base;
			base->tag = FUNCTION_TYPE;
			base->data.function.left = left;
			base->data.function.right = parse_type_use_recursive(parse, nested);
		}
		else if (t->tag == TYPE_PAREN_OPEN_TOKEN){
			*focus = *parse_type_use_recursive(parse, 1);
			base->data.product.member_count += 1;
			focus = &base->data.product.members[base->data.product.member_count-1];
		}
		else if (t->tag == TYPE_PAREN_CLOSE_TOKEN){
			if (nested == 1){
				return base;
			}
			fprintf(stderr, "Unexpected ) in non nested type use\n");
			return NULL;
		}
		else {
			fprintf(stderr, "Unexpected token in type use\n");
			return NULL;
		}
	}
	return base;
}

simple_type*
parse_type_use(char* cstr, interpreter* const inter){
	pool tokens = pool_alloc(POOL_SIZE, POOL_STATIC);
	type_parser parse = {
		.inter = inter,
		.token_pool = &tokens,
		.token_count = 0,
		.token_index = 0
	};
	lex_type(&parse, cstr);
#ifdef DEBUG
	show_type_tokens(&parse);
	printf("\n");
#endif
	simple_type* result = parse_type_use_recursive(&parse, 0);
	parameterize_simple_type(inter, result);
#ifdef DEBUG
	show_simple_type(result);
	printf("\n");
#endif
	pool_dealloc(&tokens);
	return result;
}

void
parameterize_simple_type_worker(interpreter* const inter, simple_type* const type, uint8_t_map* const is_param){
	switch (type->tag){
	case SUM_TYPE:
		for (uint64_t i = 0;i<type->data.sum.alt_count;++i){
			parameterize_simple_type_worker(inter, &type->data.sum.alts[i], is_param);
		}
		break;
	case PRODUCT_TYPE:
		if (uint8_t_map_access(is_param, type->data.product.name) != NULL){
			string param = type->data.product.name;
			type->tag = PARAMETER_TYPE;
			type->data.parameter = param;
			break;
		}
		if (simple_type_map_access(&inter->types, type->data.product.name) == NULL){
			uint8_t_map_insert(is_param, type->data.product.name, 1);
			string param = type->data.product.name;
			type->tag = PARAMETER_TYPE;
			type->data.parameter = param;
			break;
		}
		for (uint64_t i = 0;i<type->data.product.member_count;++i){
			parameterize_simple_type_worker(inter, &type->data.product.members[i], is_param);
		}
		break;
	case FUNCTION_TYPE:
		parameterize_simple_type_worker(inter, type->data.function.left, is_param);
		parameterize_simple_type_worker(inter, type->data.function.right, is_param);
		break;
	case PARAMETER_TYPE:
		break;
	}
}

void
parameterize_simple_type(interpreter* const inter, simple_type* const type){
	uint8_t_map is_param = uint8_t_map_init(inter->mem);
	parameterize_simple_type_worker(inter, type, &is_param);
}

void
show_type_tokens(type_parser* const parse){
	for (uint64_t i = 0;i<parse->token_count;++i){
		switch (parse->tokens[i].tag){
		case TYPE_IDENTIFIER_TOKEN:
			printf("[ IDENTIFIER : ");
			string_print(&parse->tokens[i].name);
			printf(" ] ");
			break;
		case TYPE_IMPL_TOKEN:
			printf("[ IMPL : -> ] ");
			break;
		case TYPE_EQ_TOKEN:
			printf("[ EQ : = ] ");
			break;
		case TYPE_ALT_TOKEN:
			printf("[ ALT : | ] ");
			break;
		case TYPE_PAREN_OPEN_TOKEN:
			printf("[ OPEN : ( ] ");
			break;
		case TYPE_PAREN_CLOSE_TOKEN:
			printf("[ CLOSE : ) ] ");
			break;
		default:
			printf("[ UNKNOWN TYPE TOKEN VARIANT : ??? ] ");
		}
	}
}

void
lower_type(interpreter* const inter, simple_type* const target, simple_type* const new){
	*new = *target;
	switch (target->tag){
	case SUM_TYPE:
		new->data.sum.alts = pool_request(inter->mem, sizeof(simple_type)*target->data.sum.alt_count);
		for (uint64_t i = 0;i<target->data.sum.alt_count;++i){
			lower_type(inter, &target->data.sum.alts[i], &new->data.sum.alts[i]);
		}
		break;
	case PRODUCT_TYPE:
		simple_type* ref = simple_type_map_access(&inter->types, target->data.product.name);
		if (ref != NULL){
			simple_type_map relation = simple_type_map_init(inter->mem);
			for (uint64_t i = 0;i<ref->parameter_count;++i){
				simple_type_map_insert(&relation, ref->parameters[i], target->data.product.members[i]);
			}
			deep_copy_simple_type_replace_multiple(inter->mem, ref, new, &relation);
			simple_type nest_lowered;
			lower_type(inter, new, &nest_lowered);
			*new = nest_lowered;
			break;
		}
		new->data.product.members = pool_request(inter->mem, sizeof(simple_type)*target->data.product.member_count);
		for (uint64_t i = 0;i<target->data.product.member_count;++i){
			lower_type(inter, &target->data.product.members[i], &new->data.product.members[i]);
		}
		break;
	case FUNCTION_TYPE:
		new->data.function.left = pool_request(inter->mem, sizeof(simple_type));
		new->data.function.right = pool_request(inter->mem, sizeof(simple_type));
		lower_type(inter, target->data.function.left, new->data.function.left);
		lower_type(inter, target->data.function.right, new->data.function.right);
		break;
	default:
		break;
	}
}

int
main(int argc, char** argv){
	srand(time(NULL));
	pool mem = pool_alloc(POOL_SIZE, POOL_DYNAMIC);
	interpreter inter = interpreter_init(&mem);
	inter.types = simple_type_map_init(&mem);
	if (0){// example functionality
		add_to_universe(&inter, "flip", "\\x.\\y.y x");
		add_to_universe(&inter, "const", "\\x.\\y.x");
		uint64_t len = strlen("(\\x.\\y.const flip x y) TRUE FALSE");
		char* term = pool_request(&mem, len+1);
		strncpy(term, "(\\x.\\y.const flip x y) TRUE FALSE", len);
		expr* result = parse_term(term, &inter);
		show_term(result);
		printf("\n");
		uint8_t reductions = 8;
		while (reduce_step(&inter, result, MAX_REDUCTION_DEPTH) != 0 && (reductions-- > 0)){}
		rebase_term(&inter, result);
		printf("Final: ");
		show_term(result);
	 	printf("\n");
	}
	if (0){
		for (uint64_t i = 0;i<5;++i){
			generate_combinator_strike_puzzle(&inter);
		}
	}
	if (0){
		grammar_ptr_map env = grammar_ptr_map_init(&mem);
		add_to_universe(&inter, "cons", "\\x.\\y.\\z.z x y");

		uint64_t falslen = strlen("\\x.\\y.y");
		char* falsterm = pool_request(&mem, falslen+1);
		strncpy(falsterm, "\\x.\\y.y", falslen);
		expr* falsresult = parse_grammar(falsterm, &inter);

		uint64_t truelen = strlen("\\x.\\y.x");
		char* trueterm = pool_request(&mem, truelen+1);
		strncpy(trueterm, "\\x.\\y.x", truelen);
		expr* trueresult = parse_grammar(trueterm, &inter);

		grammar Bool;
		string bl = string_init(&mem, "Bool");
		type_add_alt(&mem, &env, &bl, &Bool, falsresult);
		type_add_alt(&mem, &env, &bl, &Bool, trueresult);

		uint64_t len = strlen("cons T T");
		char* term = pool_request(&mem, len+1);
		strncpy(term, "cons T T", len);
		expr* result = parse_grammar(term, &inter);
		uint8_t reductions = 128;
		while (reduce_step(&inter, result, MAX_REDUCTION_DEPTH) != 0 && (reductions-- > 0)){}
		term_flatten(&inter, result);
		grammar Pair;
		string pr = string_init(&mem, "Pair");
		Pair.param_count = 1;
		Pair.params = pool_request(&mem, sizeof(string));
		Pair.params[0] = string_init(&mem, "T");
		type_add_alt(&mem, &env, &pr, &Pair, result);

		uint64_t slen = strlen("\\x.x (\\a.\\b.a) (\\a.\\b.b)");
		char* sterm = pool_request(&mem, slen+1);
		strncpy(sterm, "\\x.x (\\a.\\b.a) (\\a.\\b.b)", slen);
		expr* sresult = parse_term(sterm, &inter);
		rebase_term(&inter, sresult);
		show_term(sresult);
		printf("\n");
		show_grammar(&Pair);
		printf("\n");

		string* params = pool_request(&mem, sizeof(string));
		params[0] = string_init(&mem, "Bool");
		uint8_t res = term_matches_type(&mem, &env, sresult, &Pair, params, 1);
		printf("%u\n", res);
	}
	simple_type* maybe = parse_type_def("Maybe T = Just T | Nothing", &inter);
	parse_type_def("Either L R = Left L | Right R", &inter);
	parse_type_def("List T = Nil | Const T", &inter);
	parse_type_def("Parser F = Parser (String -> F)", &inter);

	simple_type* fmap = parse_type_use("(A -> B) -> Maybe A -> Maybe B", &inter);
	simple_type low_type;
	lower_type(&inter, fmap, &low_type);
	show_simple_type(&low_type);
	printf("\n");
	// what if the parameter is parametric? (J -> T J)
	// when building a low type
	return 0;
}
