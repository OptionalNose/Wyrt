#include "util.h"

#include <string.h>

void dynarr_init(DynArr *da, size_t elem_size)
{
	*da = (DynArr) {
		.elem_size = elem_size
	};
}
void dynarr_clean(DynArr *da)
{
	free(da->data);
}

void dynarr_alloc(DynArr *da, size_t count, Error *err)
{
	da->count += count;
	if(da->count >= da->capacity) {
		da->capacity = da->count * 2;
		da->data = realloc(da->data, da->capacity * da->elem_size);
		CHECK_MALLOC(da->data);
	}
RET:
	return;
}

void dynarr_shrink(DynArr *da, Error *err)
{
	da->capacity = da->count;
	da->data = realloc(da->data, da->capacity * da->elem_size);
	CHECK_MALLOC(da->data);
RET:
	return;
}

void *dynarr_at(DynArr *da, size_t index)
{
	if(da->data)
		return &da->data[da->elem_size * index];
	else return NULL;
}


void *dynarr_from_back(DynArr *da, size_t from_back)
{
	if(da->data)
		return &da->data[da->elem_size * (da->count - from_back - 1)];
	else return NULL;
}

void dynarr_push(DynArr *da, void *val, Error *err)
{
	dynarr_alloc(da, 1, err);
	if(*err) goto RET;

	memcpy(dynarr_from_back(da, 0), val, da->elem_size);
	
RET:
	return;	
}
void dynarr_pop(DynArr *da, void *val)
{
	memcpy(val, dynarr_from_back(da, 0), da->elem_size);
	da->count--;
}
