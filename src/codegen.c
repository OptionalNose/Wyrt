#include "codegen.h"

#include <string.h>
#include <assert.h>

#include "ui.h"

typedef struct {
	gcc_jit_rvalue *expr;
	Type type;
} Expr;

typedef struct {
	gcc_jit_lvalue *lvalue;
	Type type;
	bool mut;
	bool read;
} Lvalue;

void codegen_init(
	CodeGen *cg,
	const AstNode *nodes,
	size_t node_count,
	char *const *identifiers,
	char *const *strings,
	size_t string_count
)
{
	gcc_jit_context *gcc = gcc_jit_context_acquire();

	*cg = (CodeGen) {
		.nodes = nodes,
		.node_count = node_count,
		.identifiers = identifiers,
		.strings = strings,
		.string_count = string_count,
		.fn_count = 0,
		.fn_sigs = NULL,
		.gcc = gcc,
		.named_types = NULL,
		.named_types_gcc = NULL,
		.named_type_count = 0,
	};

	return;
}

void codegen_clean(const CodeGen *cg)
{
	for(size_t i = 0; i < cg->fn_count; i++) {
		free(cg->fn_sigs[i].args);
		free(cg->fn_sigs[i].arg_ids);
	}
	free(cg->fn_sigs);
	if(cg->gcc) gcc_jit_context_release(cg->gcc);
}

