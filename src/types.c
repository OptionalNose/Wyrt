#include "types.h"
#include "lexer.h"
#include "util.h"

#include <string.h>

void types_init(TypeContext *tc, Error *err)
{
	*tc = (TypeContext) { 0 };
	types_register(tc, (Type) {.type = TYPE_PRIMITIVE_U8}, err);
	if(*err) goto RET;
	types_register(tc, (Type) {.type = TYPE_PRIMITIVE_U16}, err);
	if(*err) goto RET;
	types_register(tc, (Type) {.type = TYPE_PRIMITIVE_U32}, err);
	if(*err) goto RET;
	types_register(tc, (Type) {.type = TYPE_PRIMITIVE_U64}, err);
	if(*err) goto RET;
	types_register(tc, (Type) {.type = TYPE_PRIMITIVE_VOID}, err);
	if(*err) goto RET;
	types_register(tc, (Type) {.type = TYPE_PRIMITIVE_S8}, err);
	if(*err) goto RET;
	types_register(tc, (Type) {.type = TYPE_PRIMITIVE_S16}, err);
	if(*err) goto RET;
	types_register(tc, (Type) {.type = TYPE_PRIMITIVE_S32}, err);
	if(*err) goto RET;
	types_register(tc, (Type) {.type = TYPE_PRIMITIVE_S64}, err);
	if(*err) goto RET;
RET:
	return;
}

static void type_clean(Type t)
{
	if(t.type == TYPE_STRUCT) {
		if(t.struct_type.member_types)
			free(t.struct_type.member_types);
		if(t.struct_type.member_name_ids)
			free(t.struct_type.member_name_ids);
	}
}

void types_clean(TypeContext const *tc)
{
	for(size_t i = 0; i < tc->count; i++) {
		type_clean(tc->types[i]);
	}
	if(tc->types) free(tc->types);
}

size_t types_register(TypeContext *tc, Type t, Error *err)
{
	size_t base = tc->count;
	tc->count += 8;
	tc->types = realloc(tc->types, tc->count * sizeof(Type));
	CHECK_MALLOC(tc->types);
	tc->types[tc->count - 8] = t;
	tc->types[tc->count - 7] = (Type) {.pointer = {.type = TYPE_POINTER_CONST, .base = base}};
	tc->types[tc->count - 6] = (Type) {.pointer = {.type = TYPE_POINTER_ABYSS, .base = base}};
	tc->types[tc->count - 5] = (Type) {.pointer = {.type = TYPE_POINTER_VAR, .base = base}};
	tc->types[tc->count - 4] = (Type) {.pointer = {.type = TYPE_ARRAY, .base = base}};
	tc->types[tc->count - 3] = (Type) {.pointer = {.type = TYPE_SLICE_CONST, .base = base}};
	tc->types[tc->count - 2] = (Type) {.pointer = {.type = TYPE_SLICE_ABYSS, .base = base}};
	tc->types[tc->count - 1] = (Type) {.pointer = {.type = TYPE_SLICE_VAR, .base = base}};

RET:
	return tc->count - 8;
}

size_t types_register_nexist(TypeContext *tc, Type t, Error *err)
{
	size_t index = SIZE_MAX;
	for(size_t i = 0; i < tc->count; i++) {
		if(types_are_equal(tc->types[i], t)) {
			index = i;
			break;
		}
	}
	if(index != SIZE_MAX) return index;

	return types_register(tc, t, err);
}

