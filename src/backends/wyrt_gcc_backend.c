#include "../backend.h"

#include <stdio.h>
#include <assert.h>
#include <libgccjit.h>

WyrtContext get_ctx(Error *err)
{
	gcc_jit_context *ctx = gcc_jit_context_acquire();
	if(!ctx) {
		fprintf(stderr, "[BACKEND] Could not create GCC Context!\n");
		*err = ERROR_NOT_FOUND;
		goto RET;
	}

RET:
	return ctx;	
}

void compile(WyrtContext vpctx, GenOptions options, const char *path, Error *err)
{
	gcc_jit_context *ctx = vpctx;

	int type = options & 3;
	int dbg = options & 4;
	int opt = options & 8;

	if(dbg) {
		gcc_jit_context_set_bool_option(ctx, GCC_JIT_BOOL_OPTION_DEBUGINFO, 1);
	}
	if(opt) {
		gcc_jit_context_set_int_option(ctx, GCC_JIT_INT_OPTION_OPTIMIZATION_LEVEL, 3);
	}

	enum gcc_jit_output_kind format;
	switch(type) {
	case GEN_EXE: format = GCC_JIT_OUTPUT_KIND_EXECUTABLE; break;
	case GEN_OBJ: format = GCC_JIT_OUTPUT_KIND_OBJECT_FILE; break;
	case GEN_ASM: format = GCC_JIT_OUTPUT_KIND_ASSEMBLER; break;
	case GEN_SHR: format = GCC_JIT_OUTPUT_KIND_DYNAMIC_LIBRARY; break;
	}

	gcc_jit_context_compile_to_file(ctx, format, path);

	return;
}

void release_ctx(WyrtContext ctx)
{
	gcc_jit_context_release((gcc_jit_context*)ctx);
}

static gcc_jit_location *gcc_loc(gcc_jit_context *ctx, const DebugInfo *debug, Error *err)
{
	if(!debug) return NULL;

	gcc_jit_location *loc = gcc_jit_context_new_location(ctx, debug->file, debug->line, debug->col);
	if(!loc) {
		fprintf(stderr, "[BACKEND] Could not generate Source Location!\n");
		*err = ERROR_IO;
		goto RET;
	}
	
RET:
	return loc;
}

static uint64_t type_hash(
	Type type,
	TypeContext const *tc,
	Error *err
)
{
	// 'WyrtType'
	uint64_t hash = 0x5779727454797065ull;

	while(type.type) {
		hash = (hash << 7) | ((hash >> 57) ^ type.type);
		
		switch(type.type) {
		case TYPE_POINTER_CONST:
		case TYPE_POINTER_ABYSS:
		case TYPE_POINTER_VAR:
			type = tc->types[type.pointer.base];
			break;
		case TYPE_ARRAY:
			hash ^= type.array.len << 3;
			type = tc->types[type.array.base];
			break;
		case TYPE_SLICE_CONST:
		case TYPE_SLICE_ABYSS:
		case TYPE_SLICE_VAR:
			type = tc->types[type.slice.base];
			break;
		case TYPE_STRUCT:
			hash ^= type.struct_type.member_count;
			for(size_t i = 0; i < type.struct_type.member_count; i++) {
				hash = ((hash << 5) | (hash >> 59)) ^ type.struct_type.member_name_ids[i];
				hash = (hash << 1) | (hash >> 1);
				hash ^= type_hash(tc->types[type.struct_type.member_types[i]], tc, err);
				if(*err) goto RET;
			}
			type.type = TYPE_NONE;
			break;
		case TYPE_TYPEDEF:
			hash ^= (type.typdef.id << 5);
			type = tc->types[type.typdef.backing];
			break;
		default:
			type.type = TYPE_NONE;
			break;
		}
	}

RET:
	return hash;
}

#define TYPE_HASH_RENDER_LEN 8
static void type_hash_render(uint64_t hash, char *buf)
{
	buf[0] = '.';

	for(int i = 1; i < TYPE_HASH_RENDER_LEN; i++) {
		uint8_t data = hash & 63;
		hash >>= 6;

		if(data <= 9) {
			buf[i] = '0' + data;
		} else if(data <= 35) {
			buf[i] = 'A' + data - 10;
		} else if(data <= 61) {
			buf[i] = 'a' + data - 36;
		} else if(data == 62) {
			buf[i] = '@';
		} else {
			buf[i] = '_';
		}
	}

	buf[TYPE_HASH_RENDER_LEN] = '\0';
}

