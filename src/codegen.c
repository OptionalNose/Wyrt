#include "codegen.h"

#include <string.h>
#include <assert.h>

#include "ui.h"

#ifdef _WIN32
void *LoadLibraryA(const char *);
void *GetProcAddress(void *, const char *);
int FreeLibrary(void *);
#else
#include <dlfcn.h>
#endif

typedef struct {
	WyrtRvalue expr;
	Type type;
} Expr;

typedef struct {
	WyrtLvalue lvalue;
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
	size_t string_count,
	char const *dlpath,
	Error *err
)
{
#ifdef _WIN32
	void *dl = LoadLibraryA(dlpath);
	if(!dl) {
		fprintf(stderr, "Could not load '%s' as a backend\n", dlpath);
		*err = ERROR_IO;
		goto RET;
	}

	WyrtBackend *be = GetProcAddress(dl, "wyrtBackend");
	if(!be) {
		fprintf(stderr, "Could not load Backend\n");
		*err = ERROR_IO;
		goto RET;
	}
#else
	void *dl = dlopen(dlpath, RTLD_NOW);
	if(!dl) {
		fprintf(stderr, "Could not load '%s' as a backend\n", dlpath);
		*err = ERROR_IO;
		goto RET;
	}

	WyrtBackend *be = dlsym(dl, "wyrtBackend");
	if(!be) {
		fprintf(stderr, "Could not load Backend\n");
		*err = ERROR_IO;
		goto RET;
	}
#endif

	WyrtContext ctx = be->get_ctx(err);
	if(*err) goto RET;

	*cg = (CodeGen) {
		.nodes = nodes,
		.node_count = node_count,
		.identifiers = identifiers,
		.strings = strings,
		.string_count = string_count,
		.fn_count = 0,
		.fn_sigs = NULL,
		.fns = NULL,
		.dl = dl,
		.be = *be,
		.ctx = ctx,
	};
RET:
	if(*err) *cg = (CodeGen) { 0 };
	return;
}

void codegen_clean(const CodeGen *cg)
{
	for(size_t i = 0; i < cg->fn_count; i++) {
		free(cg->fn_sigs[i].args);
		free(cg->fn_sigs[i].arg_ids);
	}
	free(cg->fn_sigs);
	free(cg->fns);
	if(cg->be.release_ctx) cg->be.release_ctx(cg->ctx);

#ifdef _WIN32
	if(cg->dl) FreeLibrary(cg->dl);
#else
	if(cg->dl) dlclose(cg->dl);
#endif
}

static WyrtFunction *gen_fnsig(
	CodeGen *cg,
	FnSig *sig,
	Scope *scope,
	size_t i,
	Error *err
)
{
	WyrtFunction fn = NULL;
	DynArr args;
	DynArr arg_ids;
	DynArr arg_bes;
	dynarr_init(&args, sizeof(Type));	
	dynarr_init(&arg_ids, sizeof(size_t));
	dynarr_init(&arg_bes, sizeof(WyrtParam));

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

	size_t additional = 0;
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
			char ptr_name[2+8+1];
			char len_name[2+8+1];

			snprintf(ptr_name, 2+8+1, ".p%08zi", additional);
			snprintf(len_name, 2+8+1, ".l%08zi", additional);

			Type ptr = arg_type;
			ptr.type -= TYPE_SLICE_CONST - TYPE_POINTER_CONST;

			WyrtParam arg_ptr_be = cg->be.new_param(
				cg->ctx,
				&arg.debug.debug_info,
				ptr,
				&scope->tc,
				ptr_name,
				err
			);
			if(*err) goto RET;

			WyrtParam arg_len_be = cg->be.new_param(
				cg->ctx,
				&arg.debug.debug_info,
				(Type) {.type = TYPE_PRIMITIVE_U64},
				&scope->tc,
				len_name,
				err
			);
			if(*err) goto RET;

			dynarr_push(&arg_bes, &arg_ptr_be, err);
			if(*err) goto RET;
			dynarr_push(&arg_bes, &arg_len_be, err);
			if(*err) goto RET;

			additional += 1;
		} else {
			WyrtParam arg_be = cg->be.new_param(
				cg->ctx,
				&arg.debug.debug_info,
				arg_type,
				&scope->tc,
				cg->identifiers[arg_id],
				err
			);
			if(*err) goto RET;

			dynarr_push(&arg_bes, &arg_be, err);
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

	fn = cg->be.new_function(
		cg->ctx,
		&cg->nodes[i].debug.debug_info,
		ret,
		&scope->tc,
		(WyrtParam*)arg_bes.data,
		arg_bes.count,
		imported,
		imported ? cg->strings[linkage_name] : cg->identifiers[id],
		err
	);
	if(*err) goto RET;
		
RET:
	if(*err) {
		dynarr_clean(&args);
		dynarr_clean(&arg_ids);
	}
	dynarr_clean(&arg_bes);

	return fn;
}

static Lvalue gen_lvalue(CodeGen *cg, size_t index, Scope *scope, Error *err);
static Expr gen_fn_call(CodeGen *cg, AstNode expr, Scope *scope, Error *err);

