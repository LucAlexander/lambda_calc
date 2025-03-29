#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#include "pool.h"

pool pool_alloc(uint64_t cap, POOL_TAG t){
	void* mem = malloc(cap);
	if (mem == NULL || t == NO_POOL){
		return (pool){.tag=NO_POOL};
	}
	return (pool){
		.tag = t,
		.buffer = mem,
		.ptr = mem,
		.left = cap,
		.next = NULL
	};
}

void pool_empty(pool* const p){
	p->left += p->ptr - p->buffer;
	p->ptr = p->buffer;
	if (p->next != NULL){
		pool_empty(p->next);
	}
}

void pool_dealloc(pool* const p){
	free(p->buffer);
	if (p->next != NULL){
			pool_dealloc(p->next);
	}
	free(p->next);
}

void* pool_request(pool* const p, uint64_t bytes){
	if (p->left < bytes){
		uint64_t capacity = p->left + (p->ptr-p->buffer);
		if (p->tag == POOL_STATIC || bytes > capacity){
			return NULL;
		}
		if (p->next == NULL){
			p->next = malloc(sizeof(pool));
			*p->next = pool_alloc(p->left + (p->ptr-p->buffer), POOL_DYNAMIC);
		}
		return pool_request(p->next, bytes);
	}
	p->left -= bytes;
	void* addr = p->ptr;
	p->ptr += bytes;
	return addr;
}

void* pool_byte(pool* const p){	
	if (p->left <= 0 || p->tag == POOL_DYNAMIC){
		return NULL;
	}
	p->left -= 1;
	void* addr = p->ptr;
	p->ptr += 1;
	return addr;
}

void pool_save(pool* const p){
	if (p->next != NULL){
		pool_save(p->next);
		return;
	}
	p->ptr_save = p->ptr;
	p->left_save = p->left;
}

void pool_load(pool* const p){
	if (p->next != NULL){
		pool_load(p);
		return;
	}
	p->ptr = p->ptr_save;
	p->left = p->left_save;
}