static gcc_jit_type *gen_type(
	WyrtContext vpctx,
	Type type,
	TypeContext const *tc,
	Error *err	
)
{
	gcc_jit_context *ctx = vpctx;
	gcc_jit_type *ret = NULL;

	switch(type.type) {
	case TYPE_PRIMITIVE_U8:
		ret = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_UINT8_T);
		break;
	case TYPE_PRIMITIVE_U16:
		ret = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_UINT16_T);
		break;
	case TYPE_PRIMITIVE_U32:
		ret = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_UINT32_T);
		break;
	case TYPE_PRIMITIVE_U64:
		ret = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_UINT64_T);
		break;
	case TYPE_PRIMITIVE_S8:
		ret = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_INT8_T);
		break;
	case TYPE_PRIMITIVE_S16:
		ret = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_INT16_T);
		break;
	case TYPE_PRIMITIVE_S32:
		ret = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_INT32_T);
		break;
	case TYPE_PRIMITIVE_S64:
		ret = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_INT64_T);
		break;
	case TYPE_PRIMITIVE_VOID:
		ret = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_VOID);
		break;

	case TYPE_POINTER_CONST:
	case TYPE_POINTER_ABYSS:
	case TYPE_POINTER_VAR: {
		gcc_jit_type *base = gen_type(ctx, tc->types[type.pointer.base], tc, err);
		if(*err) goto RET;

		ret = gcc_jit_type_get_pointer(base);
	} break;
	
	case TYPE_ARRAY: {
		//Note: gccjit does not do array-decay
		gcc_jit_type *elem = gen_type(ctx, tc->types[type.array.base], tc, err);
		if(*err) goto RET;

		ret = gcc_jit_context_new_array_type(
			ctx,
			NULL,
			elem,
			type.array.len
		);
	} break;

	case TYPE_SLICE_CONST:
	case TYPE_SLICE_ABYSS:
	case TYPE_SLICE_VAR: {
		gcc_jit_type *elem_type = gen_type(ctx, tc->types[type.array.base], tc, err);
		if(*err) goto RET;

		gcc_jit_field *(fields[2]);

	   	fields[0] = gcc_jit_context_new_field(
			ctx,
			NULL,
			gcc_jit_type_get_pointer(elem_type),
			"ptr"
		);
		if(!fields[0]) {
			fprintf(stderr, "[BACKEND] Could not generate field!\n");
			*err = ERROR_IO;
			goto RET;
		}

		fields[1] = gcc_jit_context_new_field(
			ctx,
			NULL,
			gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_UINT64_T),
			"len"
		);
		if(!fields[1]) {
			fprintf(stderr, "[BACKEND] Could not generate slice field!\n");
			*err = ERROR_IO;
			goto RET;
		}

		char name[TYPE_HASH_RENDER_LEN+1];
		uint64_t hash = type_hash(type, tc, err);
		if(*err) goto RET;
		
		type_hash_render(hash, name);

		gcc_jit_struct *struct_ = gcc_jit_context_new_struct_type(
			ctx,
			NULL,
			name,
			2,
			fields
		);
		if(!struct_) {
			fprintf(stderr, "[BACKEND] Could not generate slice type!\n");
			*err = ERROR_IO;
			goto RET;
		}

		ret = gcc_jit_struct_as_type(struct_);
	} break;

	case TYPE_STRUCT: {
		gcc_jit_field **fields = malloc(sizeof(*fields) * type.struct_type.member_count);
		CHECK_MALLOC(fields);

		for(size_t i = 0; i < type.struct_type.member_count; i++) {
			gcc_jit_type *member_type = gen_type(
				ctx,
				tc->types[type.struct_type.member_types[i]],
				tc,
				err
			);
			if(*err) {
				free(fields);
				goto RET;
			}

			char name_render[10] = {0};
			snprintf(name_render, 10, ".%08d", type.struct_type.member_name_ids[i]);

			fields[i] = gcc_jit_context_new_field(
				ctx,
				NULL,
				member_type,
				name_render
			);
			if(!fields[i]) {
				fprintf(stderr, "[BACKEND] Could not generate struct field!\n");
				free(fields);
				*err = ERROR_IO;
				goto RET;
			}
		}

		char name[TYPE_HASH_RENDER_LEN+1];
		uint64_t hash = type_hash(type, tc, err);
		if(*err) {
			free(fields);
			goto RET;
		}
		
		type_hash_render(hash, name);

		ret = gcc_jit_struct_as_type(gcc_jit_context_new_struct_type(
			ctx,
			NULL,
			name,
			type.struct_type.member_count,
			fields
		));
		free(fields);
	} break;

	case TYPE_TYPEDEF: {
		ret = gen_type(ctx, tc->types[type.typdef.backing], tc, err);
		if(*err) goto RET;
	} break;

	default:
		fprintf(stderr, "[BACKEND]: Cannot Generate Type #%d\n", type.type);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;	
	}

RET:
	return ret;
}