static gcc_jit_location *gen_loc(gcc_jit_context *ctx, DebugInfo info)
{
	return gcc_jit_context_new_location(
		ctx,
		info.file,
		info.line,
		info.col	
	);
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

static gcc_jit_function *gen_fnsig(
	CodeGen *cg,
	FnSig *sig,
	Scope *scope,
	size_t i,
	Error *err
)
{
	gcc_jit_function *fn = NULL;
	DynArr args;
	DynArr arg_ids;
	DynArr arg_gccs;
	dynarr_init(&args, sizeof(Type));	
	dynarr_init(&arg_ids, sizeof(size_t));
	dynarr_init(&arg_gccs, sizeof(gcc_jit_param*));
	StringBuilder arg_builder = { 0 };
	gcc_jit_type *ret_gcc;

	AstNode ident = cg->nodes[cg->nodes[i].fn_def.ident];
	assert(ident.type == AST_IDENT);
	size_t id = ident.ident.id;

	AstNode block = cg->nodes[cg->nodes[i].fn_def.block];
	bool imported;
	size_t linkage_name;
	
	if(block.type == AST_EXTERN) {
		AstNode name = cg->nodes[block.extrn.name];
		assert(name.type == AST_STRING_LIT);
		linkage_name = name.string_lit.id;
		imported = true;
	} else {
		linkage_name = id;
		imported = false;
	}

	AstNode type = cg->nodes[cg->nodes[i].fn_def.fn_type];
	assert(type.type == AST_FN_TYPE);

	Type ret = type_from_ast(
		&scope->tc,
		cg->nodes,
		type.fn_type.ret_type,
		err
	);
	if(*err) goto RET;

	ret_gcc = gen_type(cg, ret, &scope->tc, err);
	if(*err) goto RET;


	for(size_t i = 0; i < type.fn_type.arg_count; i++) {
		Type arg_type = type_from_ast(
			&scope->tc,
			cg->nodes,
			type.fn_type.args[2*i + 1],
			err
		);
		if(*err) goto RET;

		AstNode arg = cg->nodes[type.fn_type.args[2*i]];
		assert(arg.type == AST_IDENT);

		size_t arg_id = arg.ident.id;

		dynarr_push(&args, &arg_type, err);
		if(*err) goto RET;
		dynarr_push(&arg_ids, &arg_id, err);
		if(*err) goto RET;

		if(arg_type.type == TYPE_SLICE_CONST
			|| arg_type.type == TYPE_SLICE_ABYSS
			|| arg_type.type == TYPE_SLICE_VAR
		) {
			gcc_jit_type *ptr_type = gcc_jit_type_get_pointer(gen_type(
				cg,
				scope->tc.types[arg_type.slice.base],
				&scope->tc,
				err
			));
			if(*err) goto RET;

			arg_builder.count = 0;
			string_builder_printf(&arg_builder, err, "%s.ptr", cg->identifiers[arg_id]);
			if(*err) goto RET;

			gcc_jit_param *ptr = gcc_jit_context_new_param(
				cg->gcc,
				gen_loc(cg->gcc, arg.debug.debug_info),
				ptr_type,
				arg_builder.str
			);

			dynarr_push(&arg_gccs, &ptr, err);
			if(*err) goto RET;

			arg_builder.count = 0;
			string_builder_printf(&arg_builder, err, "%s.len", cg->identifiers[arg_id]);
			if(*err) goto RET;

			gcc_jit_param *len = gcc_jit_context_new_param(
				cg->gcc,
				gen_loc(cg->gcc, arg.debug.debug_info),
				gcc_jit_context_get_type(cg->gcc, GCC_JIT_TYPE_UINT64_T),
				arg_builder.str
			);

			dynarr_push(&arg_gccs, &len, err);
			if(*err) goto RET;
		} else {
			gcc_jit_type *arg_type_gcc = gen_type(cg, arg_type, &scope->tc, err);
			if(*err) goto RET;

			gcc_jit_param *arg_gcc = gcc_jit_context_new_param(
				cg->gcc,
				gen_loc(cg->gcc, arg.debug.debug_info),
				arg_type_gcc,
				cg->identifiers[arg_id]
			);

			dynarr_push(&arg_gccs, &arg_gcc, err);
			if(*err) goto RET;
		}
	}

	*sig = (FnSig) {
		.id = id,
		.linkage_name = linkage_name,
		.ret = ret,
		.arg_count = args.count,
		.args = args.data,
		.arg_ids = arg_ids.data,
	};

	fn = gcc_jit_context_new_function(
		cg->gcc,
		gen_loc(cg->gcc, cg->nodes[i].debug.debug_info),
		!imported ? GCC_JIT_FUNCTION_EXPORTED : GCC_JIT_FUNCTION_IMPORTED,
		ret_gcc,
		!imported ? cg->identifiers[id] : cg->strings[linkage_name],
		arg_gccs.count,
		arg_gccs.data,
		false
	);

RET:
	if(*err) {
		dynarr_clean(&args);
		dynarr_clean(&arg_ids);
		dynarr_clean(&arg_gccs);
	}
	if(arg_builder.str) free(arg_builder.str);

	return fn;
}

static gcc_jit_rvalue *gen_cast(CodeGen *cg, Expr expr, Type type, TypeContext *tc, const DebugInfo *loc, Error *err)
{
	gcc_jit_rvalue *new;

	switch(expr.type.type) {
	case TYPE_PRIMITIVE_U8:
	case TYPE_PRIMITIVE_U16:
	case TYPE_PRIMITIVE_U32:
	case TYPE_PRIMITIVE_U64:
	case TYPE_PRIMITIVE_S8:
	case TYPE_PRIMITIVE_S16:
	case TYPE_PRIMITIVE_S32:
	case TYPE_PRIMITIVE_S64:
		new = gcc_jit_context_new_cast(
			cg->gcc,
			gen_loc(cg->gcc, *loc),
			expr.expr,
			gen_type(cg, type, tc, err)
		);
		if(*err) goto RET;
		break;

	case TYPE_POINTER_CONST:
	case TYPE_POINTER_ABYSS:
	case TYPE_POINTER_VAR:
		if(!types_are_compatible(tc, expr.type, type)) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, tc,
				"Cannot Cast Pointer of Type '%t' to Type '%t' at %l\n",
				expr.type,
				type,
				loc
			);
			*err =  ERROR_UNEXPECTED_DATA;
			goto RET;
		}
		if(tc->types[expr.type.pointer.base].type == TYPE_ARRAY) {
			Type slice = (Type) {
				.slice =  {
					.type = TYPE_SLICE_CONST + (expr.type.type - TYPE_POINTER_CONST),
					.base = tc->types[expr.type.pointer.base].array.base,
				},
			};

			Type ptr_to_elem = types_get_ptr(
				tc,
				tc->types[slice.slice.base],
				expr.type.type
			);

			gcc_jit_type *u64_type = gen_type(cg, (Type) {.type = TYPE_PRIMITIVE_U64}, tc, err);
			if(*err) goto RET;

			gcc_jit_rvalue *(rvalues[2]);
			rvalues[0] = gcc_jit_context_new_cast(
				cg->gcc,
				gen_loc(cg->gcc, *loc),
				expr.expr,
			  	gen_type(cg, ptr_to_elem, tc, err)
			);
			if(*err) goto RET;

			rvalues[1] = gcc_jit_context_new_rvalue_from_long(
				cg->gcc, 	
				u64_type,
				tc->types[expr.type.pointer.base].array.len
			);

			new = gcc_jit_context_new_struct_constructor(
				cg->gcc,
				NULL,
				gen_type(cg, slice, tc, err),
				2,
				NULL,
				rvalues
			);
			goto RET;
		}
		new = expr.expr;
		break;

	default:
		wyrt_diag(
			stderr, cg->identifiers, cg->strings, tc,
			"Cannot Cast Value of Type '%t' to Type '%t' at %l\n",
			expr.type,
			type,
			loc
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

RET:
	return new;
}	


static Lvalue gen_lvalue(CodeGen *cg, size_t index, Scope *scope, Error *err);
static Expr gen_fn_call(CodeGen *cg, AstNode expr, Scope *scope, Error *err);

static Expr gen_expr(CodeGen *cg, Type expected, size_t index, Scope *scope, Error *err)
{
	AstNode expr = cg->nodes[index];
	Expr ret;

	expected = type_resolve(&scope->tc, expected);

	switch(expr.type) {
	case AST_INT_LIT:
		if(expr.int_lit.val <= UINT8_MAX) {
			ret.type.type = TYPE_PRIMITIVE_U8;
		} else if(expr.int_lit.val <= UINT16_MAX) {
			ret.type.type = TYPE_PRIMITIVE_U16;
		} else if(expr.int_lit.val <= UINT32_MAX) {
			ret.type.type = TYPE_PRIMITIVE_U32;
		} else {
			ret.type.type = TYPE_PRIMITIVE_U64;
		}

		ret.expr = gcc_jit_context_new_rvalue_from_long(
			cg->gcc,
			gen_type(cg, ret.type, &scope->tc, err),
			expr.int_lit.val
		);
		if(*err) goto RET;
		break;

	case AST_IDENT: {
		for(size_t i = 0; i < scope->var_count; i++) {
			if(scope->vars[i].id == expr.ident.id) {
				if(!scope->vars[i].declared) {
					wyrt_diag(
						stderr, cg->identifiers, cg->strings, &scope->tc,
					   "Cannot use variable '%i' before it is declared at %l\n",
				   		expr.ident.id,
				 		&expr.debug.debug_info
					);		
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}
				ret.expr = gcc_jit_lvalue_as_rvalue(scope->gcc_vars[i]);
				ret.type = scope->vars[i].type;
				goto RET;
			}
		}
		for(size_t i = 0; i < scope->param_count; i++) {
			if(scope->params[i].id == expr.ident.id) {
				ret.expr = scope->gcc_params[i];
				ret.type = scope->params[i].type;
				goto RET;
			}	
		}
		wyrt_diag(
			stderr, cg->identifiers, cg->strings, &scope->tc,
			"Undeclared variable '%i' at %l\n",
			expr.ident.id,
			&expr.debug.debug_info
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	} break;

	case AST_MUL:
	case AST_DIV:
	case AST_ADD:
	case AST_SUB: {
		Expr lhs = gen_expr(cg, expected, expr.binop.lhs, scope, err);
		if(*err) goto RET;

		Expr rhs = gen_expr(cg, expected, expr.binop.rhs, scope, err);
		if(*err) goto RET;

		bool rhs_compatible = types_are_compatible(&scope->tc, rhs.type, lhs.type);
		bool lhs_compatible = types_are_compatible(&scope->tc, lhs.type, rhs.type);

		if(!lhs_compatible && !rhs_compatible) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"Canot Perform Arithmetic on Incompatible Types '%t' and '%t' at %l\n",
				lhs.type,
				rhs.type,
				&expr.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		if(!type_is_arithmetic(lhs.type)) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"Cannot Perform Arithmetic on non-arithmetic Type '%t' at %l\n",
				lhs.type,
				&expr.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		if(!type_is_arithmetic(rhs.type)) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"Cannot Perform Arithmetic on non-arithmetic Type '%t' at %l\n",
				rhs.type,
				&expr.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		if(rhs_compatible) {
			ret.type = lhs.type;
			rhs.expr = gcc_jit_context_new_cast(
				cg->gcc,
				gen_loc(cg->gcc, expr.debug.debug_info),
				rhs.expr,
				gen_type(cg, lhs.type, &scope->tc, err)
			);
			if(*err) goto RET;
		} else {
			ret.type = rhs.type;
			lhs.expr = gcc_jit_context_new_cast(
				cg->gcc,
				gen_loc(cg->gcc, expr.debug.debug_info),
				lhs.expr,
				gen_type(cg, rhs.type, &scope->tc, err)
			);
		}


		enum gcc_jit_binary_op op;
		switch(expr.type) {
		case AST_MUL: op = GCC_JIT_BINARY_OP_MULT; break;
		case AST_DIV: op = GCC_JIT_BINARY_OP_DIVIDE; break;
		case AST_ADD: op = GCC_JIT_BINARY_OP_PLUS; break;
		case AST_SUB: op = GCC_JIT_BINARY_OP_MINUS; break;
		default: assert(0);
		}

		ret.expr = gcc_jit_context_new_binary_op(
			cg->gcc,
			gen_loc(cg->gcc, expr.debug.debug_info),
			op,
			gen_type(cg, ret.type, &scope->tc, err),
			lhs.expr,
			rhs.expr
		);
		if(*err) goto RET;
	} break;

	case AST_FN_CALL:
		ret = gen_fn_call(cg, expr, scope, err);
		if(*err) goto RET;
		break;

	case AST_DEREF: {
		Type ptr_type = types_get_ptr(&scope->tc, expected, TYPE_POINTER_CONST);
		Expr ptr = gen_expr(cg, ptr_type, expr.deref.ptr, scope, err);
		if(*err) goto RET;
		
		ret.expr = gcc_jit_lvalue_as_rvalue(gcc_jit_rvalue_dereference(
				ptr.expr,
				gen_loc(cg->gcc, expr.debug.debug_info)
		));
		ret.type = expected;
	} break;
	
	case AST_ADDR: {
		Lvalue val = gen_lvalue(cg, expr.addr.base, scope, err);
		if(*err) goto RET;

		ret.expr = gcc_jit_lvalue_get_address(
			val.lvalue,
			gen_loc(cg->gcc, expr.debug.debug_info)
		);

		ret.type = types_get_ptr(
			&scope->tc,
			val.type,
			val.read ? (val.mut ? TYPE_POINTER_VAR : TYPE_POINTER_CONST) : TYPE_POINTER_ABYSS
		);
	} break;

	case AST_ARRAY_LIT: {
		if(expected.type != TYPE_ARRAY) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"Cannot Coerce Array Literal to non-Array Type '%t' at %l\n",
				expected,
				&expr.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
		if(expected.array.len && expected.array.len != expr.array_lit.elem_count) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"Cannot Coerce Array Literal of Length %z to Array of Length %z at %l\n",
				expr.array_lit.elem_count,
				expected.array.len,
				&expr.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		Type elem_type = scope->tc.types[expected.array.base];

		gcc_jit_rvalue **elems = malloc(sizeof(*elems) * expr.array_lit.elem_count);
		CHECK_MALLOC(elems);

		for(size_t i = 0; i < expr.array_lit.elem_count; i++) {
			elems[i] = gen_expr(
				cg,
				elem_type,
				expr.array_lit.elems[i],
				scope,
				err
			).expr;
			if(*err) goto ARRAY_LIT_CLEAN;
		}

		ret.expr = gcc_jit_context_new_array_constructor(
			cg->gcc,
			gen_loc(cg->gcc, expr.debug.debug_info),
			gen_type(cg, expected, &scope->tc, err),
			expected.array.len,
			elems
		);
		ret.type = (Type) {
			.array = {
				.type = TYPE_ARRAY,
				.base = expected.array.base,
			   	.len = expr.array_lit.elem_count	
			},
		};

ARRAY_LIT_CLEAN:
		free(elems);
		goto RET;
	} break;

	case AST_SUBSCRIPT: {
		Expr arr = gen_expr(
			cg,
			(Type) {.type = TYPE_NONE},
			expr.subscript.arr,
			scope,
			err
		);
		if(*err) goto RET;
		if(!type_is_subscriptable(&scope->tc, arr.type)) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"Cannot Subscript non-Subscriptable Type '%t' at %l\n",
				arr.type,
				&expr.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
		if(arr.type.type == TYPE_SLICE_ABYSS
			|| arr.type.type == TYPE_POINTER_ABYSS
		) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"Cannot Read Data from abyssal Pointer at %l\n",
				&expr.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		Expr index = gen_expr(
			cg,
			(Type) {.type = TYPE_PRIMITIVE_U64},
			expr.subscript.index,
			scope,
			err
		);
		if(*err) goto RET;

		gcc_jit_rvalue *ptr = arr.expr;
		gcc_jit_location *loc = gen_loc(cg->gcc, expr.debug.debug_info);

		if(arr.type.type == TYPE_SLICE_CONST
			|| arr.type.type == TYPE_SLICE_VAR
		) {
			gcc_jit_type *slice_gcc = get_named_type(cg, arr.type, &scope->tc, err);
			if(*err) goto RET;

			gcc_jit_struct *slice_struct = gcc_jit_type_is_struct(slice_gcc);

			ptr = gcc_jit_rvalue_access_field(
				arr.expr,
				loc,
				gcc_jit_struct_get_field(slice_struct, 0)
			);
		}

		ret.expr = gcc_jit_lvalue_as_rvalue(gcc_jit_context_new_array_access(
			cg->gcc,
			loc,
			ptr,
			index.expr
		));

		ret.type = scope->tc.types[arr.type.pointer.base];
	} break;

	case AST_STRUCT_LIT: {
		if(!expected.type) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"Cannot Instantiate Anonymous struct Literal without any Destination Type at %l\n",
				&expr.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
		if(expected.type != TYPE_STRUCT) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"Cannot Coerce Struct-Literal to non-struct Type '%t' at %l\n",
				expected,
				&expr.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		gcc_jit_rvalue **members = malloc(sizeof(*members) * expected.struct_type.member_count);
		CHECK_MALLOC(members);
		gcc_jit_field **fields = malloc(sizeof(*fields) * expected.struct_type.member_count);
		if(!fields) {
			fprintf(stderr, "OOM!\n");
			free(members);
			*err = ERROR_OUT_OF_MEMORY;
			goto RET;
		}

		gcc_jit_type *type = gen_type(
			cg,
			expected,
			&scope->tc,
			err
		);
		if(*err) {
			free(members);
			free(fields);
			goto RET;
		}

		gcc_jit_struct *struct_gcc = gcc_jit_type_is_struct(type);
		assert(struct_gcc);

		for(size_t i = 0; i < expected.struct_type.member_count; i++) {
			members[i] = gen_expr(
				cg,
				scope->tc.types[expected.struct_type.member_types[i]],
				expr.struct_lit.member_values[i],
				scope,
				err
			).expr;
			if(*err) {
				free(members);
				free(fields);
				goto RET;
			}

			fields[i] = gcc_jit_struct_get_field(struct_gcc, i);
		}

		ret.expr = gcc_jit_context_new_struct_constructor(
			cg->gcc,
			gen_loc(cg->gcc, expr.debug.debug_info),
			type,
			expected.struct_type.member_count,
			fields,
			members
		);

		ret.type = expected;

		free(members);
	} break;

	case AST_STRUCT_ACCESS: {
		Expr parent = gen_expr(
			cg,
			(Type) {.type = TYPE_NONE},
			expr.struct_access.parent,
			scope,
			err
		);
		if(*err) goto RET;
		if(parent.type.type != TYPE_STRUCT) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"Cannot Access Member of non-struct Type '%t' at %l\n",
				parent,
				&expr.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		gcc_jit_type *parent_type = gen_type(
			cg,
			parent.type,
			&scope->tc,
			err
		);
		if(*err) goto RET;

		gcc_jit_struct *struct_gcc = gcc_jit_type_is_struct(parent_type);
		assert(struct_gcc);

		gcc_jit_field *field = NULL;
		for(size_t i = 0; i < parent.type.struct_type.member_count; i++) {
			if(parent.type.struct_type.member_name_ids[i] == expr.struct_access.member_id) {
				field = gcc_jit_struct_get_field(struct_gcc, i);
				ret.type = scope->tc.types[parent.type.struct_type.member_types[i]];
			}
		}
		if(!field) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"No Members '%i' in struct '%t' at %l\n",
				expr.struct_access.member_id,
				parent.type,
				&expr.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		ret.expr = gcc_jit_rvalue_access_field(
			parent.expr,
			gen_loc(cg->gcc, expr.debug.debug_info),
			field
		);

	} break;

	case AST_STRING_LIT:
	case AST_ZSTRING_LIT: {
		ret.type = types_get_ptr(&scope->tc, (Type) {.type = TYPE_PRIMITIVE_U8}, TYPE_POINTER_CONST);
		ret.type.type = TYPE_SLICE_CONST;

		gcc_jit_type *slice = gen_type(cg, ret.type, &scope->tc, err);
		if(*err) goto RET;

		gcc_jit_struct *struct_gcc = gcc_jit_type_is_struct(slice);
		assert(struct_gcc);

		gcc_jit_rvalue *(vals[2]);
		vals[0] = gcc_jit_context_new_string_literal(
			cg->gcc,
			cg->strings[expr.string_lit.id]
		);

		vals[1] = gcc_jit_context_new_rvalue_from_long(
			cg->gcc,
			gcc_jit_context_get_type(cg->gcc, GCC_JIT_TYPE_UINT64_T),
			strlen(cg->strings[expr.string_lit.id])
		);

		gcc_jit_field *(fields[2]) = {
			gcc_jit_struct_get_field(struct_gcc, 0),
			gcc_jit_struct_get_field(struct_gcc, 1),
		};

		ret.expr = gcc_jit_context_new_struct_constructor(
			cg->gcc,
			gen_loc(cg->gcc, expr.debug.debug_info),
			slice,
			2,
			fields,
			vals
		);
	} break;

	case AST_CSTRING_LIT: {
		ret.type = types_get_ptr(&scope->tc, (Type) {.type = TYPE_PRIMITIVE_U8}, TYPE_POINTER_CONST);

		gcc_jit_type *type = gen_type(cg, ret.type, &scope->tc, err);
		if(*err) goto RET;

		gcc_jit_rvalue *str = gcc_jit_context_new_string_literal(
			cg->gcc,
			cg->strings[expr.string_lit.id]
		);
		
		ret.expr = gcc_jit_context_new_cast(
			cg->gcc,
			gen_loc(cg->gcc, expr.debug.debug_info),
			str,
			type
		);
	} break;

	case AST_ARROW: {
		Expr parent = gen_expr(
			cg,
			(Type) {.type = TYPE_NONE},
			expr.struct_access.parent,
			scope,
			err
		);
		if(*err) goto RET;
		if(parent.type.type != TYPE_POINTER_CONST
			&& parent.type.type != TYPE_POINTER_ABYSS
			&& parent.type.type != TYPE_POINTER_VAR
		) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"Cannot Dereference non-pointer Type '%t' at %l\n",
				parent.type,
				&expr.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		Type parent_struct = scope->tc.types[parent.type.pointer.base];
		parent_struct = type_resolve(&scope->tc, parent_struct);

		if(parent_struct.type != TYPE_STRUCT) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"Cannot Access Member of non-struct Type '%t' at %l\n",
				parent_struct,
				&expr.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		gcc_jit_type *parent_type = gen_type(
			cg,
			parent_struct,
			&scope->tc,
			err
		);
		if(*err) goto RET;

		gcc_jit_struct *struct_gcc = gcc_jit_type_is_struct(parent_type);
		assert(struct_gcc);

		gcc_jit_field *field = NULL;
		for(size_t i = 0; i < parent_struct.struct_type.member_count; i++) {
			if(parent_struct.struct_type.member_name_ids[i] == expr.struct_access.member_id) {
				field = gcc_jit_struct_get_field(struct_gcc, i);
				ret.type = scope->tc.types[parent_struct.struct_type.member_types[i]];
			}
		}
		if(!field) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"No Members '%i' in struct '%t' at %l\n",
				expr.struct_access.member_id,
				parent.type,
				&expr.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		ret.expr = gcc_jit_lvalue_as_rvalue(gcc_jit_rvalue_dereference_field(
			parent.expr,
			gen_loc(cg->gcc, expr.debug.debug_info),
			field
		));
	} break;

	default:
		wyrt_diag(
			stderr, cg->identifiers, cg->strings, &scope->tc,
			"Expected Expression at %l\n",
			&expr.debug.debug_info
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}


RET:
	if(!*err) {
		types_register_nexist(&scope->tc, ret.type, err);
		if(*err) goto RET_FAIL;
		if(expected.type && !types_are_compatible(&scope->tc, ret.type, expected)) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"Cannot Coerce between Expression Type '%t' and Expected '%t' at %l\n",
				ret.type,
				expected,
				&expr.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET_FAIL;
		} else if(expected.type && !types_are_equal(ret.type, expected)) {
			ret.expr = gen_cast(cg, ret, expected, &scope->tc, &expr.debug.debug_info, err);
			ret.type = expected;
		}

		ret.type = type_resolve(&scope->tc, ret.type);
	}
RET_FAIL:
	return ret;
}

static Expr gen_fn_call(CodeGen *cg, AstNode expr, Scope *scope, Error *err)
{
	Expr ret;
	gcc_jit_rvalue **args = NULL;

	bool found = false;
	FnSig sig;
	gcc_jit_function *fn;
	for(size_t i = 0; i < cg->fn_count; i++) {
		if(cg->fn_sigs[i].id == expr.fn_call.fn_id) {
			found = true;
			sig = cg->fn_sigs[i];
			fn = cg->fns[i]; 
			break;
		}
	}
	if(!found) {
		wyrt_diag(
			stderr, cg->identifiers, NULL, NULL,
			"No Function '%i' at %l\n",
			expr.fn_call.fn_id,
			&expr.debug.debug_info
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}
	
	if(expr.fn_call.arg_count != sig.arg_count) {
		wyrt_diag(
			stderr, cg->identifiers, cg->strings, &scope->tc,
			"Expected %z arguments to function call, found %z at %l\n",
			sig.arg_count,
			expr.fn_call.arg_count,
			&expr.debug.debug_info
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;	
	}

	args = malloc(gcc_jit_function_get_param_count(fn) * sizeof(gcc_jit_rvalue*));
	CHECK_MALLOC(args);

	int additional = 0;
	for(size_t i = 0; i < sig.arg_count; i++) {
		Expr arg = gen_expr(cg, sig.args[i], expr.fn_call.args[i], scope, err);
		if(*err) goto RET;
		if(arg.type.type == TYPE_SLICE_CONST
			|| arg.type.type == TYPE_SLICE_ABYSS
			|| arg.type.type == TYPE_SLICE_VAR
		) {
			additional += 1;

			gcc_jit_location *loc = gen_loc(
				cg->gcc,
				cg->nodes[expr.fn_call.args[i]].debug.debug_info
			);

			gcc_jit_type *slice_type = get_named_type(cg, arg.type, &scope->tc, err);
			if(*err) goto RET;

			gcc_jit_struct *slice_struct = gcc_jit_type_is_struct(slice_type);

			gcc_jit_field *ptr = gcc_jit_struct_get_field(slice_struct, 0);
			gcc_jit_field *len = gcc_jit_struct_get_field(slice_struct, 1);

			args[i+additional-1] = gcc_jit_rvalue_access_field(
				arg.expr,
				loc,
				ptr
			);

			args[i+additional] = gcc_jit_rvalue_access_field(
				arg.expr,
				loc,
				len
			);
		} else {
			args[i+additional] = arg.expr;
		}
	}

	ret.expr = gcc_jit_context_new_call(
		cg->gcc,
		gen_loc(cg->gcc, expr.debug.debug_info),
		fn,
		sig.arg_count+additional,
		args
	);
	ret.type = sig.ret;

RET:
	if(args) free(args);
	return ret;
}

static gcc_jit_lvalue *gen_var_decl(
	CodeGen *cg,
	const AstNode *statement,
	gcc_jit_function *fn,
	Var *var,
	Scope *scope,
	Error *err
)
{
	gcc_jit_lvalue *gcc_var = NULL;

	Type type = type_from_ast(&scope->tc, cg->nodes, statement->var_decl.data_type, err);
	if(*err) goto RET;

	if(type.type == TYPE_ARRAY) {
		if(type.array.len == 0) {
			if(!statement->var_decl.initial) {
				wyrt_diag(
					stderr, cg->identifiers, cg->strings, &scope->tc,
					"Cannot Infer Length of Array '%i' when no Initializer is present at %l\n",
					statement->var_decl.id,
					&statement->debug.debug_info
				);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			if(cg->nodes[statement->var_decl.initial].type != AST_ARRAY_LIT) {
				wyrt_diag(
					stderr, cg->identifiers, cg->strings, &scope->tc,
					"Cannot Initialize Array '%i' with Value that is not an array literal at %l\n",
					statement->var_decl.id,
					&statement->debug.debug_info
				);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			type.array.len = cg->nodes[statement->var_decl.initial].array_lit.elem_count;
		}
	}

	gcc_jit_type *gcc_type = gen_type(cg, type, &scope->tc, err);
	if(*err) goto RET;

	*var = (Var) {
		.id = statement->var_decl.id,
		.type = type,
		.mut = statement->var_decl.mut,
		.declared = false,
	};

	gcc_var = gcc_jit_function_new_local(
		fn,
		gen_loc(cg->gcc, statement->debug.debug_info),
		gcc_type,
		cg->identifiers[statement->var_decl.id]
	);


RET:
	return gcc_var;
}

static Lvalue gen_lvalue(CodeGen *cg, size_t index, Scope *scope, Error *err)
{
	Lvalue ret = { 0 };
	AstNode var = cg->nodes[index];

	switch(var.type) {
	case AST_IDENT: {
		for(size_t i = 0; i < scope->var_count; i++) {
			if(scope->vars[i].id == var.ident.id) {
				if(!scope->vars[i].declared) {
					wyrt_diag(
						stderr, cg->identifiers, cg->strings, &scope->tc,
						"Cannot Assign to Variable '%i' at %l before it is Declared!\n",
						var.ident.id,
						&var.debug.debug_info
					);
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}

				ret.type = scope->vars[i].type;
				ret.lvalue = scope->gcc_vars[i];
				ret.mut = scope->vars[i].mut;
				ret.read = true;
				goto RET;
			}
		}

		wyrt_diag(
			stderr, cg->identifiers, cg->strings, &scope->tc,
			"Cannot Assign to Undeclared Variable '%i' at %l\n",
			var.ident.id,
			&var.debug.debug_info
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	} break;	

	case AST_DEREF: {
		Expr ptr = gen_expr(cg, (Type) {.type = TYPE_NONE}, var.deref.ptr, scope, err);
		if(*err) goto RET;

		if(ptr.type.type != TYPE_POINTER_ABYSS
			&& ptr.type.type != TYPE_POINTER_VAR
		) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"Cannot Dereference non-Pointer Type '%t' at %l\n",
				ptr.type,
				&var.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		ret.lvalue = gcc_jit_rvalue_dereference(ptr.expr, gen_loc(cg->gcc, var.debug.debug_info));
		ret.type = scope->tc.types[ptr.type.pointer.base];
		ret.mut = !(ptr.type.type == TYPE_POINTER_CONST);
		ret.read = !(ptr.type.type == TYPE_POINTER_ABYSS);
	} break;

	case AST_STRUCT_ACCESS: {
		Lvalue parent = gen_lvalue(
			cg,
			var.struct_access.parent,
			scope,
			err
		);
		if(*err) goto RET;	

		if(parent.type.type != TYPE_STRUCT) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"Cannot Access Member in non-struct Type '%t' at %l\n",
				parent.type,
				&var.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		gcc_jit_type *parent_type = gen_type(
			cg,
			parent.type,
			&scope->tc,
			err
		);
		if(*err) goto RET;

		gcc_jit_struct *struct_gcc = gcc_jit_type_is_struct(parent_type);
		assert(struct_gcc);

		gcc_jit_field *field = NULL;
		for(size_t i = 0; i < parent.type.struct_type.member_count; i++) {
			if(parent.type.struct_type.member_name_ids[i] == var.struct_access.member_id) {
				field = gcc_jit_struct_get_field(struct_gcc, i);
				ret.type = scope->tc.types[parent.type.struct_type.member_types[i]];
			}
		}

		if(!field) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"No Member '%i' in struct '%t' at %l\n",
				var.struct_access.member_id,
				parent.type,
				&var.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		ret.lvalue = gcc_jit_lvalue_access_field(
			parent.lvalue,
			gen_loc(cg->gcc, var.debug.debug_info),
			field
		);
		ret.mut = parent.mut;
		ret.read = parent.read;
	} break;

	case AST_SUBSCRIPT: {
		Expr arr = gen_expr(
			cg,
			(Type) {.type = TYPE_NONE},
			var.subscript.arr,
			scope,
			err
		);
		if(*err) goto RET;

		if(!type_is_subscriptable(&scope->tc, arr.type)) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"Cannot subscript non-Subscriptable Type '%t' at %l\n",
				arr.type,
				&var.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		Expr index = gen_expr(
			cg,
			(Type) {.type = TYPE_PRIMITIVE_U64},
			var.subscript.index,
			scope,
			err
		);
		if(*err) goto RET;

		gcc_jit_rvalue *arr_gcc = arr.expr;
		if(arr.type.type == TYPE_SLICE_CONST
			|| arr.type.type == TYPE_SLICE_ABYSS
			|| arr.type.type == TYPE_SLICE_VAR
		) {
			gcc_jit_type *slice_gcc = gen_type(
				cg,
				arr.type,
				&scope->tc,
				err
			);
			if(*err) goto RET;

			gcc_jit_struct *struct_gcc = gcc_jit_type_is_struct(slice_gcc);
			assert(struct_gcc);

			gcc_jit_field *ptr = gcc_jit_struct_get_field(struct_gcc, 0);

			arr_gcc = gcc_jit_rvalue_access_field(
				arr.expr,
				gen_loc(cg->gcc, var.debug.debug_info),
				ptr
			);
		}

		ret.lvalue = gcc_jit_context_new_array_access(
			cg->gcc,
			gen_loc(cg->gcc, var.debug.debug_info),
			arr_gcc,
			index.expr
		);
		switch(arr.type.type) {
		case TYPE_POINTER_CONST:
		case TYPE_SLICE_CONST:
			ret.mut = false;
			ret.read = true;
			break;
		case TYPE_POINTER_ABYSS:
		case TYPE_SLICE_ABYSS:
			ret.mut = true;
			ret.read = false;
			break;
		case TYPE_POINTER_VAR:
		case TYPE_SLICE_VAR:
			ret.mut = true;
			ret.read = true;
			break;
		case TYPE_ARRAY: {
			Lvalue lval = gen_lvalue(cg, var.subscript.arr, scope, err);
			if(*err) goto RET;
			ret.mut = lval.mut;
			ret.read = lval.read;
		} break;
		default: assert(0);
		}
		ret.type = scope->tc.types[arr.type.pointer.base];
	} break;

	case AST_ARROW: {
		Expr parent = gen_expr(
			cg,
			(Type) {.type = TYPE_NONE},
			var.struct_access.parent,
			scope,
			err
		);
		if(*err) goto RET;	

		if(parent.type.type != TYPE_POINTER_CONST
			&& parent.type.type != TYPE_POINTER_ABYSS
			&& parent.type.type != TYPE_POINTER_VAR
		) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"Cannot Dereference non-pointer Type '%t' at %l\n",
				parent.type,
				&var.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		Type parent_struct = scope->tc.types[parent.type.pointer.base];

		parent_struct = type_resolve(&scope->tc, parent_struct);

		if(parent_struct.type != TYPE_STRUCT) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"Cannot Access Member in non-struct Type '%t' at %l\n",
				parent_struct,
				&var.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		gcc_jit_type *parent_type = gen_type(
			cg,
			parent_struct,
			&scope->tc,
			err
		);
		if(*err) goto RET;

		gcc_jit_struct *struct_gcc = gcc_jit_type_is_struct(parent_type);
		assert(struct_gcc);

		gcc_jit_field *field = NULL;
		for(size_t i = 0; i < parent_struct.struct_type.member_count; i++) {
			if(parent_struct.struct_type.member_name_ids[i] == var.struct_access.member_id) {
				field = gcc_jit_struct_get_field(struct_gcc, i);
				ret.type = scope->tc.types[parent_struct.struct_type.member_types[i]];
			}
		}

		if(!field) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"No Member '%i' in struct '%t' at %l\n",
				var.struct_access.member_id,
				parent_struct,
				&var.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		ret.lvalue = gcc_jit_rvalue_dereference_field(
			parent.expr,
			gen_loc(cg->gcc, var.debug.debug_info),
			field
		);
		ret.mut = !(parent.type.type == TYPE_POINTER_CONST);
		ret.read = !(parent.type.type == TYPE_POINTER_ABYSS);
	} break;
	
	default:
		wyrt_diag(
			stderr, cg->identifiers, cg->strings, &scope->tc,
			"Expected Lvalue at %l\n",
			&var.debug.debug_info
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
		break;	
	}

RET:
	if(!*err) {
		types_register_nexist(&scope->tc, ret.type, err);
		if(*err) goto RET_FAIL;
	}
RET_FAIL:
	return ret;
}

static void gen_fn(CodeGen *cg, FnSig sig, size_t index, gcc_jit_function *fn, const Scope *global, Error *err)
{
	const AstNode def = cg->nodes[index];
	assert(def.type == AST_FN_DEF);
	
	const AstNode block = cg->nodes[def.fn_def.block];
	if(block.type == AST_EXTERN) return;
	
	DynArr gcc_vars;
	DynArr vars;
	dynarr_init(&gcc_vars, sizeof(gcc_jit_lvalue*));
	dynarr_init(&vars, sizeof(Var));

	Scope scope;
	scope_init(&scope, global, err);
	if(*err) goto RET;

	//TODO: SLICE ARGUMENTS
	scope.gcc_params = malloc(sig.arg_count * sizeof(gcc_jit_rvalue*));	
	CHECK_MALLOC(scope.gcc_params);
	scope.params = malloc(sig.arg_count * sizeof(Var));
	CHECK_MALLOC(scope.params);
	scope.param_count = sig.arg_count;

	assert(block.type == AST_BLOCK);

	size_t additional = 0;
	for(size_t i = 0; i < sig.arg_count; i++) {
		if(sig.args[i].type == TYPE_SLICE_CONST
			|| sig.args[i].type == TYPE_SLICE_ABYSS
			|| sig.args[i].type == TYPE_SLICE_VAR
		) {
			gcc_jit_rvalue *(values[2]);
			
			values[0] = gcc_jit_param_as_rvalue(
				gcc_jit_function_get_param(fn, i+additional)
			);	

			values[1] = gcc_jit_param_as_rvalue(
				gcc_jit_function_get_param(fn, i+additional+1)
			);

			gcc_jit_type *slice_gcc = gen_type(cg, sig.args[i], &scope.tc, err);
			if(*err) goto RET;

			gcc_jit_struct *slice_struct = gcc_jit_type_is_struct(slice_gcc);
			gcc_jit_field *(fields[2]);
			fields[0] = gcc_jit_struct_get_field(slice_struct, 0);
			fields[1] = gcc_jit_struct_get_field(slice_struct, 1);

			scope.gcc_params[i] = gcc_jit_context_new_struct_constructor(
				cg->gcc,
				gen_loc(cg->gcc, def.debug.debug_info),
				slice_gcc,
				2,
				fields,
				values
			);

			additional += 1;
		} else {
			scope.gcc_params[i] = gcc_jit_param_as_rvalue(
				gcc_jit_function_get_param(fn, i)
			);	
		}

		scope.params[i] = (Var) {
			.id = sig.arg_ids[i],
			.type = sig.args[i],
			.mut = false,
			.declared = true,
		};
	}

	for(size_t i = 0; i < block.block.statement_count; i++) {
		const AstNode statement = cg->nodes[block.block.statements[i]];

		if(statement.type == AST_VAR_DECL) {
			Var var;
			gcc_jit_lvalue *gcc_var = gen_var_decl(cg, &statement, fn, &var, &scope, err);
			if(*err) {
				dynarr_clean(&vars);
				dynarr_clean(&gcc_vars);
				goto RET;
			}

			dynarr_push(&gcc_vars, &gcc_var, err);
			if(*err) {
				dynarr_clean(&vars);
				dynarr_clean(&gcc_vars);
				goto RET;
			}

			dynarr_push(&vars, &var, err);
			if(*err) {
				dynarr_clean(&vars);
				dynarr_clean(&gcc_vars);
				goto RET;
			}
		}
	}

	scope.gcc_vars = gcc_vars.data;
	scope.vars = vars.data;
	scope.var_count = vars.count;

	gcc_jit_block *gcc_block = gcc_jit_function_new_block(fn, ".entry");

	size_t varnum = 0;
	bool returned = false;
	for(size_t i = 0; i < block.block.statement_count; i++) {
		const AstNode statement = cg->nodes[block.block.statements[i]];

		switch(statement.type) {
		case AST_DISCARD: {
			Expr expr = gen_expr(
				cg,
				(Type) {.type = TYPE_NONE},
				statement.discard.value,
				&scope,
				err
			);
			if(*err) goto RET;
						
			gcc_jit_block_add_eval(
				gcc_block,
				gen_loc(cg->gcc, statement.debug.debug_info),
				expr.expr
			);
		} break;

		case AST_FN_CALL: {
			bool found = false;
			for(size_t i = 0; i < cg->fn_count; i++) {
				if(cg->fn_sigs[i].id == statement.fn_call.fn_id) {
					if(cg->fn_sigs[i].ret.type != TYPE_PRIMITIVE_VOID) {
						wyrt_diag(
							stderr, cg->identifiers, cg->strings, &scope.tc,
							"Cannot implicitly discard return value of function '%i' at %l, "
							"Consider using the 'discard' keyword\n",
							statement.fn_call.fn_id,
							&statement.debug.debug_info
						);
						*err = ERROR_UNEXPECTED_DATA;
						goto RET;
					}
					Expr val = gen_fn_call(cg, statement, &scope, err);
					if(*err) goto RET;
					gcc_jit_block_add_eval(
						gcc_block,
						gen_loc(cg->gcc, statement.debug.debug_info),
						val.expr
					);
					found = true;
					break;
				}
			}

			if(found) break;

			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope.tc,
				"Undeclared Function '%i' at %l\n",
				statement.fn_call.fn_id,
				&statement.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		} break;

		case AST_VAR_DECL: {
			if(statement.var_decl.initial) {
				Expr expr = gen_expr(
					cg,
					scope.vars[i].type,
					statement.var_decl.initial,
					&scope,
					err
				);
				if(*err) goto RET;

				gcc_jit_block_add_assignment(
					gcc_block,
					gen_loc(cg->gcc, statement.debug.debug_info),
					scope.gcc_vars[i],
					expr.expr
				);
			} else {
				if(!scope.vars[varnum].mut) {
					fprintf(stderr, "Error: const Variable uninitialized at ");
					lexer_print_debug_to_file(stderr, &statement.debug.debug_info);
					fprintf(stderr, "\n");
					*err = ERROR_UNDEFINED; // Prevent malformed program
					goto RET;
				}
			}
			scope.vars[varnum].declared = true;
			varnum += 1;
		} break;
		case AST_ASSIGN: {
			Lvalue lhs = gen_lvalue(
				cg,
				statement.assign.var,
				&scope,
				err
			);
			if(*err) goto RET;

			if(!lhs.mut) {
				wyrt_diag(
					stderr, cg->identifiers, cg->strings, &scope.tc,
					"Cannot assign to const value at %l\n",
					&statement.debug.debug_info
				);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			Expr rhs = gen_expr(
				cg,
				lhs.type,
				statement.assign.expr,
				&scope,
				err
			);
			if(*err) goto RET;

			gcc_jit_block_add_assignment(
				gcc_block,
				gen_loc(cg->gcc, statement.debug.debug_info),
				lhs.lvalue,
				rhs.expr
			);
		} break;

		case AST_ADD_ASSIGN:
		case AST_SUB_ASSIGN:
		case AST_MUL_ASSIGN:
		case AST_DIV_ASSIGN: {
			enum gcc_jit_binary_op op;
			switch(statement.type) {
			case AST_ADD_ASSIGN: op = GCC_JIT_BINARY_OP_PLUS; break;
			case AST_SUB_ASSIGN: op = GCC_JIT_BINARY_OP_MINUS; break;
			case AST_MUL_ASSIGN: op = GCC_JIT_BINARY_OP_MULT; break;
			case AST_DIV_ASSIGN: op = GCC_JIT_BINARY_OP_DIVIDE; break;
			default: assert(0);
			}

			Lvalue lhs = gen_lvalue(
				cg,
				statement.assign.var,
				&scope,
				err
			);
			if(*err) goto RET;

			if(!lhs.mut || !lhs.read) {
				wyrt_diag(
					stderr, cg->identifiers, cg->strings, &scope.tc,
					"Cannot Perform Compound Assignment on non-'var' Value at %l\n",
					&statement.debug.debug_info
				);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			Expr rhs = gen_expr(
				cg,
				lhs.type,
				statement.assign.expr,
				&scope,
				err
			);
			if(*err) goto RET;

			gcc_jit_block_add_assignment_op(
				gcc_block,
				gen_loc(cg->gcc, statement.debug.debug_info),
				lhs.lvalue,
				op,
				rhs.expr
			);
		} break;
		
		case AST_RET:
			returned = true;
			if(sig.ret.type == TYPE_PRIMITIVE_VOID) {
				if(statement.ret.return_val) {
					fprintf(stderr, "Cannot Return a Value from a void function at ");
					lexer_print_debug_to_file(stderr, &statement.debug.debug_info);
					fprintf(stderr, "\n");
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}

				gcc_jit_block_end_with_void_return(
					gcc_block,
					gen_loc(cg->gcc, statement.debug.debug_info)
				);
			} else {
				Expr val = gen_expr(cg, sig.ret, statement.ret.return_val, &scope, err);
				if(*err) goto RET;

				gcc_jit_block_end_with_return(
					gcc_block,
					gen_loc(cg->gcc, statement.debug.debug_info),
					val.expr		
				);
			}
			break;

		default:
			fprintf(stderr, "Invalid Statement at ");
			lexer_print_debug_to_file(stderr, &statement.debug.debug_info);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
	}

	if(!returned) {
		if(sig.ret.type != TYPE_PRIMITIVE_VOID) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope.tc,
				"Non-Void Function '%i' does not return a value!\n",
				sig.id
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		gcc_jit_block_end_with_void_return(
			gcc_block,
			NULL
		);
	}

RET:
	return;
}

void codegen_gen(CodeGen *cg, GenType gen_type, const char *path, const char *ir_dump, Error *err)
{
	assert(cg->nodes[0].type == AST_MODULE);
	DynArr sigs;
	dynarr_init(&sigs, sizeof(FnSig));
	DynArr fns;
	dynarr_init(&fns, sizeof(gcc_jit_function*));

	Scope global;
	scope_init(&global, NULL, err);
	if(*err) goto RET;
	
	const AstNode module = cg->nodes[0];

	for(size_t i = 0; i < module.module.statement_count; i++) {
		size_t index = module.module.statements[i];
		if(cg->nodes[index].type == AST_TYPEDEF) {
			Type backing = type_from_ast(&global.tc, cg->nodes, cg->nodes[index].typdef.backing, err);
			if(*err) goto RET;

			size_t type_index = SIZE_MAX;
			for(size_t j = 0; j < global.tc.count; j++) {
				if(types_are_equal(backing, global.tc.types[j])) {
					type_index = j;
					break;
				}
			}
			assert(type_index != SIZE_MAX);

			Type t = (Type) {
				.typdef = {
					.type = TYPE_TYPEDEF,
					.id = cg->nodes[index].typdef.id,
					.backing = type_index,
				},
			};

			types_register(&global.tc, t, err);
			if(*err) goto RET;
		}
	}

	for(size_t i = 0; i < module.module.statement_count; i++) {
		size_t index = module.module.statements[i];
		if(cg->nodes[index].type == AST_FN_DEF) {
			dynarr_alloc(&sigs, 1, err);
			if(*err) {
				dynarr_clean(&sigs);
				dynarr_clean(&fns);
				goto RET;
			}
			dynarr_alloc(&fns, 1, err);
			if(*err) {
				dynarr_clean(&sigs);
				dynarr_clean(&fns);
				goto RET;
			}
			
			FnSig *sig = dynarr_from_back(&sigs, 0);
			*(gcc_jit_function**)dynarr_from_back(&fns, 0) = gen_fnsig(cg, sig, &global, index, err);
			
			if(*err) {
				dynarr_clean(&sigs);
				dynarr_clean(&fns);
				goto RET;
			}
		}	
	}
	cg->fn_sigs = sigs.data;
	cg->fns = fns.data;
	cg->fn_count = sigs.count;

	size_t fnnum = 0;
	for(size_t i = 0; i < module.module.statement_count; i++) {
		size_t index = module.module.statements[i];
		switch(cg->nodes[index].type) {
		case AST_FN_DEF:
			gen_fn(cg, cg->fn_sigs[fnnum], index, cg->fns[fnnum], &global, err);
			if(*err) goto RET;
			fnnum += 1;
			break;

		case AST_TYPEDEF: {
			Type backing = type_from_ast(
				&global.tc,
				cg->nodes,
				cg->nodes[index].typdef.backing,
				err		
			);
			if(*err) goto RET;
			types_register(&global.tc, backing, err);
			if(*err ) goto RET;
		} break;

		default:
			fprintf(stderr, "Illegal File-Scope Statement at ");
			lexer_print_debug_to_file(stderr, &cg->nodes[i].debug.debug_info);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
	}

	if(ir_dump) {
		gcc_jit_context_dump_to_file(cg->gcc, ir_dump, false);
	}

	switch(gen_type) {
	case GEN_EXE:
		gcc_jit_context_compile_to_file(
			cg->gcc,
			GCC_JIT_OUTPUT_KIND_EXECUTABLE,
			path
		);
		break;
	case GEN_SHR:
		gcc_jit_context_compile_to_file(
			cg->gcc,
			GCC_JIT_OUTPUT_KIND_DYNAMIC_LIBRARY,
			path
		);
		break;
	case GEN_OBJ:
		gcc_jit_context_compile_to_file(
			cg->gcc,
			GCC_JIT_OUTPUT_KIND_OBJECT_FILE,
			path
		);
		break;
	case GEN_ASM:	
		gcc_jit_context_compile_to_file(
			cg->gcc,
			GCC_JIT_OUTPUT_KIND_ASSEMBLER,
			path
		);
		break;
	}

RET:
	return;
}

void scope_init(Scope *scope, const Scope *parent, Error *err)
{
	if(parent) {
		scope->params = malloc(parent->param_count * sizeof(Var));
		CHECK_MALLOC(scope->params);
		scope->param_count = parent->param_count;
		scope->vars = malloc(parent->var_count * sizeof(Var));
		CHECK_MALLOC(scope->vars);
		scope->var_count = parent->var_count;

		memcpy(scope->params, parent->params, parent->param_count * sizeof(Var));
		memcpy(scope->vars, parent->vars, parent->var_count * sizeof(Var));

		types_copy(&scope->tc, &parent->tc, err);
		if(*err) goto RET;
	} else {
		scope->params = NULL;
		scope->vars = NULL;
		scope->param_count = 0;
		scope->var_count = 0;

		types_init(&scope->tc, err);
		if(*err) goto RET;
	}

RET:
	return;
}

void scope_clean(const Scope *scope)
{
	if(scope->params) free(scope->params);
	if(scope->vars) free(scope->vars);
	if(scope->tc.types) free(scope->tc.types);
}
