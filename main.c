#include <stdio.h>
#include <string.h>
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
		new->data.bind.expression = deep_copy(inter, map, target->data.bind.expression);
		string_map_insert(map, target->data.bind.name.str, &new->data.bind.name);
		return new;
	case APPL_EXPR:
		new->data.appl.left = deep_copy(inter, map, target->data.appl.left);
		new->data.appl.right = deep_copy(inter, map, target->data.appl.right);
		return new;
	case NAME_EXPR:
		string* access = string_map_access(map, target->data.name.str);
		if (access == NULL){
			return NULL;
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
	uint64_t target_len = left->data.bind.name.len;
	expr* stack[APPLICATION_STACK_LIMIT];
	uint64_t sp = 0;
	expr* new = deep_copy(inter, NULL, left);
	stack[sp++] = new;
	while (sp > 0){
		expr* current = stack[sp-1];
		switch (current->tag){
		case BIND_EXPR:
			stack[sp-1] = current->data.bind.expression;
			continue;
		case APPL_EXPR:
			stack[sp-1] = current->data.appl.left;
			stack[sp++] = current->data.appl.right;
			continue;
		case NAME_EXPR:
			if (strncmp(target, current->data.name.str, target_len) == 0){
				expr* copy = deep_copy(inter, NULL, current);
				*current = *copy;
			}
			sp -= 1;
			continue;
		}
	}
	return new;
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

int main(int argc, char** argv){
	pool mem = pool_alloc(POOL_SIZE, POOL_STATIC);
	interpreter inter = {
		.mem = &mem,
		.next.str = pool_request(&mem, NAME_MAX)
	};
	inter.next.str[0] = 'a';
	inter.next.str[1] = '\0';
	inter.next.len = 1;
	return 0;
}