WyrtParam new_param(WyrtContext vpctx, const DebugInfo* debug, Type type, TypeContext const *tc, char const *name, Error* err)
{
	gcc_jit_context *ctx = vpctx;
	gcc_jit_param *param = NULL;
	gcc_jit_type *gcc_type = gen_type(ctx, type, tc, err);
	if(*err) goto RET;

	gcc_jit_location *loc = gcc_loc(ctx, debug, err);
	if(*err) goto RET;

	param = gcc_jit_context_new_param(ctx, loc, gcc_type, name);
	if(!param) {
		fprintf(stderr, "[BACKEND] Could not create parameter!\n");
		*err = ERROR_IO;
		goto RET;
	}

RET:
	return param;
}

WyrtFunction new_function(
	WyrtContext vpctx,
	const DebugInfo *debug,
	Type ret,
	TypeContext const *tc, 
	WyrtParam *vpparams,
	size_t param_count,
	bool imported,
	const char *name,
	Error *err
)
{
	gcc_jit_context *ctx = vpctx;
	gcc_jit_param **params = (gcc_jit_param**)vpparams;

	gcc_jit_type *gcc_type = gen_type(ctx, ret, tc, err);
	if(*err) goto RET;
	
	gcc_jit_location *loc = gcc_loc(ctx, debug, err);
	if(*err) goto RET;

	gcc_jit_function *fn = gcc_jit_context_new_function(
		ctx,
		loc,
		imported ? GCC_JIT_FUNCTION_IMPORTED : GCC_JIT_FUNCTION_EXPORTED,
		gcc_type,
		name,
		param_count,
		params,
		false
	);
	
	if(!fn) {
		fprintf(stderr, "[BACKEND] Could not create function!\n");
		*err = ERROR_IO;
		goto RET;
	}

RET:
	return fn;
}


WyrtParam function_get_param(WyrtContext vpctx, WyrtFunction vpfn, size_t idx, Error* err)
{
	gcc_jit_context *ctx = vpctx;
	gcc_jit_function *fn = vpfn;

	gcc_jit_param *param = gcc_jit_function_get_param(fn, idx);
	if(!param) {
		fprintf(stderr, "[BACKEND] Could not retrieve Function Parameter!\n");
		*err = ERROR_IO;
		goto RET;
	}

RET:
	return param;
}

WyrtRvalue new_cast(WyrtContext vpctx, const DebugInfo* debug, WyrtRvalue vpval, Type type, TypeContext const *tc, Error* err)
{
	gcc_jit_context *ctx = vpctx;
	gcc_jit_rvalue *val = vpval;

	gcc_jit_location *loc = gcc_loc(ctx, debug, err);
	if(*err) goto RET;

	gcc_jit_type *gcc_type = gen_type(ctx, type, tc, err);
	if(*err) goto RET;

	gcc_jit_rvalue *casted = gcc_jit_context_new_cast(ctx, loc, val, gcc_type);
	if(!casted) {
		fprintf(stderr, "[BACKEND] Could not cast value!\n");
		*err = ERROR_IO;
		goto RET;
	}

RET:
	return casted;
}

WyrtRvalue rvalue_from_lvalue(WyrtLvalue vplval)
{
	gcc_jit_lvalue *lval = vplval;
	gcc_jit_rvalue *rval = gcc_jit_lvalue_as_rvalue(lval);
	return rval;
}

WyrtRvalue rvalue_from_param(WyrtParam vpparam)
{
	gcc_jit_param *param = vpparam;
	gcc_jit_rvalue *rval = gcc_jit_param_as_rvalue(param);
	return rval;
}

WyrtRvalue rvalue_null(WyrtContext vpctx, Type type, TypeContext *tc, Error *err)
{
	gcc_jit_context *ctx = vpctx;
	gcc_jit_rvalue *rval = NULL;

	gcc_jit_type *t = gen_type(ctx, type, tc, err);
	if(*err) goto RET;

	switch(type.type) {
	case TYPE_PRIMITIVE_U8:
	case TYPE_PRIMITIVE_U16:
	case TYPE_PRIMITIVE_U32:
	case TYPE_PRIMITIVE_U64:
	case TYPE_PRIMITIVE_S8:
	case TYPE_PRIMITIVE_S16:
	case TYPE_PRIMITIVE_S32:
	case TYPE_PRIMITIVE_S64:
	case TYPE_POINTER_CONST:
	case TYPE_POINTER_ABYSS:
	case TYPE_POINTER_VAR: {
		rval = gcc_jit_context_zero(ctx, t);
	} break;

	case TYPE_ARRAY: {
		rval = gcc_jit_context_new_array_constructor(ctx, NULL, t, 0, NULL);
	} break;

	case TYPE_SLICE_CONST:
	case TYPE_SLICE_ABYSS:
	case TYPE_SLICE_VAR:
	case TYPE_STRUCT: {
		rval = gcc_jit_context_new_struct_constructor(ctx, NULL, t, 0, NULL, NULL);
	} break;

	case TYPE_TYPEDEF:
		assert(false);
	}

	if(!rval) {
		fprintf(stderr, "[BACKEND] Could not generate NULL rvalue!\n");
		*err = ERROR_IO;
		goto RET;
	}

RET:
	return rval;
}

