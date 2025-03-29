#ifndef HASHMAP_H
#define HASHMAP_H

#include "pool.h"

#define MAP_SIZE 128
#define HASHMAP_ITERATE(i) for(uint8_t i = 0;i<MAP_SIZE;++i)

inline uint8_t hash_s(const char* key){
	uint32_t hash = 5381;
	int16_t c;
	while ((c=*key++)) hash = ((hash<<5)+hash)+c;
	return hash%MAP_SIZE;
}

typedef enum BUCKET_TAG {
	BUCKET_EMPTY,
	BUCKET_FULL
} BUCKET_TAG; 

#define MAP_DEF(type)\
struct type##_map_bucket;\
typedef struct type##_map_bucket{\
	BUCKET_TAG tag;\
	const char* key;\
	type* value;\
	struct type##_map_bucket* left;\
	struct type##_map_bucket* right;\
} type##_map_bucket;\
\
typedef struct type##_map{\
	pool* mem;\
	type##_map_bucket buckets[MAP_SIZE];\
} type##_map;\
\
type##_map type##_map_init(pool* const mem);\
uint8_t type##_bucket_insert(type##_map_bucket* bucket, pool* const mem, const char* const key, type* value);\
type* type##_bucket_access(type##_map_bucket* bucket, const char* const key);\
uint8_t type##_map_insert(type##_map* const m, const char* const key, type* value);\
type* type##_map_access(type##_map* const m, const char* const key);\
type* type##_map_access_by_hash(type##_map* const m, uint32_t hash, const char* const key);\
void type##_map_empty(type##_map* const m);

#define MAP_IMPL(type)\
type##_map type##_map_init(pool* const mem){\
	type##_map m = {\
		.mem=mem\
	};\
	for (size_t i = 0;i<MAP_SIZE;++i){\
		m.buckets[i] = (type##_map_bucket){\
			.tag=BUCKET_EMPTY\
		};\
	}\
	return m;\
}\
\
uint8_t type##_bucket_insert(type##_map_bucket* bucket, pool* const mem, const char* const key, type* value){\
	if (bucket->tag == BUCKET_EMPTY){\
		bucket->tag = BUCKET_FULL;\
		bucket->key = key;\
		bucket->value = value;\
		bucket->left = pool_request(mem, sizeof(type##_map_bucket));\
		bucket->right = pool_request(mem, sizeof(type##_map_bucket));\
		*bucket->left = (type##_map_bucket){ .tag=BUCKET_EMPTY };\
		*bucket->right = (type##_map_bucket){ .tag=BUCKET_EMPTY };\
		return 0;\
	}\
	int32_t cmp = strncmp(key, bucket->key, TOKEN_MAX);\
	if (cmp < 0){\
		return type##_bucket_insert(bucket->left, mem, key, value);\
	}\
	if (cmp > 0){\
		return type##_bucket_insert(bucket->right, mem, key, value);\
	}\
	bucket->value = value;\
	return 1;\
}\
\
type* type##_bucket_access(type##_map_bucket* bucket, const char* const key){\
	if (bucket->tag == BUCKET_EMPTY){\
		return NULL;\
	}\
	int32_t cmp = strncmp(key, bucket->key, TOKEN_MAX);\
	if (cmp < 0){\
		return type##_bucket_access(bucket->left, key);\
	}\
	if (cmp > 0){\
		return type##_bucket_access(bucket->right, key);\
	}\
	return bucket->value;\
}\
\
uint8_t type##_map_insert(type##_map* const m, const char* const key, type* value){\
	uint32_t hash = 5381;\
	int16_t c;\
	const char* k = key;\
	while ((c=*k++)) hash = ((hash<<5)+hash)+c;\
	hash = hash%MAP_SIZE;\
	return type##_bucket_insert(&m->buckets[hash], m->mem, key, value);\
}\
\
type* type##_map_access(type##_map* const m, const char* const key){\
	uint32_t hash = 5381;\
	int16_t c;\
	const char* k = key;\
	while ((c=*k++)) hash = ((hash<<5)+hash)+c;\
	hash = hash%MAP_SIZE;\
	return type##_bucket_access(&m->buckets[hash], key);\
}\
\
type* type##_map_access_by_hash(type##_map* const m, uint32_t hash, const char* const key){\
	hash = hash%MAP_SIZE;\
	return type##_bucket_access(&m->buckets[hash], key);\
}\
\
void type##_map_empty(type##_map* const m){\
	for (uint8_t i = 0;i<MAP_SIZE;++i){\
		m->buckets[i] = (type##_map_bucket){\
			.tag=BUCKET_EMPTY\
		};\
	}\
}

#endif
