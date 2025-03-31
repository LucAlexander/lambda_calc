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
reduce_step(interpreter* const inter, expr* const expression){
	switch (expression->tag){
	case BIND_EXPR:
		return reduce_step(inter, expression->data.bind.expression);
	case APPL_EXPR:
		if (reduce_step(inter, expression->data.appl.left) == 0){
			if (reduce_step(inter, expression->data.appl.right) == 0){
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
		printf("%s", ex->data.bind.name.str);
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
	srand(time(NULL));
	string* assoc = pool_request(inter->mem, sizeof(string)*depth);
	return generate_term_internal(inter, assoc, 0, 0, depth);
}

int main(int argc, char** argv){
	pool mem = pool_alloc(POOL_SIZE, POOL_STATIC);
	interpreter inter = {
		.mem = &mem,
		.next.str = pool_request(&mem, NAME_MAX)
	};
	inter.next.str[0] = 'a';
	inter.next.str[1] = '\0';
	inter.next.len = 1;
	{
		expr* t = pool_request(&mem, sizeof(expr));
		t->tag = BIND_EXPR;
		t->data.bind.name = next_string(&inter);
		expr* inner = pool_request(&mem, sizeof(expr));
		inner->tag = BIND_EXPR;
		inner->data.bind.name = next_string(&inter);
		t->data.bind.expression = inner;
		expr* result = pool_request(&mem, sizeof(expr));
		inner->data.bind.expression = result;
		result->tag = NAME_EXPR;
		result->data.name = t->data.bind.name;
		show_term(t);
		printf("\n");
		expr* iden = pool_request(&mem, sizeof(expr));
		iden->tag = BIND_EXPR;
		iden->data.bind.name = next_string(&inter);
		expr* var = pool_request(&mem, sizeof(expr));
		iden->data.bind.expression = var;
		var->tag = NAME_EXPR;
		var->data.name = iden->data.bind.name;
		show_term(iden);
		printf("\n");
		expr* new = apply_term(&inter, t, iden);
		show_term(new);
		printf("\n");
		expr* new2 = apply_term(&inter, iden, t);
		show_term(new2);
		printf("\n");
		expr* application = pool_request(&mem, sizeof(expr));
		application->tag = APPL_EXPR;
		application->data.appl.left = new;
		application->data.appl.right = new2;
		expr* outer = pool_request(&mem, sizeof(expr));
		outer->tag = APPL_EXPR;
		outer->data.appl.left = application;
		outer->data.appl.right = t;
		show_term(outer);
		printf("\n");
		while (reduce_step(&inter, outer) != 0){
			printf(" -> ");
			show_term(outer);
			printf("\n");
		}
		expr* generated = generate_term(&inter, 6);
		show_term(generated);
		printf("\n");
		while (reduce_step(&inter, generated) != 0){
			printf(" -> ");
			show_term(generated);
			printf("\n");
		}
	}
	return 0;
}