WyrtRvalue rvalue_int_lit(WyrtContext vpctx, intmax_t val, TypeType type, Error *err)
{
	gcc_jit_context *ctx = vpctx;
	gcc_jit_rvalue *rval = NULL;

	gcc_jit_type *t = NULL;

	switch(type) {
	case TYPE_PRIMITIVE_U8:
		t = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_UINT8_T);
		break;
	case TYPE_PRIMITIVE_U16:
		t = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_UINT16_T);
		break;
	case TYPE_PRIMITIVE_U32:
		t = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_UINT32_T);
		break;
	case TYPE_PRIMITIVE_U64:
		t = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_UINT64_T);
		break;
	case TYPE_PRIMITIVE_S8:
		t = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_INT8_T);
		break;
	case TYPE_PRIMITIVE_S16:
		t = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_INT16_T);
		break;
	case TYPE_PRIMITIVE_S32:
		t = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_INT32_T);
		break;
	case TYPE_PRIMITIVE_S64:
		t = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_INT64_T);
		break;
	default:
		assert(false);
	}

	if(!t) {
		fprintf(stderr, "[BACKEND] Could not get Integer Type!\n");
		*err = ERROR_IO;
		goto RET;
	}

	rval = gcc_jit_context_new_rvalue_from_long(ctx, t, val);
	if(!rval) {
		fprintf(stderr, "[BACKEND] Could not generate Integer Literal!\n");
		*err = ERROR_IO;
		goto RET;
	}

RET:
	return rval;
}

WyrtRvalue rvalue_struct_lit(
	WyrtContext vpctx,
	const DebugInfo* debug,
	Type type,
	TypeContext const* tc,
	WyrtRvalue* vpvals,
	size_t val_count,
	Error *err
)
{
	gcc_jit_context *ctx = vpctx;
	gcc_jit_rvalue **vals = (gcc_jit_rvalue**)vpvals;

	gcc_jit_location *loc = gcc_loc(ctx, debug, err);
	if(*err) goto RET;

	gcc_jit_type *t = gen_type(ctx, type, tc, err);
	if(*err) goto RET;

	gcc_jit_struct *s = gcc_jit_type_is_struct(t);
	assert(s);

	gcc_jit_rvalue *dummy = gcc_jit_context_new_struct_constructor(
		ctx,
		loc,
		t,
		0,
		NULL,
		NULL
	);
	if(!dummy) {
		fprintf(stderr, "[BACKEND] Could not create dummy struct!\n");
		*err = ERROR_IO;
		goto RET;
	}

	for(size_t i = 0; i < val_count; i++) {
		gcc_jit_field *field = gcc_jit_struct_get_field(s, i);
		if(!field) {
			fprintf(stderr, "[BACKEND] Could not get field from struct!\n");
			*err = ERROR_IO;
			goto RET;
		}

		gcc_jit_rvalue *mval = gcc_jit_rvalue_access_field(dummy, NULL, field);
		if(!mval) {
			fprintf(stderr, "[BACKEND] Could not get field of dummy struct!\n");
			*err = ERROR_IO;
			goto RET;
		}

		gcc_jit_type *mtype = gcc_jit_rvalue_get_type(mval);
		if(!mtype) {
			fprintf(stderr, "[BACKEND] Could not get type of field of dummy struct!\n");
			*err = ERROR_IO;
			goto RET;
		}

		// Also catch errors with pointers to structs
		gcc_jit_rvalue *casted = gcc_jit_context_new_bitcast(ctx, loc, vals[i], mtype);
		if(!casted) {
			fprintf(stderr, "[BACKEND] Could not struct literal member!\n");
			*err = ERROR_IO;
			goto RET;
		}

		// OK --- All Opaque to Codegen
		vals[i] = casted;
	}
	gcc_jit_rvalue *lit = gcc_jit_context_new_struct_constructor(
		ctx,
		loc,
		t,
		val_count,
		NULL,
		vals
	);
	
	if(!lit) {
		fprintf(stderr, "[BACKEND] Could not generate Struct Literal!\n");
		*err = ERROR_IO;
		goto RET;
	}

RET:
	return lit;
}

WyrtRvalue rvalue_array_lit(
	WyrtContext vpctx,
	const DebugInfo *debug,
	Type type,
	TypeContext const *tc,
	WyrtRvalue *vpvals,
	size_t val_count,
	Error *err
)
{
	gcc_jit_context *ctx = vpctx;
	gcc_jit_rvalue **vals = (gcc_jit_rvalue**)vpvals;

	gcc_jit_location *loc = gcc_loc(ctx, debug, err);
	if(*err) goto RET;

	gcc_jit_type *t = gen_type(ctx, type, tc, err);
	if(*err) goto RET;
	
	gcc_jit_rvalue *lit = gcc_jit_context_new_array_constructor(
		ctx,
		loc,
		t,
		val_count,
		vals
	);
	
	if(!lit) {
		fprintf(stderr, "[BACKEND] Could not generate Array Literal!\n");
		*err = ERROR_IO;
		goto RET;
	}

RET:
	return lit;
}

