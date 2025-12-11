#include "../backend.h"

#include <stdio.h>
#include <libgccjit.h>

struct WyrtContext {
	gcc_jit_context *gcc;
};

WyrtContext *get_ctx(Error *err)
{
	WyrtContext *ctx = malloc(sizeof *ctx);
	CHECK_MALLOC(ctx);

	ctx->gcc = gcc_jit_context_acquire();
	if(!ctx->gcc) {
		fprintf(stderr, "Could not create GCC Context!\n");
		*err = ERROR_NOT_FOUND;
		goto RET;
	}

RET:
	return ctx;	
}

void compile(WyrtContext *ctx, GenOptions options, const char *path, Error *err)
{
	int type = options & 3;
	int dbg = options & 4;
	int opt = options & 8;

	if(dbg) {
		gcc_jit_context_set_bool_option(ctx->gcc, GCC_JIT_BOOL_OPTION_DEBUGINFO, 1);
	}
	if(opt) {
		gcc_jit_context_set_int_option(ctx->gcc, GCC_JIT_INT_OPTION_OPTIMIZATION_LEVEL, 3);
	}

	enum gcc_jit_output_kind format;
	switch(type) {
	case GEN_EXE: format = GCC_JIT_OUTPUT_KIND_EXECUTABLE; break;
	case GEN_OBJ: format = GCC_JIT_OUTPUT_KIND_OBJECT_FILE; break;
	case GEN_ASM: format = GCC_JIT_OUTPUT_KIND_ASSEMBLER; break;
	case GEN_SHR: format = GCC_JIT_OUTPUT_KIND_DYNAMIC_LIBRARY; break;
	}

	gcc_jit_context_compile_to_file(ctx->gcc, format, path);

	return;
}

void release_ctx(WyrtContext *ctx)
{
	gcc_jit_context_release(ctx->gcc);
}

static gcc_jit_type *get_named_type(CodeGen *cg, Type type, TypeContext *tc, Error *err)
{
	gcc_jit_type *ret = NULL;
	for(size_t i = 0; i < cg->named_type_count; i++) {
		if(types_are_equal(cg->named_types[i], type)) {
			ret = cg->named_types_gcc[i];
			goto RET;
		}
	}

	char *arr_name = malloc(16);
	CHECK_MALLOC(arr_name);

	memcpy(arr_name, "_.0000000000000", 16);

	int pos = 2;
	for(size_t num = cg->named_type_count; num > 0; pos++, num /= 27) {
		arr_name[pos] = 'A' + num % 27 - 1;
	}
	cg->named_type_count += 1;
	cg->named_types = realloc(
		cg->named_types,
		sizeof(*cg->named_types) * cg->named_type_count
	);
	CHECK_MALLOC(cg->named_types);
	cg->named_types_gcc = realloc(
		cg->named_types_gcc,
		sizeof(*cg->named_types_gcc) * cg->named_type_count
	);	
	CHECK_MALLOC(cg->named_types_gcc);
	cg->named_type_names = realloc(
		cg->named_type_names,
		sizeof(*cg->named_type_names) * cg->named_type_count
	);
	CHECK_MALLOC(cg->named_type_names);

	cg->named_type_names[cg->named_type_count - 1] = arr_name;
	cg->named_types[cg->named_type_count - 1] = type;

RET:
	return ret;
}

