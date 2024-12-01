#include "util.h"

#include <string.h>
#include <stdarg.h>

void dynarr_init(DynArr *da, size_t elem_size)
{
	*da = (DynArr) {
		.elem_size = elem_size
	};
}
void dynarr_clean(DynArr const *da)
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

void *dynarr_at(DynArr const *da, size_t index)
{
	if(da->data)
		return &((char*)da->data)[da->elem_size * index];
	else return NULL;
}


void *dynarr_from_back(DynArr const *da, size_t from_back)
{
	if(da->data && da->count)
		return &((char*)da->data)[da->elem_size * (da->count - from_back - 1)];
	else return NULL;
}

void dynarr_push(DynArr *da, void const *val, Error *err)
{
	dynarr_alloc(da, 1, err);
	if(*err) goto RET;

	memcpy(dynarr_from_back(da, 0), val, da->elem_size);
	
RET:
	return;	
}
void *dynarr_pop(DynArr *da)
{
	void *top = dynarr_from_back(da, 0);
	da->count--;
	return top;
}

void dynarr_append(DynArr *da, void const *vals, size_t count, Error *err)
{
	dynarr_alloc(da, count, err);
	if(*err) goto RET;

	for(size_t i = count - 1; i >= 0; i--) {
		memcpy(dynarr_from_back(da, i), &vals[i], da->elem_size);
	}

RET:
	return;
}

void string_builder_append(StringBuilder *sb, const char *str, Error *err)
{
	size_t len = strlen(str);

	sb->count += len;

	if(sb->count >= sb->capacity) {
		sb->str = realloc(sb->str, sb->count * 2);
		CHECK_MALLOC(sb->str);
	}

	strcpy(sb->str + sb->count - len, str);
	
RET:
	return;
}

void string_builder_printf(
	StringBuilder *sb,
	Error *err,
	const char *fmt_str,
	...
)
{
	va_list args;
	va_start(args, fmt_str);

	FILE *tmp = tmpfile();

	if(!tmp) {
		fprintf(stderr, "[ERROR] UNABLE TO CREATE TEMPORARY FILE\n");
		*err = ERROR_IO;
		goto RET;
	}

	if(vfprintf(tmp, fmt_str, args) < 0) {
		fprintf(stderr, "[ERROR] UNABLE TO WRITE TO TEMPORARY FILE\n");
		*err = ERROR_IO;
		goto RET;
	}
	
	long len = ftell(tmp);

	bool empty = !sb->count;

	if(!empty) {
		sb->count += len;
	} else {
		sb->count += len + 1;
	}
	

	if(sb->count >= sb->capacity) {
		sb->str = realloc(sb->str, sb->count * 2);
		CHECK_MALLOC(sb->str);
	}

	fseek(tmp, 0, SEEK_SET);

	
	if(empty) {
		if(!fread(sb->str, len, 1, tmp)) {
			fprintf(stderr, "[ERROR] UNABLE TO READ FROM TEMPORARY FILE\n");
			*err = ERROR_IO;
			goto RET;
		}
	} else {
		if(!fread(sb->str + sb->count - len - 1, len, 1, tmp)) {
			fprintf(stderr, "[ERROR] UNABLE TO READ FROM TEMPORARY FILE\n");
			*err = ERROR_IO;
			goto RET;
		}
	}
	
	sb->str[sb->count - 1] = '\0';

RET:
	fclose(tmp);
}