WyrtRvalue rvalue_cstring_lit(WyrtContext vpctx, const DebugInfo *debug, char const *str, Error *err)
{
	gcc_jit_context *ctx = vpctx;

	gcc_jit_location *loc = gcc_loc(ctx, debug, err);
	if(*err) goto RET;

	gcc_jit_rvalue *lit = gcc_jit_context_new_string_literal(ctx, str);

	if(!lit) {
		fprintf(stderr, "[BACKEND] Could not generate String Literal!\n");
		*err = ERROR_IO;
		goto RET;
	}

	gcc_jit_type *u8 = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_UINT8_T);
	if(!u8) {
		fprintf(stderr, "[BACKEND] Could not generate u8 type!\n");
		*err = ERROR_IO;
		goto RET;
	}

	gcc_jit_type *t = gcc_jit_type_get_pointer(u8);
	if(!t) {
		fprintf(stderr, "[BACKEND] Could not generate &u8 type!\n");
		*err = ERROR_IO;
		goto RET;
	}

	gcc_jit_rvalue *val = gcc_jit_context_new_cast(ctx, loc, lit, t);
	if(!val) {
		fprintf(stderr, "[BACKEND] Could not make String Literal into &u8!\n");
		*err = ERROR_IO;
		goto RET;
	}

RET:
	return val;
}

WyrtRvalue rvalue_binary_op(
	WyrtContext vpctx,
	const DebugInfo *debug,
	AstNodeType op,
	TypeType type,
	WyrtRvalue vplhs,
	WyrtRvalue vprhs,
	Error *err
)
{
	gcc_jit_context *ctx = vpctx;
	gcc_jit_rvalue *lhs = vplhs;
	gcc_jit_rvalue *rhs = vprhs;
	gcc_jit_rvalue *res = NULL;

	enum gcc_jit_binary_op gcc_op;
	switch(op) {
	case AST_MUL:
		gcc_op = GCC_JIT_BINARY_OP_MULT;
		break;
	case AST_DIV:
		gcc_op = GCC_JIT_BINARY_OP_DIVIDE;
		break;
	case AST_ADD:
		gcc_op = GCC_JIT_BINARY_OP_PLUS;
		break;
	case AST_SUB:
		gcc_op = GCC_JIT_BINARY_OP_MINUS;
		break;
	default:
		assert(false);
	}

	gcc_jit_type *t;
	switch(type) {
	case TYPE_PRIMITIVE_U8:
		t = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_UINT8_T);
		break;
	case TYPE_PRIMITIVE_U16:
		t = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_UINT16_T);
		break;
	case TYPE_PRIMITIVE_U32:
		t = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_UINT32_T);
		break;
	case TYPE_PRIMITIVE_U64:
		t = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_UINT64_T);
		break;
	case TYPE_PRIMITIVE_S8:
		t = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_INT8_T);
		break;
	case TYPE_PRIMITIVE_S16:
		t = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_INT16_T);
		break;
	case TYPE_PRIMITIVE_S32:
		t = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_INT32_T);
		break;
	case TYPE_PRIMITIVE_S64:
		t = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_INT64_T);
		break;
	default:
		assert(false);
	}

	if(!t) {
		fprintf(stderr, "[BACKEND] Could not generate integer type!\n");
		*err = ERROR_IO;
		goto RET;
	}

	gcc_jit_location *loc = gcc_loc(ctx, debug, err);
	if(*err) goto RET;

	res = gcc_jit_context_new_binary_op(ctx, loc, gcc_op, t, lhs, rhs);

	if(!res) {
		fprintf(stderr, "[BACKEND] Could not generate binary operation!\n");
		*err = ERROR_IO;
		goto RET;
	}

RET:
	return res;
}

WyrtRvalue rvalue_address(WyrtContext vpctx, const DebugInfo* debug, WyrtLvalue vplval, Error* err)
{
	gcc_jit_context *ctx = vpctx;
	gcc_jit_lvalue *lval = vplval;
	gcc_jit_location *loc = gcc_loc(ctx, debug, err);
	if(*err) goto RET;

	gcc_jit_rvalue *rval = gcc_jit_lvalue_get_address(lval, loc);

	if(!rval) {
		fprintf(stderr, "[BACKEND] Could not get address of Lvalue!\n");
		*err = ERROR_IO;
		goto RET;
	}

RET:
	return rval;
}