static gcc_jit_type *gen_type(
	CodeGen *cg,
	Type type,
	TypeContext *tc,
	Error *err	
)
{
	gcc_jit_type *ret = NULL;

	type = type_resolve(tc, type);

	switch(type.type) {
	case TYPE_PRIMITIVE_U8:
		ret = gcc_jit_context_get_type(cg->gcc, GCC_JIT_TYPE_UINT8_T);
		break;
	case TYPE_PRIMITIVE_U16:
		ret = gcc_jit_context_get_type(cg->gcc, GCC_JIT_TYPE_UINT16_T);
		break;
	case TYPE_PRIMITIVE_U32:
		ret = gcc_jit_context_get_type(cg->gcc, GCC_JIT_TYPE_UINT32_T);
		break;
	case TYPE_PRIMITIVE_U64:
		ret = gcc_jit_context_get_type(cg->gcc, GCC_JIT_TYPE_UINT64_T);
		break;
	case TYPE_PRIMITIVE_S8:
		ret = gcc_jit_context_get_type(cg->gcc, GCC_JIT_TYPE_INT8_T);
		break;
	case TYPE_PRIMITIVE_S16:
		ret = gcc_jit_context_get_type(cg->gcc, GCC_JIT_TYPE_INT16_T);
		break;
	case TYPE_PRIMITIVE_S32:
		ret = gcc_jit_context_get_type(cg->gcc, GCC_JIT_TYPE_INT32_T);
		break;
	case TYPE_PRIMITIVE_S64:
		ret = gcc_jit_context_get_type(cg->gcc, GCC_JIT_TYPE_INT64_T);
		break;
	case TYPE_PRIMITIVE_VOID:
		ret = gcc_jit_context_get_type(cg->gcc, GCC_JIT_TYPE_VOID);
		break;

	case TYPE_POINTER_CONST:
	case TYPE_POINTER_ABYSS:
	case TYPE_POINTER_VAR: {
		gcc_jit_type *base = gen_type(cg, tc->types[type.pointer.base], tc, err);
		if(*err) goto RET;

		ret = gcc_jit_type_get_pointer(base);
	} break;
	
	case TYPE_ARRAY: {
		//Note: gccjit does not do array-decay
		gcc_jit_type *elem = gen_type(cg, tc->types[type.array.base], tc, err);
		if(*err) goto RET;

		ret = gcc_jit_context_new_array_type(
			cg->gcc,
			NULL,
			elem,
			type.array.len
		);
	} break;

	case TYPE_SLICE_CONST:
	case TYPE_SLICE_ABYSS:
	case TYPE_SLICE_VAR: {
		ret = get_named_type(cg, type, tc, err);
		if(*err) goto RET;
		if(ret) goto RET;

		size_t named_type_index = cg->named_type_count - 1;

		gcc_jit_type *elem_type = gen_type(cg, tc->types[type.array.base], tc, err);
		if(*err) goto RET;

		gcc_jit_field *(fields[2]);

	   	fields[0] = gcc_jit_context_new_field(
			cg->gcc,
			NULL,
			gcc_jit_type_get_pointer(elem_type),
			"ptr"
		);

		fields[1] = gcc_jit_context_new_field(
			cg->gcc,
			NULL,
			gcc_jit_context_get_type(cg->gcc, GCC_JIT_TYPE_UINT64_T),
			"len"
		);

		gcc_jit_struct *struct_ = gcc_jit_context_new_struct_type(
			cg->gcc,
			NULL,
			cg->named_type_names[named_type_index],
			2,
			fields
		);

		ret = gcc_jit_struct_as_type(struct_);
		cg->named_types_gcc[named_type_index] = ret;
	} break;

	case TYPE_STRUCT: {
		ret = get_named_type(cg, type, tc, err);
		if(*err) goto RET;
		if(ret) goto RET;

		size_t named_type_index = cg->named_type_count - 1;

		gcc_jit_field **fields = malloc(sizeof(*fields) * type.struct_type.member_count);
		CHECK_MALLOC(fields);

		for(size_t i = 0; i < type.struct_type.member_count; i++) {
			gcc_jit_type *member_type = gen_type(
				cg,
				tc->types[type.struct_type.member_types[i]],
				tc,
				err
			);
			if(*err) {
				free(fields);
				goto RET;
			}

			fields[i] = gcc_jit_context_new_field(
				cg->gcc,
				NULL,
				member_type,
				cg->identifiers[type.struct_type.member_name_ids[i]]
			);
		}

		ret = gcc_jit_struct_as_type(gcc_jit_context_new_struct_type(
				cg->gcc,
				NULL,
				cg->named_type_names[named_type_index],
				type.struct_type.member_count,
				fields
		));
		cg->named_types_gcc[named_type_index] = ret;
		free(fields);
	} break;

	default:
		fprintf(stderr, "[INTERNAL]: Cannot Generate Type #%d\n", type.type);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;	
	}

RET:
	return ret;
}


const WyrtBackend wyrtBackend = {
	get_ctx,
	compile,
	release_ctx,
	new_param,
	new_function,
	function_get_param,
	new_cast,
	rvalue_from_lvalue,
	rvalue_from_param,
	rvalue_int_lit,
	rvalue_struct_lit,
	rvalue_array_lit,
	rvalue_cstring_lit,
	rvalue_binary_op,
	rvalue_address,
	rvalue_field,
	rvalue_fn_call,
	block_new_variable,
	lvalue_subscript,
	lvalue_deref_field,
	lvalue_deref,
	lvalue_field,
	new_block,
	block_add_eval,
	block_add_assign,
	block_add_compound_assign,
	block_end_with_return
};
