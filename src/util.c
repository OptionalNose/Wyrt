#include "util.h"

#include <string.h>
#include <stdarg.h>

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
		return &((char*)da->data)[da->elem_size * index];
	else return NULL;
}


void *dynarr_from_back(DynArr *da, size_t from_back)
{
	if(da->data)
		return &((char*)da->data)[da->elem_size * (da->count - from_back - 1)];
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


void string_builder_append(StringBuilder *sb, const char *str, Error *err)
{
	DynArr raw = {
		.data = sb->str,
		.elem_size = sizeof(char),
		.count = sb->count > 0 ? sb->count - 1 : 0,
		.capacity = sb->capacity
	};

	char c;
	while((c = *(str++)) != 0) {
		dynarr_push(&raw, &c, err);
		if(*err) goto RET;
	}
	dynarr_push(&raw, &(char){'\0'}, err);
	if(*err) goto RET;

	*sb = (StringBuilder) {
		.str = raw.data,
		.count = raw.count,
		.capacity = raw.count,
	};
	
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

	char *formatted = malloc(len + 1);
	formatted[len] = '\0';
	CHECK_MALLOC(formatted);

	fseek(tmp, 0, SEEK_SET);

	fread(formatted, len, 1, tmp);

	string_builder_append(sb, formatted, err);
	if(*err) goto RET;

RET:
	free(formatted);
	fclose(tmp);
}

void string_builder_append_va(
	StringBuilder *sb,
	Error *err,
	...
)
{
	va_list args;
	va_start(args, err);

	const char *str = NULL;
	while((str = va_arg(args, const char *)) != NULL) {
		string_builder_append(sb, str, err);
		if(*err) goto RET;
	}

RET:
	va_end(args);
}