Type type_from_ast(TypeContext *tc, AstNode const *nodes, size_t i, Error *err)
{
	Type t = { 0 };
	AstNode const *node = &nodes[i];
	switch(node->type) {
	case AST_IDENT:
		do {} while(0);
		size_t id = node->ident.id;
		switch(id) {
		case 0:
			t = (Type) { .type = TYPE_PRIMITIVE_U8 };
			break;
		case 1:
			t = (Type) { .type = TYPE_PRIMITIVE_S8 };
			break;
		case 2:
			t = (Type) { .type = TYPE_PRIMITIVE_U16 };
			break;
		case 3:
			t = (Type) { .type = TYPE_PRIMITIVE_S16 };
			break;
		case 4:
			t = (Type) { .type = TYPE_PRIMITIVE_U32 };
			break;
		case 5:
			t = (Type) { .type = TYPE_PRIMITIVE_S32 };
			break;
		case 6:
			t = (Type) { .type = TYPE_PRIMITIVE_U64 };
			break;
		case 7:
			t = (Type) { .type = TYPE_PRIMITIVE_S64 };
			break;
		case 8:
			t = (Type) { .type = TYPE_PRIMITIVE_VOID };
			break;
		default: {
			size_t index = SIZE_MAX;
			for(size_t j = 0; j < tc->count; j++) {
				if(tc->types[j].type == TYPE_TYPEDEF
					&& tc->types[j].typdef.id == id
				) {
					index = j;
					break;
				}
			}
			if(index == SIZE_MAX) {
				fprintf(stderr, "Identifier is not a Type at ");
				lexer_print_debug_to_file(stderr, &node->debug.debug_info);
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
			t = tc->types[index];
		} break;
		}
		break;

	case AST_POINTER_CONST:
	case AST_POINTER_VAR:
	case AST_POINTER_ABYSS:
		do {} while(0);
		Type targ_type = type_from_ast(tc, nodes, node->pointer_type.base_type, err);
		if(*err) goto RET;
		size_t index = types_register_nexist(tc, targ_type, err);
		if(*err) goto RET;

		TypeType ptr_type; 
		switch(node->type) {
		default:
		case AST_POINTER_CONST:
			ptr_type = TYPE_POINTER_CONST;
			break;
		case AST_POINTER_ABYSS:
			ptr_type = TYPE_POINTER_ABYSS;
			break;
		case AST_POINTER_VAR:
			ptr_type = TYPE_POINTER_VAR;
			break;
		}

		t = (Type) {
			.pointer = {
				.type = ptr_type,
				.base = index,
			},
		};
		break;

	case AST_ARRAY:
		do {} while(0);
		Type base_type = type_from_ast(tc, nodes, node->array.elem_type, err);
		if(*err) goto RET;
		index = types_register_nexist(tc, base_type, err);
		if(*err) goto RET;

		t = (Type) {
			.array = {
				.type = TYPE_ARRAY,
				.base = index,
				.len = node->array.len,
			},
		};
		break;

	case AST_SLICE_CONST:
	case AST_SLICE_VAR:
	case AST_SLICE_ABYSS:
		do {} while(0);
		Type slice_type = type_from_ast(tc, nodes, node->pointer_type.base_type, err);
		if(*err) goto RET;
		index = types_register_nexist(tc, slice_type, err);
		if(*err) goto RET;

		TypeType slice_access; 
		switch(node->type) {
		default:
		case AST_SLICE_CONST:
			ptr_type = TYPE_SLICE_CONST;
			break;
		case AST_SLICE_ABYSS:
			ptr_type = TYPE_SLICE_ABYSS;
			break;
		case AST_SLICE_VAR:
			ptr_type = TYPE_SLICE_VAR;
			break;
		}

		t = (Type) {
			.slice = {
				.type = ptr_type,
				.base = index,
			},
		};
		break;

	case AST_STRUCT_TYPE:
		do {} while(0);
		t.type = TYPE_STRUCT;
		t.struct_type.member_count = node->struct_type.member_count;
		t.struct_type.member_types = malloc(t.struct_type.member_count * sizeof(size_t));
		CHECK_MALLOC(t.struct_type.member_types);
		t.struct_type.member_name_ids = malloc(t.struct_type.member_count * sizeof(size_t));
		CHECK_MALLOC(t.struct_type.member_name_ids);

		for(size_t i = 0; i < node->struct_type.member_count; i++) {
			Type member_type = type_from_ast(tc, nodes, node->struct_type.member_types[i], err);
			if(*err) goto RET;	

			index = types_register_nexist(tc, member_type, err);
			if(*err) goto RET;

			t.struct_type.member_types[i] = index;
			t.struct_type.member_name_ids[i] = node->struct_type
				.member_name_ids[i];
		}

		break;

	default:
		fprintf(stderr, "Invalid Type at ");
		lexer_print_debug_to_file(stderr, &node->debug.debug_info);
		fprintf(stderr, "\n");
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

RET:
	do {} while(0);
	size_t exist_limit = tc->count;
	size_t index = types_register_nexist(tc, t, err);
	if(index < exist_limit) {
		type_clean(t);
		t = tc->types[index];
	}
	return t;
}


bool types_are_equal(Type a, Type b)
{
	if(a.type != b.type) return false;

	switch(a.type) {
	case TYPE_NONE:
	case TYPE_PRIMITIVE_U8:
	case TYPE_PRIMITIVE_U16:
	case TYPE_PRIMITIVE_U32:
	case TYPE_PRIMITIVE_U64:
	case TYPE_PRIMITIVE_S8:
	case TYPE_PRIMITIVE_S16:
	case TYPE_PRIMITIVE_S32:
	case TYPE_PRIMITIVE_S64:
	case TYPE_PRIMITIVE_VOID:
		return true;
	case TYPE_POINTER_CONST:
	case TYPE_POINTER_ABYSS:
	case TYPE_POINTER_VAR:
	case TYPE_SLICE_CONST:
	case TYPE_SLICE_ABYSS:
	case TYPE_SLICE_VAR:
		if(a.pointer.base == b.pointer.base) return true;
		else return false;
	case TYPE_ARRAY:
		if(a.array.base == b.array.base && a.array.len == b.array.len)
			return true;
		else return false;
	case TYPE_STRUCT:
		if(a.struct_type.member_count != b.struct_type.member_count)
			return false;

		for(size_t i = 0; i < a.struct_type.member_count; i++) {
			if(a.struct_type.member_types[i]
				!= b.struct_type.member_types[i]
			) {
				return false;
			}
			if(a.struct_type.member_name_ids[i]
				!= b.struct_type.member_name_ids[i]
			) {
				return false;
			}
		}
		return true;

	case TYPE_TYPEDEF:
		if(a.typdef.id == b.typdef.id) return true;
		return false;
	}
}

bool types_are_compatible(TypeContext const *tc, Type a, Type b)
{
	while(b.type == TYPE_TYPEDEF) {
		b = tc->types[b.typdef.backing];
	}

	switch(a.type) {
	case TYPE_NONE:
	case TYPE_PRIMITIVE_VOID:
		return (a.type == b.type);
	case TYPE_PRIMITIVE_U8:
		if(b.type >= TYPE_PRIMITIVE_U8 && b.type <= TYPE_PRIMITIVE_U64)
			return true;
		else if(b.type >= TYPE_PRIMITIVE_S16 && b.type <= TYPE_PRIMITIVE_S64)
			return true;
		else return false;
	case TYPE_PRIMITIVE_U16:
		if(b.type >= TYPE_PRIMITIVE_U16 && b.type <= TYPE_PRIMITIVE_U64)
			return true;
		else if(b.type >= TYPE_PRIMITIVE_S32 && b.type <= TYPE_PRIMITIVE_S64)
			return true;
		else return false;
	case TYPE_PRIMITIVE_U32:
		if(b.type >= TYPE_PRIMITIVE_U32 && b.type <= TYPE_PRIMITIVE_U64)
			return true;
		else if(b.type == TYPE_PRIMITIVE_S64)
			return true;
		else return false;
	case TYPE_PRIMITIVE_U64:
		if(b.type == TYPE_PRIMITIVE_U64) return true;
		else return false;
	case TYPE_PRIMITIVE_S8:
		if(b.type >= TYPE_PRIMITIVE_S8 && b.type <= TYPE_PRIMITIVE_S64)
			return true;
		else return false;
	case TYPE_PRIMITIVE_S16:
		if(b.type >= TYPE_PRIMITIVE_S16 && b.type <= TYPE_PRIMITIVE_S64)
			return true;
		else return false;
	case TYPE_PRIMITIVE_S32:
		if(b.type >= TYPE_PRIMITIVE_S32 && b.type <= TYPE_PRIMITIVE_S64)
			return true;
		else return false;
	case TYPE_PRIMITIVE_S64:
		if(b.type == TYPE_PRIMITIVE_S64)
			return true;
		else return false;
	case TYPE_POINTER_CONST:
		if(b.type == TYPE_SLICE_CONST) return true;
		return types_are_equal(a, b);
		break;
	case TYPE_POINTER_ABYSS:
		if(b.type == TYPE_SLICE_ABYSS) return true;
		return types_are_equal(a, b);
		break;
	case TYPE_SLICE_CONST:
	case TYPE_SLICE_ABYSS:
		return types_are_equal(a, b);
	case TYPE_POINTER_VAR:
		if(b.type >= TYPE_POINTER_CONST && b.type <= TYPE_SLICE_VAR) {
			a.type = b.type;
			return types_are_equal(a, b);
		} else return false;
		break;
	case TYPE_SLICE_VAR:
		if(b.type >= TYPE_SLICE_CONST && b.type <= TYPE_SLICE_VAR) {
			a.type = b.type;
			return types_are_equal(a, b);
		} else {
			return false;
		}
	case TYPE_ARRAY:
		if(b.type != TYPE_ARRAY) return false;
		if(a.array.base != b.array.base) return false;
		if(a.array.len != b.array.len) return false;
		return true;
	case TYPE_STRUCT:
		if(a.struct_type.member_count != b.struct_type.member_count)
			return false;

		for(size_t i = 0; i < a.struct_type.member_count; i++) {
			if(a.struct_type.member_types[i]
				!= b.struct_type.member_types[i]
			) {
				return false;
			}
		}
		return true;
	case TYPE_TYPEDEF:
		return types_are_equal(tc->types[a.typdef.backing], b);
		break;
	}
}

void type_print(FILE *file, TypeContext const *tc, Type t, char *const *identifiers)
{
	switch(t.type) {
	case TYPE_NONE:
		fprintf(file, "NONE");
		break;
	case TYPE_PRIMITIVE_U8:
		fprintf(file, "u8");
		break;
	case TYPE_PRIMITIVE_U16:
		fprintf(file, "u16");
		break;
	case TYPE_PRIMITIVE_U32:
		fprintf(file, "u32");
		break;
	case TYPE_PRIMITIVE_U64:
		fprintf(file, "u64");
		break;
	case TYPE_PRIMITIVE_S8:
		fprintf(file, "s8");
		break;
	case TYPE_PRIMITIVE_S16:
		fprintf(file, "s16");
		break;
	case TYPE_PRIMITIVE_S32:
		fprintf(file, "s32");
		break;
	case TYPE_PRIMITIVE_S64:
		fprintf(file, "s64");
		break;
	case TYPE_PRIMITIVE_VOID:
		fprintf(file, "void");
		break;
	case TYPE_POINTER_CONST:
		fprintf(file, "&const ");
		type_print(file, tc, tc->types[t.pointer.base], identifiers);
		break;
	case TYPE_POINTER_ABYSS:
		fprintf(file, "&abyss ");
		type_print(file, tc, tc->types[t.pointer.base], identifiers);
		break;
	case TYPE_POINTER_VAR:
		fprintf(file, "&var ");
		type_print(file, tc, tc->types[t.pointer.base], identifiers);
		break;
	case TYPE_ARRAY:
		fprintf(file, "[%zi]", t.array.len);
		type_print(file, tc, tc->types[t.array.base], identifiers);
		break;
	case TYPE_SLICE_CONST:
		fprintf(file, "[]const ");
		type_print(file, tc, tc->types[t.slice.base], identifiers);
		break;
	case TYPE_SLICE_ABYSS:
		fprintf(file, "[]abyss ");
		type_print(file, tc, tc->types[t.slice.base], identifiers);
		break;
	case TYPE_SLICE_VAR:
		fprintf(file, "[]var ");
		type_print(file, tc, tc->types[t.slice.base], identifiers);
		break;
	case TYPE_STRUCT:
		fprintf(file, "struct{");
		for(size_t i = 0; i < t.struct_type.member_count; i++) {
			type_print(
				file,
				tc,
				tc->types[t.struct_type.member_types[i]],
				identifiers
			);
		}
		break;
	case TYPE_TYPEDEF:
		fprintf(file, "\"%s\"", identifiers[t.typdef.id]);
	}
}

bool type_is_arithmetic(Type t)
{
	switch(t.type) {
	case TYPE_PRIMITIVE_U8:
	case TYPE_PRIMITIVE_U16:
	case TYPE_PRIMITIVE_U32:
	case TYPE_PRIMITIVE_U64:
		return true;
	default:
		return false;
	}
}

bool type_is_unsigned(Type t)
{
	switch(t.type) {
	case TYPE_PRIMITIVE_U8:
	case TYPE_PRIMITIVE_U16:
	case TYPE_PRIMITIVE_U32:
	case TYPE_PRIMITIVE_U64:
		return true;
	default:
		return false;
	}
}


void types_copy(
	TypeContext *restrict dst,
	TypeContext const *restrict src,
	Error *err
)
{
	dst->types = malloc(src->count * sizeof(Type));
	CHECK_MALLOC(dst->types);
	dst->count = src->count;
	for(size_t i = 0; i < src->count; i++) {
		if(src->types[i].type == TYPE_STRUCT) {
			dst->types[i].struct_type.member_types = malloc(
				src->types[i].struct_type.member_count * sizeof(size_t)
			);
			CHECK_MALLOC(dst->types[i].struct_type.member_types);
			dst->types[i].struct_type.member_name_ids = malloc(
				src->types[i].struct_type.member_count * sizeof(size_t)
			);
			CHECK_MALLOC(dst->types[i].struct_type.member_name_ids);

			dst->types[i].struct_type.member_count =
				src->types[i].struct_type.member_count;

			memcpy(
				dst->types[i].struct_type.member_name_ids,
				src->types[i].struct_type.member_name_ids,
				dst->types[i].struct_type.member_count * sizeof(size_t)
			);

			memcpy(
				dst->types[i].struct_type.member_types,
				src->types[i].struct_type.member_types,
				dst->types[i].struct_type.member_count * sizeof(size_t)
			);
			dst->types[i].type = TYPE_STRUCT;
		} else {
			dst->types[i] = src->types[i];
		}
	}

RET:
	return;
}

Type type_resolve(TypeContext const *tc, Type t)
{
	while(t.type == TYPE_TYPEDEF) {
		t = tc->types[t.typdef.backing];
	}

	return t;
}
