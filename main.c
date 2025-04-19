#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <ctype.h>

#include "calc.h"

MAP_IMPL(string)
MAP_IMPL(TOKEN)
MAP_IMPL(expr)

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
	if (left->tag != BIND_EXPR){
		return NULL;
	}
	char* target = left->data.bind.name.str;
	string_map new_map = string_map_init(inter->mem);
	expr* new = deep_copy_replace(inter, &new_map, left->data.bind.expression, target, right);
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
				if (expression->data.appl.left->tag != BIND_EXPR){
					return 0;
				}
				expr* new = apply_term(inter, expression->data.appl.left, expression->data.appl.right);
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
show_term_helper(expr* const ex, expr* const left){
	switch (ex->tag){
	case BIND_EXPR:
		printf("\\%s.", ex->data.bind.name.str);
		show_term(ex->data.bind.expression);
		return;
	case APPL_EXPR:
		if (left == NULL && ex->data.appl.right->tag != BIND_EXPR){
			show_term(ex->data.appl.left);
			printf(" ");
			show_term_helper(ex->data.appl.right, ex->data.appl.left);
			return;
		}
		printf("(");
		show_term(ex->data.appl.left);
		printf(" ");
		show_term_helper(ex->data.appl.right, ex->data.appl.left);
		printf(")");
		return;
	case NAME_EXPR:
		printf("%s", ex->data.name.str);
		return;
	}
}

void
show_term(expr* const ex){
	show_term_helper(ex, NULL);
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
		uint8_t b = term_depth(expression->data.appl.left);
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
		if (!isalnum(c) && c != '_'){
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
		else if (isalpha(c) || c == '_'){
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
		string* name = string_map_access(parse->names, t->data.name.str);
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
	TOKEN_map_insert(map, "S", tokens++);
	TOKEN_map_insert(map, "K", tokens++);
	TOKEN_map_insert(map, "I", tokens++);
	TOKEN_map_insert(map, "B", tokens++);
	TOKEN_map_insert(map, "C", tokens++);
	TOKEN_map_insert(map, "W", tokens++);
	TOKEN_map_insert(map, "A", tokens++);
	TOKEN_map_insert(map, "T", tokens++);
	TOKEN_map_insert(map, "M", tokens++);
	TOKEN_map_insert(map, "AND", tokens++);
	TOKEN_map_insert(map, "OR", tokens++);
	TOKEN_map_insert(map, "NOT", tokens++);
	TOKEN_map_insert(map, "SUCC", tokens++);
	TOKEN_map_insert(map, "ADD", tokens++);
	TOKEN_map_insert(map, "MUL", tokens++);
	TOKEN_map_insert(map, "EXP", tokens++);
	TOKEN_map_insert(map, "TRUE", tokens++);
	TOKEN_map_insert(map, "FALSE", tokens++);
	TOKEN_map_insert(map, "CONS", tokens++);
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
		.token_index = 0
	};
	lex_cstr(&parse, cstr);
	show_tokens(&parse);
	expr* term = parse_term_recursive(&parse, 0);
	printf("Parsed term: ");
	show_term(term);
	printf("\n");
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

void
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
	add_to_universe(inter, "compose", "\\x.\\y.\\z.x (y z)");
	add_to_universe(inter, "s", "S");
	add_to_universe(inter, "if", "\\x.\\y.\\z.x y z");
	char* items[] = {
		"flip", "const", "cons", "id", "and", "or", "not", "true", "false",
		"succ", "add", "mul", "exp", "0", "1", "2", "compose", "s", "if"
	};
	uint64_t count = 19;
	uint64_t max_width = 5;
	uint64_t min_width = 3;
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
	rebase_term(inter, f);
	show_term(f);
}

int
main(int argc, char** argv){
	srand(time(NULL));
	pool mem = pool_alloc(POOL_SIZE, POOL_DYNAMIC);
	interpreter inter = interpreter_init(&mem);
	{// example functionality
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
	return 0;
}
