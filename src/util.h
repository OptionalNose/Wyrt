#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>


typedef enum {
	ERROR_OK,
	ERROR_OUT_OF_MEMORY,
	ERROR_NOT_FOUND,
	ERROR_IO,
	ERROR_UNEXPECTED_DATA,
	ERROR_TODO,
	ERROR_UNDEFINED,
	ERROR_INTERNAL,
} Error;

#ifdef NDEBUG
#define CHECK_MALLOC(ptr) \
	do { \
		if(!ptr) { \
			fprintf(stderr, "Not Enough Memory!\n"); \
			*err = ERROR_OUT_OF_MEMORY; \
			goto RET; \
		} \
	} while (0)
#else
#define CHECK_MALLOC(ptr) \
	do { \
		if(!ptr) { \
			fprintf(stderr, "Not Enough Memory! (%s, %i)\n", __FILE__, __LINE__); \
			*err = ERROR_OUT_OF_MEMORY; \
			goto RET; \
		} \
	} while (0)
#endif

typedef struct {
	void *data;
	size_t elem_size;
	size_t count;
	size_t capacity;
} DynArr;

void dynarr_init(DynArr *da, size_t elem_size);
void dynarr_clean(DynArr const *da);

void dynarr_alloc(DynArr *da, size_t count, Error *e);
void dynarr_reserve(DynArr *da, size_t count, Error *e);
void dynarr_shrink(DynArr *da, Error *e);
void *dynarr_at(DynArr const *da, size_t index);
void *dynarr_from_back(DynArr const *da, size_t from_back);
void dynarr_push(DynArr *da, void const *val, Error *e);
void *dynarr_pop(DynArr *da);
void dynarr_append(DynArr *da, void const *vals, size_t count, Error *err);

typedef struct {
	char *str;
	size_t count;
	size_t capacity;
} StringBuilder;

void string_builder_append(StringBuilder *sb, const char *str, Error *err);

void string_builder_printf(
	StringBuilder *sb,
	Error *err,
	const char *fmt_str,
	...
);
