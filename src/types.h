#pragma once

#include "parser.h"

typedef enum {
	TYPE_PRIMITIVE_U8,
	TYPE_PRIMITIVE_U16,
	TYPE_PRIMITIVE_U32,
	TYPE_PRIMITIVE_U64,
	TYPE_PRIMITIVE_VOID,
	
	TYPE_POINTER_CONST,
	TYPE_POINTER_ABYSS,
	TYPE_POINTER_VAR,

	TYPE_ARRAY,
	TYPE_SLICE_CONST,
	TYPE_SLICE_ABYSS,
	TYPE_SLICE_VAR,
} TypeType;

typedef union {
	TypeType type;

	struct {
		TypeType type;
		size_t base; // index into types
	} pointer;

	struct {
		TypeType type;
		size_t base;
		size_t len;
	} array;

	struct {
		TypeType type;
		size_t base;
	} slice;
} Type;

typedef struct {
	Type *types;
	size_t count;
} TypeContext;

void types_init(TypeContext *tc, Error *err);
void types_clean(TypeContext const *tc);

void types_register(TypeContext *tc, Type t, Error *err);
size_t types_get_size(TypeContext const *tc, Type t);
Type type_from_ast(TypeContext *tc, AstNode const *nodes, size_t i, Error *err);
bool types_are_equal(Type a, Type b);
void type_print(FILE *file, TypeContext const *tc, Type t);
bool type_is_arithmetic(Type t);
