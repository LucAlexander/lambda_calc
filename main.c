#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "calc.h"

MAP_IMPL(string)

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
show_term(expr* const ex){
	switch (ex->tag){
	case BIND_EXPR:
		printf("\\%s.", ex->data.bind.name.str);
		show_term(ex->data.bind.expression);
		return;
	case APPL_EXPR:
		printf("(");
		show_term(ex->data.appl.left);
		printf(" ");
		show_term(ex->data.appl.right);
		printf(")");
		return;
	case NAME_EXPR:
		printf("%s", ex->data.name.str);
		return;
	}
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
		.next.str = pool_request(mem, NAME_MAX)
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

int
main(int argc, char** argv){
	srand(time(NULL));
	//test_fuzz_puzzle();
	return 0;
}