static WyrtRvalue gen_cast(
	CodeGen *cg,
	Expr expr,
	Type type,
	TypeContext *tc,
	const DebugInfo *loc,
	Error *err
)
{
	WyrtRvalue *new;

	switch(expr.type.type) {
	case TYPE_PRIMITIVE_U8:
	case TYPE_PRIMITIVE_U16:
	case TYPE_PRIMITIVE_U32:
	case TYPE_PRIMITIVE_U64:
	case TYPE_PRIMITIVE_S8:
	case TYPE_PRIMITIVE_S16:
	case TYPE_PRIMITIVE_S32:
	case TYPE_PRIMITIVE_S64:
		new = cg->be.new_cast(
			cg->ctx,
			loc,
			expr.expr,
			type,
			tc,
			err
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

			WyrtRvalue rvalues[2];
			rvalues[0] = cg->be.new_cast(
				cg->ctx,
				loc,
				expr.expr,
				ptr_to_elem,
				tc,
				err
			);
			if(*err) goto RET;

			rvalues[1] = cg->be.rvalue_int_lit(
				cg->ctx,
				tc->types[expr.type.pointer.base].array.len,
				TYPE_PRIMITIVE_U64,
				err
			);
			if(*err) goto RET;

			new = cg->be.rvalue_struct_lit(
				cg->ctx,
				loc,
				slice,
				tc,
				rvalues,
				2,
				err
			);
			if(*err) goto RET;
			goto RET;
		}
		new = expr.expr;
		break;
	case TYPE_PAUL_CONST:
	case TYPE_PAUL_ABYSS:
	case TYPE_PAUL_VAR:
		if(!types_are_compatible(tc, expr.type, type)) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, tc,
				"Cannot cast Pointer to Array of Unknown Length '%t' to pointer of type '%t' at %l\n",
				expr.type,
				type,
				loc
			);
			*err = ERROR_UNEXPECTED_DATA;
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

		ret.expr = cg->be.rvalue_int_lit(cg->ctx, expr.int_lit.val, ret.type.type, err);
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
				ret.expr = cg->be.rvalue_from_lvalue(scope->be_vars[i]);
				ret.type = scope->vars[i].type;
				goto RET;
			}
		}
		for(size_t i = 0; i < scope->param_count; i++) {
			if(scope->params[i].id == expr.ident.id) {
				ret.expr = scope->be_params[i];
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

	case AST_COMP_EQ:
	case AST_COMP_GE:
	case AST_COMP_LE:
	case AST_COMP_NE:
	case AST_COMP_GT:
	case AST_COMP_LT:
	case AST_LOGIC_AND:
	case AST_LOGIC_OR:
	case AST_MUL:
	case AST_DIV:
	case AST_ADD:
	case AST_SUB: {
		Expr lhs, rhs;
		if(expr.type == AST_COMP_EQ
			|| expr.type == AST_COMP_GE
			|| expr.type == AST_COMP_LE
			|| expr.type == AST_COMP_NE
			|| expr.type == AST_COMP_GT
			|| (
				expected.type == TYPE_PAUL_CONST
				|| expected.type == TYPE_PAUL_ABYSS
				|| expected.type == TYPE_PAUL_VAR	
				|| expected.type == TYPE_POINTER_CONST
				|| expected.type == TYPE_POINTER_ABYSS
				|| expected.type == TYPE_POINTER_VAR
			)
		) {
			lhs = gen_expr(cg, (Type) {.type = TYPE_NONE}, expr.binop.lhs, scope, err);
			if(*err) goto RET;

			rhs = gen_expr(cg, (Type) {.type = TYPE_NONE}, expr.binop.rhs, scope, err);
			if(*err) goto RET;
		} else {
			lhs = gen_expr(cg, expected, expr.binop.lhs, scope, err);
			if(*err) goto RET;

			rhs = gen_expr(cg, expected, expr.binop.rhs, scope, err);
			if(*err) goto RET;
		}

		bool rhs_compatible = types_are_compatible(&scope->tc, rhs.type, lhs.type);
		bool lhs_compatible = types_are_compatible(&scope->tc, lhs.type, rhs.type);

		if(expected.type != TYPE_PAUL_CONST
			&& expected.type != TYPE_PAUL_ABYSS
			&& expected.type != TYPE_PAUL_VAR
			&& expected.type != TYPE_POINTER_CONST
			&& expected.type != TYPE_POINTER_ABYSS
			&& expected.type != TYPE_POINTER_VAR
		) {
			if(!lhs_compatible && !rhs_compatible) {
				wyrt_diag(
					stderr, cg->identifiers, cg->strings, &scope->tc,
					"Cannot Perform Arithmetic on Incompatible Types '%t' and '%t' at %l\n",
					lhs.type,
					rhs.type,
					&expr.debug.debug_info
				);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
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
			if(expr.type == AST_COMP_EQ
				|| expr.type == AST_COMP_GE
				|| expr.type == AST_COMP_LE
				|| expr.type == AST_COMP_NE
				|| expr.type == AST_COMP_GT
				|| expr.type == AST_COMP_LT
			) {
				ret.type = (Type) {.type = TYPE_PRIMITIVE_BOOL};
			} else {
				ret.type = lhs.type;
			}
			rhs.expr = cg->be.new_cast(cg->ctx, &expr.debug.debug_info, rhs.expr, lhs.type, &scope->tc, err);
			if(*err) goto RET;

			ret.expr = cg->be.rvalue_binary_op(
				cg->ctx,
				&expr.debug.debug_info,
				expr.type,
				ret.type,
				&scope->tc,
				lhs.expr,
				rhs.expr,
				err
			);
			if(*err) goto RET;
		} else if(expected.type == TYPE_PAUL_CONST
			|| expected.type == TYPE_PAUL_ABYSS
			|| expected.type == TYPE_PAUL_VAR
			|| expected.type == TYPE_POINTER_CONST
			|| expected.type == TYPE_POINTER_ABYSS
			|| expected.type == TYPE_POINTER_VAR
		) {	
			
			WyrtRvalue ptr;
			WyrtRvalue offset;
			Type sign;
			if(lhs.type.type == TYPE_PAUL_CONST
				|| lhs.type.type == TYPE_PAUL_ABYSS
				|| lhs.type.type == TYPE_PAUL_VAR
			) {
				if(rhs.type.type < TYPE_PRIMITIVE_U8 || rhs.type.type > TYPE_PRIMITIVE_S64) {
					wyrt_diag(
						stderr, cg->identifiers, cg->strings, &scope->tc,
						"Cannot add two pointers '%t' and '%t' together at %l\n",
						lhs.type,
						rhs.type,
						&expr.debug.debug_info
					);
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}

				ptr = lhs.expr;
				ret.type = lhs.type;
				offset = rhs.expr;
				if(rhs.type.type >= TYPE_PRIMITIVE_U8 && rhs.type.type <= TYPE_PRIMITIVE_U64) {
					sign = rhs.type;
					sign.type += TYPE_PRIMITIVE_S8 - TYPE_PRIMITIVE_U8;
					offset = cg->be.new_cast(cg->ctx, &expr.debug.debug_info, offset, sign, &scope->tc, err);
					if(*err) goto RET;
				}
			} else if(rhs.type.type == TYPE_PAUL_CONST
				|| rhs.type.type == TYPE_PAUL_ABYSS
				|| rhs.type.type == TYPE_PAUL_VAR
			) {
				ptr = rhs.expr;
				ret.type = rhs.type;
				offset = lhs.expr;	
				if(lhs.type.type >= TYPE_PRIMITIVE_U8 && lhs.type.type <= TYPE_PRIMITIVE_U64) {
					sign = lhs.type;
					sign.type += TYPE_PRIMITIVE_S8 - TYPE_PRIMITIVE_U8;
					offset = cg->be.new_cast(cg->ctx, &expr.debug.debug_info, offset, sign, &scope->tc, err);
					if(*err) goto RET;
				}
			} else {
				wyrt_diag(
					stderr, cg->identifiers, cg->strings, &scope->tc,
					"Cannot get pointer from arithmetic between non-pointer types '%t' and '%t' at %l\n",
					lhs.type,
					rhs.type,
					&expr.debug.debug_info
				);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			switch(expr.type) {
			case AST_ADD:
				break;
			case AST_SUB: {
				WyrtRvalue neg = cg->be.rvalue_int_lit(cg->ctx, -1, sign.type, err);
				if(*err) goto RET;

				offset = cg->be.rvalue_binary_op(
					cg->ctx,
					&expr.debug.debug_info,
					AST_MUL,
					sign,
					&scope->tc,
					offset,
					neg,
					err
				);
				if(*err) goto RET;
			} break;
			default:
				wyrt_diag(
					stderr, cg->identifiers, cg->strings, &scope->tc,
					"Illegal operation between pointer type '%t' and integer type '%t' at %l\n",
					lhs.type,
					rhs.type,
					&expr.debug.debug_info
				);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			WyrtLvalue elem = cg->be.lvalue_subscript(cg->ctx, &expr.debug.debug_info, ptr, offset, err);
			if(*err) goto RET;

			ret.expr = cg->be.rvalue_address(cg->ctx, &expr.debug.debug_info, elem, err);
			if(*err) goto RET;
		} else {
			if(expr.type == AST_COMP_EQ
				|| expr.type == AST_COMP_GE
				|| expr.type == AST_COMP_LE
				|| expr.type == AST_COMP_NE
				|| expr.type == AST_COMP_GT
				|| expr.type == AST_COMP_LT
			) {
				ret.type = (Type) {.type = TYPE_PRIMITIVE_BOOL};
			} else {
				ret.type = rhs.type;
			}
			lhs.expr = cg->be.new_cast(cg->ctx, &expr.debug.debug_info, lhs.expr, rhs.type, &scope->tc, err);
			if(*err) goto RET;

			ret.expr = cg->be.rvalue_binary_op(
				cg->ctx,
				&expr.debug.debug_info,
				expr.type,
				ret.type,
				&scope->tc,
				lhs.expr,
				rhs.expr,
				err
			);
			if(*err) goto RET;
		}

	} break;

	case AST_LOGIC_NOT: {
		Expr val = gen_expr(cg, expected, expr.unary_op.val, scope, err);
		if(*err) goto RET;

		if(!type_is_arithmetic(val.type)
			&& !(
				val.type.type == TYPE_POINTER_CONST
				|| val.type.type == TYPE_POINTER_ABYSS
				|| val.type.type == TYPE_POINTER_VAR
		)) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"Cannot Perform Arithmetic on non-arithmetic Type '%t' at %l\n",
				val.type,
				&expr.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		ret.expr = cg->be.rvalue_unary_op(
			cg->ctx,
			&expr.debug.debug_info,
			expr.type,
			TYPE_PRIMITIVE_BOOL,
			val.expr,
			err
		);
		if(*err) goto RET;

		ret.type = (Type) {TYPE_PRIMITIVE_BOOL};
	} break;

	case AST_FN_CALL:
		ret = gen_fn_call(cg, expr, scope, err);
		if(*err) goto RET;
		break;

	case AST_DEREF: {
		Type ptr_type = types_get_ptr(&scope->tc, expected, TYPE_POINTER_CONST);
		Expr ptr = gen_expr(cg, ptr_type, expr.unary_op.val, scope, err);
		if(*err) goto RET;
		
		WyrtLvalue *lval = cg->be.lvalue_deref(cg->ctx, &expr.debug.debug_info, ptr.expr, err);
		if(*err) goto RET;
		ret.expr = cg->be.rvalue_from_lvalue(lval);
		ret.type = expected;
	} break;
	
	case AST_ADDR: {
		Lvalue val = gen_lvalue(cg, expr.unary_op.val, scope, err);
		if(*err) goto RET;

		ret.expr = cg->be.rvalue_address(cg->ctx, &expr.debug.debug_info, val.lvalue, err);
		if(*err) goto RET;

		ret.type = types_get_ptr(
			&scope->tc,
			val.type,
			val.read ? (val.mut ? TYPE_POINTER_VAR : TYPE_POINTER_CONST) : TYPE_POINTER_ABYSS
		);
	} break;

	case AST_CHAR_LIT: {
		ret.expr = cg->be.rvalue_int_lit(cg->ctx, expr.char_lit.val, TYPE_PRIMITIVE_U8, err);
		if(*err) goto RET;
		ret.type.type = TYPE_PRIMITIVE_U8;
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

		WyrtRvalue *elems = malloc(sizeof(*elems) * expr.array_lit.elem_count);
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

		ret.expr = cg->be.rvalue_array_lit(
			cg->ctx,
			&expr.debug.debug_info,
			expected,
			&scope->tc,
			elems,
			expr.array_lit.elem_count,
			err
		);
		if(*err) goto RET;

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

		if(arr.type.type == TYPE_SLICE_CONST
			|| arr.type.type == TYPE_SLICE_VAR
		) {
			arr.expr = cg->be.rvalue_field(
				cg->ctx,
				&expr.debug.debug_info,
				arr.expr,
				arr.type,
				&scope->tc,
				0,
				err
			);
			if(*err) goto RET;
			arr.type.type -= TYPE_SLICE_CONST - TYPE_POINTER_CONST;
		}

		WyrtLvalue subs = cg->be.lvalue_subscript(
			cg->ctx,
			&expr.debug.debug_info,
			arr.expr,
			index.expr,
			err
		);
		if(*err) goto RET;

		ret.expr = cg->be.rvalue_from_lvalue(subs);
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

		if(expr.struct_lit.member_count > expected.struct_type.member_count) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"Cannot Coerce Struct-Literal with %zi members to struct Type '%t' with %zi members at %l\n",
				expr.struct_lit.member_count,
				expected,
				expected.struct_type.member_count,
				&expr.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		WyrtRvalue *members = malloc(sizeof(*members) * expected.struct_type.member_count);
		CHECK_MALLOC(members);

		for(size_t i = 0; i < expr.struct_lit.member_count; i++) {
			bool found = false;
			size_t id = expr.struct_lit.member_name_ids[i];
			for(size_t j = 0; j < expected.struct_type.member_count; j++) {
				if(expected.struct_type.member_name_ids[j] == id) {
					members[j] = gen_expr(
						cg,
						scope->tc.types[expected.struct_type.member_types[i]],
						expr.struct_lit.member_values[i],
						scope,
						err
					).expr;
					if(*err) {
						free(members);
						goto RET;
					}
					found = true;
					break;
				}
			}

			if(!found) {
				wyrt_diag(
					stderr, cg->identifiers, cg->strings, &scope->tc,
					"No member '%i' in Struct-Type '%t' at %l\n",
					id,
					expected
				);
				*err = ERROR_UNEXPECTED_DATA;
				free(members);
				goto RET;
			}
		}

		for(size_t i = 0; i < expected.struct_type.member_count; i++) {
			if(!members[i]) {
				members[i] = cg->be.rvalue_null(
					cg->ctx,
					scope->tc.types[expected.struct_type.member_types[i]],
					&scope->tc,
					err
				);

				if(*err) {
					free(members);
					goto RET;
				}
			}
		}

		ret.expr = cg->be.rvalue_struct_lit(
			cg->ctx,
			&expr.debug.debug_info,
			expected,
			&scope->tc,
			members,
			expected.struct_type.member_count,
			err
		);
		if(*err) goto RET;
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

		ret.expr = NULL;
		switch(parent.type.type) {
		case TYPE_STRUCT:
			for(size_t i = 0; i < parent.type.struct_type.member_count; i++) {
				if(parent.type.struct_type.member_name_ids[i] == expr.struct_access.member_id) {
					ret.expr = cg->be.rvalue_field(
						cg->ctx,
						&expr.debug.debug_info,
						parent.expr,
						parent.type,
						&scope->tc,
						i,
						err
					);
					if(*err) goto RET;
					ret.type = scope->tc.types[parent.type.struct_type.member_types[i]];
				}
			}

			if(!ret.expr) {
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
			break;

		case TYPE_SLICE_CONST:
		case TYPE_SLICE_ABYSS:
		case TYPE_SLICE_VAR:
			if(expr.struct_access.member_id == 10) {
				ret.expr = cg->be.rvalue_field(
					cg->ctx,
					&expr.debug.debug_info,
					parent.expr,
					parent.type,
					&scope->tc,
					0,
					err
				);
				if(*err) goto RET;
				ret.type = parent.type;
				ret.type.type += TYPE_PAUL_CONST - TYPE_SLICE_CONST;
			} else if(expr.struct_access.member_id == 11) {
				ret.expr = cg->be.rvalue_field(
					cg->ctx,
					&expr.debug.debug_info,
					parent.expr,
					parent.type,
					&scope->tc,
					0,
					err
				);
				if(*err) goto RET;
				ret.type.type = TYPE_PRIMITIVE_U64;
			} else {
				wyrt_diag(
					stderr, cg->identifiers, cg->strings, &scope->tc,
					"No Member '%i' in slice at %l\n",
					expr.struct_access.member_id,
					&expr.debug.debug_info
				);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
			break;

		default:
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &scope->tc,
				"Cannot Access Member of non-struct and non-slice Type %t at %l\n",
				parent.type,
				&expr.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

	} break;

	case AST_STRING_LIT:
	case AST_ZSTRING_LIT: {
		ret.type = types_get_ptr(&scope->tc, (Type) {.type = TYPE_PRIMITIVE_U8}, TYPE_SLICE_CONST);

		WyrtRvalue vals[2];
		vals[0] = cg->be.rvalue_cstring_lit(
			cg->ctx,
			&expr.debug.debug_info,
			cg->strings[expr.string_lit.id],
			err
		);
		if(*err) goto RET;

		vals[1] = cg->be.rvalue_int_lit(
			cg->ctx,
			strlen(cg->strings[expr.string_lit.id]),
			TYPE_PRIMITIVE_U64,
			err
		);
		if(*err) goto RET;

		ret.expr = cg->be.rvalue_struct_lit(
			cg->ctx,
			&expr.debug.debug_info,
			ret.type,
			&scope->tc,
			vals,
			2,
			err
		);
		if(*err) goto RET;
	} break;

	case AST_CSTRING_LIT: {
		ret.type = types_get_ptr(&scope->tc, (Type) {.type = TYPE_PRIMITIVE_U8}, TYPE_PAUL_CONST);

		ret.expr = cg->be.rvalue_cstring_lit(
			cg->ctx,
			&expr.debug.debug_info,
			cg->strings[expr.string_lit.id],
			err
		);
		if(*err) goto RET;	
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

		ret.expr = NULL;
		for(size_t i = 0; i < parent_struct.struct_type.member_count; i++) {
			if(parent_struct.struct_type.member_name_ids[i] == expr.struct_access.member_id) {
				WyrtLvalue *lval = cg->be.lvalue_deref_field(
					cg->ctx,
					&expr.debug.debug_info,
					parent.expr,
					parent_struct,
					&scope->tc,
					i,
					err
				);
				if(*err) goto RET;
				ret.expr = cg->be.rvalue_from_lvalue(lval);
				ret.type = scope->tc.types[parent_struct.struct_type.member_types[i]];
			}
		}
		if(!ret.expr) {
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
	DynArr args;
	dynarr_init(&args, sizeof(WyrtRvalue));

	bool found = false;
	FnSig sig;
	WyrtFunction fn;
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
	
	ret.type = sig.ret;

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

	for(size_t i = 0; i < sig.arg_count; i++) {
		Expr arg = gen_expr(cg, sig.args[i], expr.fn_call.args[i], scope, err);
		if(*err) goto RET;

		if(arg.type.type == TYPE_SLICE_CONST
			|| arg.type.type == TYPE_SLICE_ABYSS
			|| arg.type.type == TYPE_SLICE_VAR
		) {
			WyrtRvalue ptr = cg->be.rvalue_field(
				cg->ctx,
				&expr.debug.debug_info,
				arg.expr,
				arg.type,
				&scope->tc,
				0,
				err
			);
			if(*err) goto RET;

			WyrtRvalue len = cg->be.rvalue_field(
				cg->ctx,
				&expr.debug.debug_info,
				arg.expr,
				arg.type,
				&scope->tc,
				1,
				err
			);
			if(*err) goto RET;
			
			dynarr_push(&args, &ptr, err);
			if(*err) goto RET;

			dynarr_push(&args, &len, err);
			if(*err) goto RET;
		} else {
			dynarr_push(&args, &arg.expr, err);
			if(*err) goto RET;
		}
	}

	ret.expr = cg->be.rvalue_fn_call(
		cg->ctx,
		&expr.debug.debug_info,
		fn,
		args.data,
		args.count,
		err
	);
	if(*err) goto RET;

RET:
	dynarr_clean(&args);
	return ret;
}

static WyrtLvalue gen_var_decl(
	CodeGen *cg,
	const AstNode *statement,
	WyrtBlock *block,
	Var *var,
	Scope *scope,
	Error *err
)
{
	WyrtLvalue be_var = NULL;

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

	*var = (Var) {
		.id = statement->var_decl.id,
		.type = type,
		.mut = statement->var_decl.mut,
		.declared = false,
	};

	be_var = cg->be.block_new_variable(
		cg->ctx,
		&statement->debug.debug_info,
		block,
		type,
		&scope->tc,
		cg->identifiers[statement->var_decl.id],
		err
	);
	if(*err) goto RET;

RET:
	return be_var;
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
				ret.lvalue = scope->be_vars[i];
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
		Expr ptr = gen_expr(cg, (Type) {.type = TYPE_NONE}, var.unary_op.val, scope, err);
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

		ret.lvalue = cg->be.lvalue_deref(
			cg->ctx,
			&var.debug.debug_info,
			ptr.expr,
			err
		);
		if(*err) goto RET;
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

		size_t field = SIZE_MAX;
		for(size_t i = 0; i < parent.type.struct_type.member_count; i++) {
			if(parent.type.struct_type.member_name_ids[i] == var.struct_access.member_id) {
				field = i;
				ret.type = scope->tc.types[parent.type.struct_type.member_types[i]];
				break;
			}
		}

		if(field == SIZE_MAX) {
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

		ret.lvalue = cg->be.lvalue_field(
			cg->ctx,
			&var.debug.debug_info,
			parent.lvalue,
			parent.type,
			&scope->tc,
			field,
			err
		);
		if(*err) goto RET;
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

		ret.lvalue = cg->be.lvalue_subscript(
			cg->ctx,
			&var.debug.debug_info,
			arr.expr,
			index.expr,
			err
		);
		if(*err) goto RET;

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

		size_t field = SIZE_MAX;
		for(size_t i = 0; i < parent_struct.struct_type.member_count; i++) {
			if(parent_struct.struct_type.member_name_ids[i] == var.struct_access.member_id) {
				field = i;
				ret.type = scope->tc.types[parent_struct.struct_type.member_types[i]];
				break;
			}
		}

		if(field == SIZE_MAX) {
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

		ret.lvalue = cg->be.lvalue_deref_field(
			cg->ctx,
			&var.debug.debug_info,
			parent.expr,
			parent_struct,
			&scope->tc,
			field,
			err
		);
		if(*err) goto RET;
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

static void gen_block(
	CodeGen *cg,
	const AstNode *block,
	WyrtBlock *be_block,
	WyrtFunction fn,
	Type ret_type,
	bool *returned,
	const Scope *parent,
	Error *err
);

static void gen_if(
	CodeGen *cg,
	const AstNode *statement,
	WyrtBlock *be_block,
	WyrtFunction fn,
	Type ret_type,
	bool *returned,
	Scope *parent,
	Error *err
)
{
	Scope new;
	scope_init(&new, parent, err);
	if(*err) goto RET;

	WyrtRvalue cond;
	if(statement->if_statement.decl) {
		new.var_count += 1;
		new.vars = realloc(new.vars, sizeof(Var) * sizeof(new.var_count));
		CHECK_MALLOC(new.vars);
		new.be_vars = realloc(new.be_vars, sizeof(WyrtLvalue) * sizeof(new.var_count));
		CHECK_MALLOC(new.be_vars);

		AstNode decl = cg->nodes[statement->if_statement.decl];
		new.vars[new.var_count - 1] = (Var) {
			.id = decl.var_decl.id,
			.type = type_from_ast(&parent->tc, cg->nodes, decl.var_decl.data_type, err),
			.mut = decl.var_decl.mut,
			.declared = true,
		};
		if(*err) goto RET;

		new.be_vars[new.var_count - 1] = cg->be.block_new_variable(
			cg->ctx,
			&decl.debug.debug_info,
			*be_block,
			new.vars[new.var_count - 1].type,
			&parent->tc,
			cg->identifiers[decl.var_decl.id],
			err
		);
		if(*err) goto RET;

		if(!decl.var_decl.initial) {
			wyrt_diag(
				stderr, cg->identifiers, cg->strings, &parent->tc,
				"Declaration in if-statement not initialized at %l\n",
				&decl.debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		Expr init = gen_expr(cg, new.vars[new.var_count - 1].type, decl.var_decl.initial, parent, err);
		if(*err) goto RET;

		cg->be.block_add_assign(
			cg->ctx,
			&decl.debug.debug_info,
			*be_block,
			new.be_vars[new.var_count - 1],
			init.expr,
			err
		);
		if(*err) goto RET;
	
		if(!statement->if_statement.condition) {
			cond = cg->be.rvalue_from_lvalue(new.be_vars[new.var_count - 1]);
		} else {
			cond = gen_expr(cg, (Type) {TYPE_NONE}, statement->if_statement.condition, &new, err).expr;
			if(*err) goto RET;
		}
	} else {
		cond = gen_expr(cg, (Type) {TYPE_NONE}, statement->if_statement.condition, &new, err).expr;
	}

	WyrtBlock true_block = cg->be.new_block(cg->ctx, fn, err);
	if(*err) goto RET;

	WyrtBlock after = cg->be.new_block(cg->ctx, fn, err);
	if(*err) goto RET;

	if(statement->if_statement.else_block) {
		WyrtBlock else_block = cg->be.new_block(cg->ctx, fn, err);
		if(*err) goto RET;

		cg->be.block_end_with_cond(
			cg->ctx,
			&statement->debug.debug_info,
			*be_block,
			cond,
			true_block,
			else_block,
			err
		);
		if(*err) goto RET;
		
		bool true_returns = false;
		gen_block(cg, &cg->nodes[statement->if_statement.block], &true_block, fn, ret_type, &true_returns, &new, err);
		if(*err) goto RET;

		bool false_returns = false;
		gen_block(cg, &cg->nodes[statement->if_statement.else_block], &else_block, fn, ret_type, &false_returns, &new, err);
		if(*err) goto RET;

		if(!true_returns) {
			cg->be.block_end_with_jump(
				cg->ctx,
				&statement->debug.debug_info,
				true_block,
				after,
				err
			);
			if(*err) goto RET;
		}

		if(!false_returns) {
			cg->be.block_end_with_jump(
				cg->ctx,
				&statement->debug.debug_info,
				else_block,
				after,
				err
			);
			if(*err) goto RET;
		}

		*returned = true_returns && false_returns;
	} else {
		cg->be.block_end_with_cond(
			cg->ctx,
			&statement->debug.debug_info,
			*be_block,
			cond,
			true_block,
			after,
			err
		);
		if(*err) goto RET;

		gen_block(cg, &cg->nodes[statement->if_statement.block], &true_block, fn, ret_type, returned, &new, err);
		if(*err) goto RET;

		cg->be.block_end_with_jump(
			cg->ctx,
			&statement->debug.debug_info,
			true_block,
			after,
			err
		);
		if(*err) goto RET;
	}

	*be_block = after;

RET:
	scope_clean(&new);
	return;
}

static void gen_block(
	CodeGen *cg,
	const AstNode *block,
	WyrtBlock *be_block,
	WyrtFunction fn,
	Type ret_type,
	bool *returned,
	const Scope *parent,
	Error *err
)
{
	Scope scope;
	DynArr be_vars;
	dynarr_init(&be_vars, sizeof(WyrtLvalue));
	DynArr vars;
	dynarr_init(&vars, sizeof(Var));

	scope_init(&scope, parent, err);
	if(*err) goto RET;

	for(size_t i = 0; i < block->block.statement_count; i++) {
		const AstNode statement = cg->nodes[block->block.statements[i]];

		if(statement.type == AST_VAR_DECL) {
			Var var;
			WyrtLvalue be_var = gen_var_decl(cg, &statement, *be_block, &var, &scope, err);
			if(*err) {
				dynarr_clean(&vars);
				dynarr_clean(&be_vars);
				goto RET;
			}

			dynarr_push(&be_vars, &be_var, err);
			if(*err) {
				dynarr_clean(&vars);
				dynarr_clean(&be_vars);
				goto RET;
			}

			dynarr_push(&vars, &var, err);
			if(*err) {
				dynarr_clean(&vars);
				dynarr_clean(&be_vars);
				goto RET;
			}
		}
	}

	if(vars.count > 0) {
		scope.be_vars = realloc(scope.be_vars, sizeof(WyrtLvalue) * (scope.var_count + be_vars.count));
		CHECK_MALLOC(scope.be_vars);
		scope.vars = realloc(scope.vars, sizeof(Var) * (scope.var_count + vars.count));
		CHECK_MALLOC(scope.vars);
		memcpy(scope.be_vars + scope.var_count * sizeof(WyrtLvalue), be_vars.data, sizeof(WyrtLvalue) * be_vars.count);
		memcpy(scope.vars + scope.var_count * sizeof(Var), vars.data, sizeof(Var) * vars.count);
		scope.var_count = scope.var_count + vars.count;
	}

	size_t varnum = 0;
	for(size_t i = 0; i < block->block.statement_count; i++) {
		const AstNode statement = cg->nodes[block->block.statements[i]];

		switch(statement.type) {
		case AST_IF: {
			gen_if(cg, &statement, be_block, fn, ret_type, returned, &scope, err);
			if(*err) goto RET;
		} break;

		case AST_DISCARD: {
			Expr expr = gen_expr(
				cg,
				(Type) {.type = TYPE_NONE},
				statement.discard.value,
				&scope,
				err
			);
			if(*err) goto RET;
						
			cg->be.block_add_eval(
				cg->ctx,
				&statement.debug.debug_info,
				*be_block,
				expr.expr,
				err
			);
			if(*err) goto RET;
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
					cg->be.block_add_eval(
						cg->ctx,
						&statement.debug.debug_info,
						*be_block,
						val.expr,
						err
					);
					if(*err) goto RET;
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
					scope.vars[varnum].type,
					statement.var_decl.initial,
					&scope,
					err
				);
				if(*err) goto RET;

				cg->be.block_add_assign(
					cg->ctx,
					&statement.debug.debug_info,
					*be_block,
					scope.be_vars[varnum],
					expr.expr,
					err
				);
				if(*err) goto RET;
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

			cg->be.block_add_assign(
				cg->ctx,
				&statement.debug.debug_info,
				*be_block,
				lhs.lvalue,
				rhs.expr,
				err
			);
			if(*err) goto RET;
		} break;

		case AST_ADD_ASSIGN:
		case AST_SUB_ASSIGN:
		case AST_MUL_ASSIGN:
		case AST_DIV_ASSIGN: {
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

			cg->be.block_add_compound_assign(
				cg->ctx,
				&statement.debug.debug_info,
				*be_block,
				lhs.lvalue,
				rhs.expr,
				statement.type,
				err
			);
			if(*err) goto RET;
		} break;
		
		case AST_RET:
			*returned = true;
			if(ret_type.type == TYPE_PRIMITIVE_VOID) {
				if(statement.ret.return_val) {
					fprintf(stderr, "Cannot Return a Value from a void function at ");
					lexer_print_debug_to_file(stderr, &statement.debug.debug_info);
					fprintf(stderr, "\n");
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}

				cg->be.block_end_with_return(
					cg->ctx,
					&statement.debug.debug_info,
					*be_block,
					NULL,
					err
				);
				if(*err) goto RET;
			} else {
				Expr val = gen_expr(cg, ret_type, statement.ret.return_val, &scope, err);
				if(*err) goto RET;

				cg->be.block_end_with_return(
					cg->ctx,
					&statement.debug.debug_info,
					*be_block,
					val.expr,
					err
				);
				if(*err) goto RET;
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

RET:
	dynarr_clean(&be_vars);
	dynarr_clean(&vars);
	scope_clean(&scope);
	return;
}

static void gen_fn(CodeGen *cg, FnSig sig, size_t index, WyrtFunction fn, const Scope *global, Error *err)
{
	const AstNode def = cg->nodes[index];
	assert(def.type == AST_FN_DEF);
	
	const AstNode block = cg->nodes[def.fn_def.block];
	if(block.type == AST_EXTERN) return;
	
	DynArr be_vars;
	DynArr vars;
	dynarr_init(&be_vars, sizeof(WyrtLvalue));
	dynarr_init(&vars, sizeof(Var));

	Scope scope;
	scope_init(&scope, global, err);
	if(*err) goto RET;

	scope.be_params = malloc(sig.arg_count * sizeof(WyrtRvalue));	
	CHECK_MALLOC(scope.be_params);
	scope.params = malloc(sig.arg_count * sizeof(Var));
	CHECK_MALLOC(scope.params);
	scope.param_count = sig.arg_count;

	assert(block.type == AST_BLOCK);

	size_t additional = 0;
	for(size_t i = 0; i < sig.arg_count; i++) {
		scope.params[i] = (Var) {
			.id = sig.arg_ids[i],
			.type = sig.args[i],
			.mut = false,
			.declared = true,
		};

		if(sig.args[i].type == TYPE_SLICE_CONST
			|| sig.args[i].type == TYPE_SLICE_ABYSS
			|| sig.args[i].type == TYPE_SLICE_VAR
		) {
			WyrtParam ptr = cg->be.function_get_param(
				cg->ctx,
				fn,
				i + additional,
				err
			);
			if(*err) goto RET;

			WyrtParam len = cg->be.function_get_param(
				cg->ctx,
				fn,
				i + additional + 1,
				err
			);
			if(*err) goto RET;

			WyrtRvalue vals[2];
			vals[0] = cg->be.rvalue_from_param(ptr);
			vals[1] = cg->be.rvalue_from_param(len);

			size_t type_refs[2];
			Type ptr_type = (Type) {
				.pointer = {
					.type = TYPE_POINTER_CONST + (sig.args[i].type - TYPE_SLICE_CONST),
					.base = sig.args[i].slice.base,
				},
			};
			type_refs[0] = types_register_nexist(&scope.tc, ptr_type, err);
			if(*err) goto RET;

			Type len_type = (Type) {.type = TYPE_PRIMITIVE_U64};
			type_refs[1] = types_register_nexist(&scope.tc, len_type, err);
			if(*err) goto RET;

			size_t ids[2] = {0, 1};
			Type s = (Type) {
				.struct_type = {
					.type = TYPE_STRUCT,
					.member_types = type_refs,
					.member_name_ids = ids,
					.member_count = 2,
				},
			};

			scope.be_params[i] = cg->be.rvalue_struct_lit(
				cg->ctx,
				NULL,
				s,
				&scope.tc,
				vals,
				2,
				err
			);
			if(*err) goto RET;

			additional += 1;
		} else {
			WyrtParam param = cg->be.function_get_param(
				cg->ctx,
				fn,
				i + additional,
				err
			);
			if(*err) goto RET;
			scope.be_params[i] = cg->be.rvalue_from_param(param);
		}
	}


	WyrtBlock be_block = cg->be.new_block(
		cg->ctx,
		fn,
		err
	);
	if(*err) goto RET;

	bool returned = false;

	gen_block(cg, &block, &be_block, fn, sig.ret, &returned, &scope, err);
	if(*err) goto RET;

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

		cg->be.block_end_with_return(
			cg->ctx,
			NULL,
			be_block,
			NULL,
			err
		);
		if(*err) goto RET;
	}

RET:
	scope_clean(&scope);
	return;
}

void codegen_gen(CodeGen *cg, GenOptions options, const char *path, Error *err)
{
	assert(cg->nodes[0].type == AST_MODULE);
	DynArr sigs;
	dynarr_init(&sigs, sizeof(FnSig));
	DynArr fns;
	dynarr_init(&fns, sizeof(WyrtFunction));

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
			*(WyrtFunction*)dynarr_from_back(&fns, 0) = gen_fnsig(cg, sig, &global, index, err);
			
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

	cg->be.compile(cg->ctx, options, path, err);
	if(*err) goto RET;

RET:
	scope_clean(&global);
	return;
}

void scope_init(Scope *scope, const Scope *parent, Error *err)
{
	*scope = (Scope) { 0 };
	if(parent) {
		if(parent->param_count) {
			scope->param_count = parent->param_count;

			scope->params = malloc(parent->param_count * sizeof(Var));
			CHECK_MALLOC(scope->params);
			memcpy(scope->params, parent->params, parent->param_count * sizeof(Var));

			scope->be_params = malloc(parent->param_count * sizeof(WyrtRvalue));
			CHECK_MALLOC(scope->be_params);
			memcpy(scope->be_params, parent->be_params, parent->param_count * sizeof(WyrtRvalue));
		}
		if(parent->var_count) {
			scope->var_count = parent->var_count;

			scope->vars = malloc(parent->var_count * sizeof(Var));
			CHECK_MALLOC(scope->vars);
			memcpy(scope->vars, parent->vars, parent->var_count * sizeof(Var));

			scope->be_vars = malloc(parent->var_count * sizeof(WyrtRvalue));
			CHECK_MALLOC(scope->be_vars);
			memcpy(scope->be_vars, parent->be_vars, parent->var_count * sizeof(WyrtRvalue));
		}

		types_copy(&scope->tc, &parent->tc, err);
		if(*err) goto RET;
	} else {
		types_init(&scope->tc, err);
		if(*err) goto RET;
	}

RET:
	return;
}

void scope_clean(const Scope *scope)
{
	if(scope->params) free(scope->params);
	if(scope->be_params) free(scope->be_params);
	if(scope->vars) free(scope->vars);
	if(scope->be_vars) free(scope->be_vars);
	if(scope->tc.types) free(scope->tc.types);
}