WyrtRvalue rvalue_field(
	WyrtContext vpctx,
	const DebugInfo *debug,
	WyrtRvalue vprval,
	Type type,
	TypeContext const *tc,
	size_t idx,
	Error *err
)
{
	gcc_jit_context *ctx = vpctx;
	gcc_jit_rvalue *rval = vprval;
	gcc_jit_rvalue *ret = NULL;

	gcc_jit_location *loc = gcc_loc(ctx, debug, err);
	if(*err) goto RET;
		
	gcc_jit_type *t = gcc_jit_rvalue_get_type(rval);
	if(!t) {
		fprintf(stderr, "[BACKEND] Could not get type of Rvalue!\n");
		*err = ERROR_IO;
		goto RET;
	}

	gcc_jit_struct *s = gcc_jit_type_is_struct(t);
	assert(s);

	gcc_jit_field *field = gcc_jit_struct_get_field(s, idx);
	if(!field) {
		fprintf(stderr, "[BACKEND] Could not get struct field!\n");
		*err = ERROR_IO;
		goto RET;
	}

	ret = gcc_jit_rvalue_access_field(rval, loc, field);
	if(!ret) {
		fprintf(stderr, "[BACKEND] Could not get field of rvalue!\n");
		*err = ERROR_IO;
		goto RET;
	}

RET:
	return ret;
}

WyrtRvalue rvalue_fn_call(
	WyrtContext vpctx,
	const DebugInfo *debug,
	WyrtFunction vpfn,
	WyrtRvalue *vpargs,
	size_t arg_count,
	Error *err
)
{
	gcc_jit_context *ctx = vpctx;
	gcc_jit_function *fn = vpfn;
	gcc_jit_rvalue **args = (gcc_jit_rvalue**)vpargs;
	gcc_jit_rvalue *ret = NULL;

	gcc_jit_location *loc = gcc_loc(ctx, debug, err);
	if(*err) goto RET;

	// Get around gccjit being blind to identical struct types
	gcc_jit_rvalue *fnptr = gcc_jit_function_get_address(fn, NULL);
	if(!fnptr) {
		fprintf(stderr, "[BACKEND] Could not get function pointer to get type info!\n");
		*err = ERROR_IO;
		goto RET;
	}
	gcc_jit_type *fntype = gcc_jit_rvalue_get_type(fnptr);
	if(!fntype) {
		fprintf(stderr, "[BACKEND] Could not get function type!\n");
		*err = ERROR_IO;
		goto RET;
	}
	gcc_jit_function_type *actual_type = gcc_jit_type_dyncast_function_ptr_type(fntype);
	if(!actual_type) {
		fprintf(stderr, "[BACKEND] Could not get underlying function type!\n");
		*err = ERROR_IO;
		goto RET;
	}
	for(size_t i = 0; i < arg_count; i++) {
		gcc_jit_type *t = gcc_jit_function_type_get_param_type(actual_type, i);
		if(!t) {
			fprintf(stderr, "[BACKEND] Could not get function parameter type!\n");
			*err = ERROR_IO;
			goto RET;
		}

		// Also catch pointers to structs
		gcc_jit_rvalue *casted = gcc_jit_context_new_bitcast(ctx, loc, args[i], t);
		if(!casted) {
			fprintf(stderr, "[BACKEND] Could not bitcast struct!\n");
			*err = ERROR_IO;
			goto RET;
		}
		// OK --- All Opaque to Codegen
		args[i] = casted;
	}
	ret = gcc_jit_context_new_call(ctx, loc, fn, arg_count, args);
	if(!ret) {
		fprintf(stderr, "[BACKEND] Could not generate function call!\n");
		*err = ERROR_IO;
		goto RET;
	}

RET:
	return ret;
}

WyrtLvalue block_new_variable(
	WyrtContext vpctx,
	const DebugInfo *debug,
	WyrtBlock vpblk,
	Type type,
	TypeContext const *tc,
	const char *name,
	Error *err
)
{
	gcc_jit_context *ctx = vpctx;
	gcc_jit_block *blk = vpblk;
	
	gcc_jit_location *loc = gcc_loc(ctx, debug, err);
	if(*err) goto RET;

	gcc_jit_function *fn = gcc_jit_block_get_function(blk);
	if(!fn) {
		fprintf(stderr, "[BACKEND] Could not retrieve block's function!\n");
		*err = ERROR_IO;
		goto RET;
	}

	gcc_jit_type *t = gen_type(ctx, type, tc, err);
	if(*err) goto RET;

	gcc_jit_lvalue *var = gcc_jit_function_new_local(
		fn,
		loc,
		t,
		name
	);
	if(!var) {
		fprintf(stderr, "[BACKEND] Could not generate variable!\n");
		*err = ERROR_IO;
		goto RET;
	}

RET:
	return var;
}

WyrtLvalue lvalue_subscript(WyrtContext vpctx, const DebugInfo *debug, WyrtRvalue vparr, WyrtRvalue vpidx, Error *err)
{
	gcc_jit_context *ctx = vpctx;
	gcc_jit_rvalue *arr = vparr;
	gcc_jit_rvalue *idx = vpidx;

	gcc_jit_location *loc = gcc_loc(ctx, debug, err);
	if(*err) goto RET;

	gcc_jit_type *t = gcc_jit_rvalue_get_type(arr);
	if(!t) {
		fprintf(stderr, "[BACKEND] Could not get type of subscripted rvalue!\n");
		*err = ERROR_IO;
		goto RET;
	}

	// Know this is a slice
	if(gcc_jit_type_is_struct(t)) {
		gcc_jit_struct *s = gcc_jit_type_is_struct(t);
		assert(s);

		gcc_jit_field *mptr = gcc_jit_struct_get_field(s, 0);
		if(!mptr) {
			fprintf(stderr, "[BACKEND] Could not get pointer element of slice!\n");
			*err = ERROR_IO;
			goto RET;
		}

		gcc_jit_rvalue *ptr = gcc_jit_rvalue_access_field(arr, loc, mptr);
		if(!ptr) {
			fprintf(stderr, "[BACKEND] Could not get pointer member of slice!\n");
			*err = ERROR_IO;
			goto RET;
		}

		arr = ptr;
	}

	gcc_jit_lvalue *lval = gcc_jit_context_new_array_access(ctx, loc, arr, idx);
	if(!lval) {
		fprintf(stderr, "[BACKEND] Could not generate subscript!\n");
		*err = ERROR_IO;
		goto RET;
	}

RET:
	return lval;
}

WyrtLvalue lvalue_deref_field(
	WyrtContext vpctx,
	const DebugInfo *debug,
	WyrtRvalue vprval,
	Type type,
	TypeContext const *tc,
	size_t idx,
	Error *err
)
{
	gcc_jit_context *ctx = vpctx;
	gcc_jit_rvalue *rval = vprval;

	gcc_jit_location *loc = gcc_loc(ctx, debug, err);
	if(*err) goto RET;

	gcc_jit_type *t = gcc_jit_rvalue_get_type(rval);
	if(!t) {
		fprintf(stderr, "[BACKEND] Could not get type of Rvalue!\n");
		*err = ERROR_IO;
		goto RET;
	}

	gcc_jit_type *base = gcc_jit_type_is_pointer(t);
	assert(base);

	gcc_jit_struct *s = gcc_jit_type_is_struct(base);
	assert(s);

	gcc_jit_field *field = gcc_jit_struct_get_field(s, idx);
	if(!field) {
		fprintf(stderr, "[BACKEND] Could not get struct field!\n");
		*err = ERROR_IO;
		goto RET;
	}

	gcc_jit_lvalue *lval = gcc_jit_rvalue_dereference_field(rval, loc, field);
	if(!lval) {
		fprintf(stderr, "[BACKEND] Could not generate indirect field access!\n");
		*err = ERROR_IO;
		goto RET;
	}
	
RET:
	return lval;
}

WyrtLvalue lvalue_deref(WyrtContext vpctx, const DebugInfo *debug, WyrtRvalue vprval, Error *err)
{
	gcc_jit_context *ctx = vpctx;
	gcc_jit_rvalue *rval = vprval;

	gcc_jit_location *loc = gcc_loc(ctx, debug, err);
	if(*err) goto RET;

	gcc_jit_lvalue *lval = gcc_jit_rvalue_dereference(rval, loc);
	if(!lval) {
		fprintf(stderr, "[BACKEND] Could not generate dereference!\n");
		*err = ERROR_IO;
		goto RET;
	}

RET:
	return lval;
}

WyrtLvalue lvalue_field(
	WyrtContext vpctx,
	const DebugInfo *debug,
	WyrtLvalue vpparent,
	Type type,
	TypeContext const *tc,
	size_t idx,
	Error *err
)
{
	gcc_jit_context *ctx = vpctx;
	gcc_jit_lvalue *parent = vpparent;

	gcc_jit_location *loc = gcc_loc(ctx, debug, err);
	if(*err) goto RET;

	gcc_jit_rvalue *r = gcc_jit_lvalue_as_rvalue(parent);
	if(!r) {
		fprintf(stderr, "[BACKEND] Could not cast Lvalue to Rvalue to get its type!\n");
		*err = ERROR_IO;
		goto RET;
	}
	gcc_jit_type *t = gcc_jit_rvalue_get_type(r);
	if(!t) {
		fprintf(stderr, "[BACKEND] Could not get type of Rvalue!\n");
		*err = ERROR_IO;
		goto RET;
	}

	gcc_jit_struct *s = gcc_jit_type_is_struct(t);
	assert(s);

	gcc_jit_field *field = gcc_jit_struct_get_field(s, idx);
	if(!field) {
		fprintf(stderr, "[BACKEND] Could not get struct field!\n");
		*err = ERROR_IO;
		goto RET;
	}

	gcc_jit_lvalue *lval = gcc_jit_lvalue_access_field(parent, loc, field);
	if(!lval) {
		fprintf(stderr, "[BACKEND] Could not generate field access of lvalue!\n");
		*err = ERROR_IO;
		goto RET;
	}

RET:
	return lval;
}

WyrtBlock new_block(WyrtContext vpctx, WyrtFunction vpfn, Error *err)
{
	gcc_jit_context *ctx = vpctx;
	gcc_jit_function *fn = vpfn;

	gcc_jit_block *blk = gcc_jit_function_new_block(fn, NULL);
	if(!blk) {
		fprintf(stderr, "[BACKEND] Could not generate block!\n");
		*err = ERROR_IO;
		goto RET;
	}

RET:
	return blk;
}

void block_end_with_return(WyrtContext vpctx, const DebugInfo *debug, WyrtBlock vpblk, WyrtRvalue vpval, Error *err)
{
	gcc_jit_context *ctx = vpctx;
	gcc_jit_block *blk = vpblk;
	gcc_jit_rvalue *val = vpval;

	gcc_jit_location *loc = gcc_loc(ctx, debug, err);
	if(*err) goto RET;

	if(vpval) {
		gcc_jit_block_end_with_return(blk, loc, val);
	} else {
		gcc_jit_block_end_with_void_return(blk, loc);	
	}

RET:
	return;
}

void block_add_eval(WyrtContext vpctx, const DebugInfo *debug, WyrtBlock vpblk, WyrtRvalue vprval, Error *err)
{
	gcc_jit_context *ctx = vpctx;
	gcc_jit_block *blk = vpblk;
	gcc_jit_rvalue *rval = vprval;

	gcc_jit_location *loc = gcc_loc(ctx, debug, err);
	if(*err) goto RET;

	gcc_jit_block_add_eval(blk, loc, rval);

RET:
	return;
}

void block_add_assign(WyrtContext vpctx, const DebugInfo *debug, WyrtBlock vpblk, WyrtLvalue vpvar, WyrtRvalue vpval, Error *err)
{
	gcc_jit_context *ctx = vpctx;
	gcc_jit_block *blk = vpblk;
	gcc_jit_lvalue *var = vpvar;
	gcc_jit_rvalue *val = vpval;

	gcc_jit_location *loc = gcc_loc(ctx, debug, err);
	if(*err) goto RET;

	gcc_jit_rvalue *rvar = gcc_jit_lvalue_as_rvalue(var);
	if(!rvar) {
		fprintf(stderr, "[BACKEND] Could not get rvalue from lvalue!\n");
		*err = ERROR_IO;
		goto RET;
	}

	gcc_jit_type *vart = gcc_jit_rvalue_get_type(rvar);
	if(!vart) {
		fprintf(stderr, "[BACKEND] Could not get type from Rvalue!\n");
		*err = ERROR_IO;
		goto RET;
	}

	// Get around annoying type uniquing, even with identical struct names
	gcc_jit_rvalue *casted = gcc_jit_context_new_bitcast(ctx, loc, val, vart);
	if(!casted) {
		fprintf(stderr, "[BACKEND] Could not bitcast rvlaue before assignment!\n");
		*err = ERROR_IO;
		goto RET;
	}

	gcc_jit_block_add_assignment(blk, loc, var, casted);

RET:
	return;
}

void block_add_compound_assign(
	WyrtContext vpctx,
	const DebugInfo *debug,
	WyrtBlock vpblk,
	WyrtLvalue vpvar,
	WyrtRvalue vpval,
	AstNodeType op,
	Error *err
)
{
	gcc_jit_context *ctx = vpctx;
	gcc_jit_block *blk = vpblk;
	gcc_jit_lvalue *var = vpvar;
	gcc_jit_rvalue *val = vpval;

	gcc_jit_location *loc = gcc_loc(ctx, debug, err);
	if(*err) goto RET;

	enum gcc_jit_binary_op gcc_op;
	switch(op) {
	case AST_MUL_ASSIGN:
		gcc_op = GCC_JIT_BINARY_OP_MULT;
		break;
	case AST_DIV_ASSIGN:
		gcc_op = GCC_JIT_BINARY_OP_DIVIDE;
		break;
	case AST_ADD_ASSIGN:
		gcc_op = GCC_JIT_BINARY_OP_PLUS;
		break;
	case AST_SUB_ASSIGN:
		gcc_op = GCC_JIT_BINARY_OP_MINUS;
		break;
	default:
		assert(false);
	}

	gcc_jit_block_add_assignment_op(blk, loc, var, gcc_op, val);

RET:
	return;
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

	rvalue_null,
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
	block_end_with_return,
	block_add_eval,
	block_add_assign,
	block_add_compound_assign,
};
