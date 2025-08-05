#include "codegen.h"
#include "lexer.h"
#include "parser.h"
#include "types.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

void codegen_init(
	CodeGen	*cg,
	FILE *output,
	const char *target_triple,
	const AstNode *nodes,
	size_t node_count,
	char *const	*identifiers,
	char *const *strings,
	size_t string_count
)
{
	*cg	= (CodeGen)	{
		.output	= output,
		.nodes = nodes,
		.node_count	= node_count,
		.identifiers = identifiers,
		.strings = strings,
		.string_count = string_count,
		.fn_sig_count =	0,
		.fn_sigs = NULL,
		.metadata_counter =	0,
	};
}

void codegen_clean(const CodeGen *cg)
{
	for(size_t i = 0; i	< cg->fn_sig_count;	i++) {
		free(cg->fn_sigs[i].args);
		free(cg->fn_sigs[i].arg_ids);
	}
	if(cg->fn_sigs)	free(cg->fn_sigs);
}

static void print_type(
	FILE *file,
	TypeContext	const *tc,
	Type t,
	Error *err
)
{
	switch(t.type) {
	case TYPE_NONE:
		fprintf(stderr,	"[INTERNAL]: Attempting	to Generate	TYPE_NONE\n");
		*err = ERROR_INTERNAL;
		goto RET;
	case TYPE_PRIMITIVE_U8:
	case TYPE_PRIMITIVE_S8:
		FPUTS_OR_ERR(file, "i8");
		break;
	case TYPE_PRIMITIVE_U16:
	case TYPE_PRIMITIVE_S16:
		FPUTS_OR_ERR(file, "i16");
		break;
	case TYPE_PRIMITIVE_U32:
	case TYPE_PRIMITIVE_S32:
		FPUTS_OR_ERR(file, "i32");
		break;
	case TYPE_PRIMITIVE_U64:
	case TYPE_PRIMITIVE_S64:
		FPUTS_OR_ERR(file, "i64");
		break;
	case TYPE_PRIMITIVE_VOID:
		FPUTS_OR_ERR(file, "void");
		break;
	case TYPE_POINTER_CONST:
	case TYPE_POINTER_ABYSS:
	case TYPE_POINTER_VAR:
		FPUTS_OR_ERR(file, "ptr");
		break;
	case TYPE_ARRAY:
		FPRINTF_OR_ERR(file, "[%zi x ",	t.array.len);
		print_type(file, tc, tc->types[t.array.base], err);
		FPUTS_OR_ERR(file, "]");
		break;
	case TYPE_SLICE_CONST:
	case TYPE_SLICE_ABYSS:
	case TYPE_SLICE_VAR:
		FPUTS_OR_ERR(file, "{ptr, i64}");
		break;
	case TYPE_STRUCT:
		FPUTS_OR_ERR(file, "{");
		for(size_t i = 0; i	< t.struct_type.member_count; i++) {
			print_type(
				file,
				tc,
				tc->types[t.struct_type.member_types[i]],
				err
			);
			if(*err) goto RET;
			if(i ==	t.struct_type.member_count - 1)	{
				FPUTS_OR_ERR(file, "}");
			} else {
				FPUTS_OR_ERR(file, ", ");
			}
		}
		break;
	}

RET:
	return;
}

static Var *find_var(const Scope *scope, size_t	id)
{
	for(size_t i = 0; i	< scope->param_count; i++) {
		if(id == scope->params[i].id) return &scope->params[i];
	}
	for(size_t i = 0; i	< scope->var_count;	i++) {
		if(id == scope->vars[i].id)	return &scope->vars[i];
	}
	return NULL;
}

Type type_of_expr(
	CodeGen const *cg,
	Scope *scope,
	size_t i,
	Error *err
)
{
	Type type;

	AstNode expr = cg->nodes[i];

	switch(expr.type) {
	case AST_IDENT: {
		const Var *var = find_var(scope, expr.ident.id);
		if(!var) {
			fprintf(
				stderr,
				"Use of Undeclared Variable '%s' at ",
				cg->identifiers[expr.ident.id]
			);
			lexer_print_debug_to_file(stderr, &expr.debug.debug_info);
			fprintf(stderr, "\n");
			*err = ERROR_NOT_FOUND;
			goto RET;
		}
		type = var->type;
	} break;

	case AST_INT_LIT: {
		if(expr.int_lit.val <= UINT8_MAX) {
			type.type = TYPE_PRIMITIVE_U8;
			break;
		} else if(expr.int_lit.val <= UINT16_MAX) {
			type.type = TYPE_PRIMITIVE_U16;
			break;
		} else if(expr.int_lit.val <= UINT32_MAX) {
			type.type = TYPE_PRIMITIVE_U32;
			break;
		} else {
			type.type = TYPE_PRIMITIVE_U64;
			break;
		}
	} break;

	case AST_MUL:
	case AST_DIV:
	case AST_ADD:
	case AST_SUB: {
		Type lhs = type_of_expr(
			cg,
			scope,
			expr.binop.lhs,
			err
		);
		if(*err) goto RET;
		Type rhs = type_of_expr(
			cg,
			scope,
			expr.binop.rhs,
			err
		);
		if(*err) goto RET;

		if(types_are_compatible(lhs, rhs)) {
			type = rhs;
			break;
		} else if(types_are_compatible(rhs, lhs)) {
			type = lhs;
			break;
		} else {
			fprintf(
				stderr,
				"Cannot Perform Arithmetic on Non-Compatible Types at "
			);
			lexer_print_debug_to_file(stderr, &expr.debug.debug_info);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
	} break;

	case AST_FN_CALL: {
		bool found = false;
		for(size_t i = 0; i < cg->fn_sig_count; i++) {
			if(cg->fn_sigs[i].id == expr.fn_call.fn_id) {
				found = true;
				type = cg->fn_sigs[i].ret;
				break;
			}
		}

		if(!found) {
			fprintf(stderr, "Cannot Call undeclared Function at ");
			lexer_print_debug_to_file(stderr, &expr.debug.debug_info);
			fprintf(stderr, "\n");
			*err = ERROR_NOT_FOUND;
			goto RET;
		}
	} break;

	case AST_DEREF: {
		Type ptr = type_of_expr(
			cg,
			scope,
			expr.deref.ptr,
			err
		);
		if(*err) goto RET;

		type = scope->tc.types[ptr.pointer.base];
	} break;

	case AST_ADDR: {
		AstNode parent = cg->nodes[expr.addr.base];
		bool seeking = true;
		while(seeking) {
			switch(parent.type) {
			case AST_IDENT: {
				Var *var = find_var(scope, parent.ident.id);
				if(!var) {
					fprintf(
						stderr,
						"Identifier '%s' undeclared at ",
						cg->identifiers[parent.ident.id]
					);
					lexer_print_debug_to_file(stderr, &expr.debug.debug_info);
					fprintf(stderr, "\n");
					*err = ERROR_NOT_FOUND;
					goto RET;
				}

				if(!var->declared) {
					fprintf(
						stderr,
						"Cannot Use Identifier '%s' before it is declared at ",
						cg->identifiers[parent.ident.id]
					);
					lexer_print_debug_to_file(
						stderr,
						&parent.debug.debug_info
					);
					fprintf(stderr, "\n");
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}

				type.type = var->mut ? TYPE_POINTER_VAR : TYPE_POINTER_CONST;

				size_t type_index = SIZE_MAX;
				for(size_t i = 0; i < scope->tc.count; i++) {
					if(types_are_equal(var->type, scope->tc.types[i])) {
						type_index = i;
						break;
					}
				}

				if(type_index == SIZE_MAX) {
					assert(0);
				}

				type.pointer.base = type_index;
				seeking = false;
			} break;

			case AST_FN_CALL: {
				Type ret = {.type = TYPE_NONE };

				for(size_t i = 0; i < cg->fn_sig_count; i++) {
					if(cg->fn_sigs[i].id == parent.fn_call.fn_id) {
						ret = cg->fn_sigs[i].ret;
						break;
					}
				}
				if(!ret.type) {
					fprintf(stderr, "Cannot Call Undeclared Function at ");
					lexer_print_debug_to_file(
						stderr,
						&parent.debug.debug_info
					);
					fprintf(stderr, "\n");
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}

				switch(ret.type) {
				case TYPE_POINTER_CONST:
				case TYPE_POINTER_ABYSS:
				case TYPE_POINTER_VAR:
					type.type = ret.type;
					break;

				case TYPE_SLICE_CONST:
					type.type = TYPE_POINTER_CONST;
					break;
				case TYPE_SLICE_ABYSS:
					type.type = TYPE_POINTER_ABYSS;
					break;
				case TYPE_SLICE_VAR:
					type.type = TYPE_POINTER_VAR;
					break;

				default:
					type.type = TYPE_POINTER_CONST;
					break;
				}
				seeking = false;
			} break;

			case AST_DEREF: {
				parent = cg->nodes[parent.deref.ptr];
			} break;

			case AST_SUBSCRIPT: {
				parent = cg->nodes[parent.subscript.arr];
			} break;

			case AST_STRUCT_ACCESS: {
				parent = cg->nodes[parent.struct_access.parent];
			} break;

			default:
				fprintf(stderr, "Cannot Take Address of Value at ");
				lexer_print_debug_to_file(stderr, &expr.debug.debug_info);
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
		}
	} break;

	case AST_SUBSCRIPT: {
		Type arr = type_of_expr(
			cg,
			scope,
			expr.subscript.arr,
			err
		);
		if(*err) goto RET;

		type = scope->tc.types[arr.slice.base];
	} break;

	case AST_ARRAY_LIT: {
		Type elem = type_of_expr(
			cg,
			scope,
			expr.array_lit.elems[0],
			err
		);
		if(*err) goto RET;

		size_t elem_index = types_register_nexist(&scope->tc, elem, err);
		if(*err) goto RET;

		type = (Type) {
			.array = {
				.type = TYPE_ARRAY,
				.base = elem_index,
				.len = expr.array_lit.elem_count
			},
		};
	} break;

	case AST_STRUCT_LIT: {
		type.type = TYPE_STRUCT;
		type.struct_type.member_count = expr.struct_lit.member_count;
		type.struct_type.member_types = malloc(
			expr.struct_lit.member_count * sizeof(size_t)
		);
		CHECK_MALLOC(type.struct_type.member_types);
		type.struct_type.member_name_ids = malloc(
			expr.struct_lit.member_count * sizeof(size_t)
		);
		CHECK_MALLOC(type.struct_type.member_name_ids);

		for(size_t i = 0; i < expr.struct_lit.member_count; i++) {
			Type member = type_of_expr(
				cg,
				scope,
				expr.struct_lit.member_values[i],
				err
			);
			if(*err) goto RET;

			size_t index = types_register_nexist(&scope->tc, member, err);
			if(*err) goto RET;

			type.struct_type.member_types[i] = index;
		}

		memcpy(
			type.struct_type.member_name_ids,
			expr.struct_lit.member_name_ids,
			sizeof(size_t)*expr.struct_lit.member_count
		);
	} break;

	case AST_STRUCT_ACCESS: {
		Type parent = type_of_expr(
			cg,
			scope,
			expr.struct_access.parent,
			err
		);
		if(*err) goto RET;

		if(parent.type != TYPE_STRUCT) {
			fprintf(stderr, "Cannot Access Member of non-struct Type at ");
			lexer_print_debug_to_file(stderr, &expr.debug.debug_info);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		type.type = TYPE_NONE;
		for(size_t i = 0; i < parent.struct_type.member_count; i++) {
			if(expr.struct_access.member_id
			   == parent.struct_type.member_name_ids[i]
			) {
				type = scope->tc.types[parent.struct_type.member_types[i]];
				break;
			}
		}
		if(!type.type) {
			fprintf(
				stderr,
				"No Member '%s' in struct at ",
				cg->identifiers[expr.struct_access.member_id]
			);
			lexer_print_debug_to_file(stderr, &expr.debug.debug_info);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
	} break;

	case AST_STRING_LIT:
	case AST_ZSTRING_LIT:
		type = (Type) {.slice = {.type = TYPE_SLICE_CONST, .base = 0}};
		break;
	case AST_CSTRING_LIT:
		type = (Type) {.pointer = {.type = TYPE_POINTER_CONST, .base = 0}};
		break;

	default:
		fprintf(stderr, "Expected Expression at ");
		lexer_print_debug_to_file(stderr, &expr.debug.debug_info);
		fprintf(stderr, "\n");
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

RET:
	types_register_nexist(&scope->tc, type, err);
	return type;
}

static void gen_expr(
	CodeGen	*cg,
	const AstNode *expr,
	Type expected,
	size_t *temp_counter,
	Scope *scope,
	Error *err
);

static void	gen_fn_call(
	CodeGen	*cg,
	const AstNode *call,
	Type expected,
	size_t *temp_counter,
	Scope *scope,
	Error *err
)
{
	size_t *args = malloc(call->fn_call.arg_count *	sizeof*args);
	CHECK_MALLOC(args);

	size_t fn_index	= SIZE_MAX;
	for(size_t i = 0; i	< cg->fn_sig_count;	i++) {
		if(cg->fn_sigs[i].id ==	call->fn_call.fn_id) {
			fn_index = i;
			break;
		}
	}
	if(fn_index	== SIZE_MAX) {
		fprintf(
			stderr,
			"Calling Undeclared	Function %s	at ",
			cg->identifiers[call->fn_call.fn_id]
		);
		lexer_print_debug_to_file(stderr, &call->debug.debug_info);
		fprintf(stderr,	"\n");
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

	const FnSig	sig	= cg->fn_sigs[fn_index];
	if(sig.arg_count !=	call->fn_call.arg_count) {
		fprintf(
			stderr,
			"Expected %zi arguments	to function, found %zi at ",
			sig.arg_count,
			call->fn_call.arg_count
		);
		lexer_print_debug_to_file(stderr, &call->debug.debug_info);
		fprintf(stderr,	"\n");
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

	if(expected.type && !types_are_equal(expected, sig.ret))	{
		fprintf(stderr,	"Function Returns '");
		type_print(stderr, &scope->tc, sig.ret);
		fprintf(stderr,	"',	Expected '");
		type_print(stderr, &scope->tc, expected);
		fprintf(stderr,	" at ");
		lexer_print_debug_to_file(stderr, &call->debug.debug_info);
		fprintf(stderr,	"\n");
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

	for(size_t i = 0; i	< sig.arg_count; i++) {
		gen_expr(
			cg,
			&cg->nodes[call->fn_call.args[i]],
			sig.args[i],
			temp_counter,
			scope,
			err
		);
		if(*err) goto RET;

		if(sig.args[i].type == TYPE_SLICE_CONST
		   || sig.args[i].type == TYPE_SLICE_ABYSS
		   || sig.args[i].type == TYPE_SLICE_VAR
		) {
			FPRINTF_OR_ERR(
				cg->output,
				"%%.%zi.0 = extractvalue {ptr, i64} %%%zi, 0\n"
				"%%.%zi.1 = extractvalue {ptr, i64} %%%zi, 1\n",
				*temp_counter, *temp_counter - 1,
				*temp_counter, *temp_counter - 1
			);
			*temp_counter += 1;
		}

		args[i]	= *temp_counter	- 1;
	}

	if(sig.ret.type != TYPE_PRIMITIVE_VOID) {
		FPRINTF_OR_ERR(cg->output, "%%%zi = ", *temp_counter);
		*temp_counter += 1;
	}

	FPUTS_OR_ERR(cg->output, "call ");

	print_type(cg->output, &scope->tc, sig.ret,	err);

	if(sig.linkage_name != SIZE_MAX) {
		FPRINTF_OR_ERR(cg->output, " @%s(", cg->strings[sig.linkage_name]);
	} else {
		FPRINTF_OR_ERR(cg->output, " @%s(", cg->identifiers[sig.id]);
	}

	for(size_t i = 0; i	< sig.arg_count; i++) {
		if(sig.args[i].type == TYPE_SLICE_CONST
		   || sig.args[i].type == TYPE_SLICE_ABYSS
		   || sig.args[i].type == TYPE_SLICE_VAR
		) {
			FPRINTF_OR_ERR(
				cg->output,
				"ptr %%.%zi.0, i64 %%.%zi.1",
				args[i], args[i]
			);
		} else {
			print_type(cg->output, &scope->tc, sig.args[i], err);
			if(*err) goto RET;
			FPRINTF_OR_ERR(cg->output, " %%%zi", args[i]);
		}

		if(i != sig.arg_count - 1) {
			FPUTS_OR_ERR(cg->output, ", ");
		}
	}
	FPUTS_OR_ERR(cg->output, ")\n");

RET:
	if(args) free(args);
	return;
}

static void	gen_assign(
	CodeGen	*cg,
	const AstNode *assign,
	size_t *temp_counter,
	Scope *scope,
	Error *err
)
{
	const AstNode lhs =	cg->nodes[assign->assign.var];

	AstNode expr = {
		.binop = {
			.lhs = assign->assign.var,
			.rhs = assign->assign.expr,
		},
	};

	switch(assign->type) {
	case AST_ASSIGN:
		expr = cg->nodes[assign->assign.expr];
		break;
	case AST_MUL_ASSIGN:
		expr.type = AST_MUL;
		break;
	case AST_DIV_ASSIGN:
		expr.type = AST_DIV;
		break;
	case AST_ADD_ASSIGN:
		expr.type = AST_ADD;
		break;
	case AST_SUB_ASSIGN:
		expr.type = AST_SUB;
		break;
	default:
		assert(0);
	}

	switch(lhs.type) {
	case AST_IDENT:
		do {} while(0);
		Var	*ident = find_var(scope, lhs.ident.id);
		if(!ident) {
			fprintf(
				stderr,
				"Undeclared	variable '%s' at ",
				cg->identifiers[lhs.ident.id]
			);
			lexer_print_debug_to_file(stderr, &lhs.debug.debug_info);
			fprintf(stderr,	"\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		gen_expr(
			cg,
			&expr,
			ident->type,
			temp_counter,
			scope,
			err
		);
		if(*err) goto RET;

		FPUTS_OR_ERR(cg->output, "store ");
		print_type(cg->output, &scope->tc, ident->type,	err);
		if(*err) goto RET;
		FPRINTF_OR_ERR(
			cg->output,
			" %%%zi, ptr %%%s\n",
			*temp_counter -	1,
			cg->identifiers[ident->id]
		);
		break;

	case AST_DEREF: {
		Type ptr_type = type_of_expr(
			cg,
			scope,
			lhs.deref.ptr,
			err
		);
		if(*err) goto RET;

		if(ptr_type.type != TYPE_POINTER_ABYSS
		   && ptr_type.type != TYPE_POINTER_VAR
		) {
			if(ptr_type.type == TYPE_POINTER_CONST) {
				fprintf(stderr, "Cannot Assign to const Pointer at ");
				lexer_print_debug_to_file(stderr, &assign->debug.debug_info);
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
			fprintf(stderr, "Cannot Dereference non-Pointer Type '");
			type_print(stderr, &scope->tc, ptr_type);
			fprintf(stderr, "' at ");
			lexer_print_debug_to_file(stderr, &assign->debug.debug_info);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		gen_expr(
			cg,
			&cg->nodes[lhs.deref.ptr],
			ptr_type,
			temp_counter,
			scope,
			err
		);
		if(*err) goto RET;
		const size_t ptr_ssa = *temp_counter - 1;

		Type base_type = scope->tc.types[ptr_type.pointer.base];

		gen_expr(
			cg,
			&expr,
			base_type,
			temp_counter,
			scope,
			err
		);
		if(*err) goto RET;

		FPUTS_OR_ERR(cg->output, "store ");
		print_type(cg->output, &scope->tc, base_type, err);
		FPRINTF_OR_ERR(
			cg->output,
			" %%%zi, ptr %%%zi\n",
			*temp_counter - 1,
			ptr_ssa
		);
	} break;

	case AST_SUBSCRIPT: {
		const Type array_type =	type_of_expr(
			cg,
			scope,
			lhs.subscript.arr,
			err
		);
		if(*err) goto RET;

		gen_expr(
			cg,
			&cg->nodes[lhs.subscript.arr],
			array_type,
			temp_counter,
			scope,
			err
		);
		if(*err) goto RET;
		const size_t array_ssa = *temp_counter - 1;

		if(array_type.type != TYPE_ARRAY
			&& array_type.type != TYPE_SLICE_ABYSS
			&& array_type.type != TYPE_SLICE_VAR
		) {
			if(array_type.type == TYPE_SLICE_CONST)	{
				fprintf(stderr,	"Cannot	Assign to const	Slice at ");
				lexer_print_debug_to_file(
					stderr,
					&cg->nodes[lhs.subscript.arr].debug.debug_info
				);
				fprintf(stderr,	"\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
			fprintf(stderr,	"Expected Slice	Type at	");
			lexer_print_debug_to_file(
				stderr,
				&cg->nodes[lhs.subscript.arr].debug.debug_info
			);
			fprintf(stderr,	"\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		if(array_type.type == TYPE_ARRAY) {
			bool seeking = true;
			AstNode parent = cg->nodes[lhs.subscript.arr];
			while(seeking) {
				switch(parent.type) {
				case AST_IDENT: {
					const Var *var = find_var(scope, parent.ident.id);
					seeking = false;
				} break;
				case AST_DEREF: {
					Type ptr_type = type_of_expr(
						cg,
						scope,
						parent.deref.ptr,
						err
					);
					if(*err) goto RET;

					// know that it's a pointer because it passed sema
					if(ptr_type.type == TYPE_POINTER_CONST) {
						fprintf(stderr, "Cannot Assign to const Pointer at ");
						lexer_print_debug_to_file(
							stderr,
							&lhs.debug.debug_info
						);
						fprintf(stderr, "\n");
						*err = ERROR_UNEXPECTED_DATA;
						goto RET;
					}
					seeking = false;
				} break;
				case AST_SUBSCRIPT: {
					parent = cg->nodes[parent.subscript.arr];
				} break;
				case AST_STRUCT_ACCESS: {
					Type struct_type = type_of_expr(
						cg,
						scope,
						parent.struct_access.parent,
						err
					);
					if(*err) goto RET;
					Type member_type = {.type = TYPE_NONE};
					for(
						size_t i = 0;
						i < struct_type.struct_type.member_count;
						i++
					) {
						if(struct_type.struct_type.member_name_ids[i]
							== parent.struct_access.member_id
						) {
							member_type = scope->tc.types[
								struct_type.struct_type.member_types[i]
							];
							break;
						}
					}
					if(member_type.type == TYPE_NONE) {
						fprintf(
							stderr,
							"No Member '%s' in Type at ",
							cg->identifiers[parent.struct_access.member_id]
						);
						lexer_print_debug_to_file(
							stderr,
							&parent.debug.debug_info
						);
						fprintf(stderr, "\n");
						*err = ERROR_UNEXPECTED_DATA;
						goto RET;
					}
					switch(member_type.type) {
					case TYPE_POINTER_CONST:
						fprintf(stderr, "Cannot Assign to const Pointer at ");
						lexer_print_debug_to_file(
							stderr,
							&lhs.debug.debug_info
						);
						fprintf(stderr, "\n");
						*err = ERROR_UNEXPECTED_DATA;
						goto RET;
					case TYPE_POINTER_ABYSS:
					case TYPE_POINTER_VAR:
						seeking = false;
						break;
					case TYPE_SLICE_CONST:
						fprintf(stderr, "Cannot Assign to const Slice at ");
						lexer_print_debug_to_file(
							stderr,
							&lhs.debug.debug_info
						);
						fprintf(stderr, "\n");
						*err = ERROR_UNEXPECTED_DATA;
						goto RET;
					case TYPE_SLICE_ABYSS:
					case TYPE_SLICE_VAR:
						seeking = false;
						break;
					default:
						parent = cg->nodes[parent.struct_access.parent];
						break;
					}
				} break;
				case AST_FN_CALL: {
					for(size_t i = 0; i < cg->fn_sig_count; i++) {
						if(cg->fn_sigs[i].id == parent.fn_call.fn_id) {
							switch(cg->fn_sigs[i].ret.type) {
							case TYPE_POINTER_ABYSS:
							case TYPE_POINTER_VAR:
							case TYPE_SLICE_ABYSS:
							case TYPE_SLICE_VAR:
								seeking = false;
								break;
							default:
								fprintf(
									stderr,
									"Cannot Assign to Temporary Value at "
								);
								lexer_print_debug_to_file(
									stderr,
									&parent.debug.debug_info
								);
								fprintf(stderr, "\n");
								*err = ERROR_UNEXPECTED_DATA;
								goto RET;
							}
						}
					}
					// Will find because passed sema
					break;
				}
				default:
					seeking = false;
					break;
				}
			}
		}

		gen_expr(
			cg,
			&cg->nodes[lhs.subscript.index],
			(Type) {.type =	TYPE_PRIMITIVE_U64},
			temp_counter,
			scope,
			err
		);
		if(*err) goto RET;

		const size_t index_ssa = *temp_counter - 1;

		const size_t subscript_location	= *temp_counter;
		*temp_counter += 1;
		FPRINTF_OR_ERR(
			cg->output,
			"%%%zi = getelementptr inbounds	",
			subscript_location
		);
		print_type(cg->output, &scope->tc, array_type, err);
		if(*err) goto RET;

		Type array_elem_type;

		switch(array_type.type)	{
		case TYPE_ARRAY:
			FPRINTF_OR_ERR(
				cg->output,
				", ptr %%%zi, i64 %%%zi\n",
				array_ssa,
				index_ssa
			);
			array_elem_type	= scope->tc.types[array_type.array.base];
			break;

		case TYPE_SLICE_ABYSS:
		case TYPE_SLICE_VAR:
			FPRINTF_OR_ERR(
				cg->output,
				", ptr %%%zi, i32 0, i64 %%%zi\n",
				array_ssa,
				index_ssa
			);
			array_elem_type	= scope->tc.types[array_type.slice.base];
			break;

		default:
			assert(0);
			break;
		}

		gen_expr(
			cg,
			&expr,
			array_elem_type,
			temp_counter,
			scope,
			err
		);
		if(*err) goto RET;

		FPUTS_OR_ERR(cg->output, "store ");
		print_type(cg->output, &scope->tc, array_elem_type,	err);
		if(*err) goto RET;
		FPRINTF_OR_ERR(
			cg->output,
			"%%%zi,	ptr	%%%zi\n",
			*temp_counter -	1,
			subscript_location
		);
	} break;

	case AST_STRUCT_ACCESS: {
		const AstNode *parent =	&cg->nodes[lhs.struct_access.parent];

		const Type parent_type = type_of_expr(
			cg,
			scope,
			lhs.struct_access.parent,
			err
		);
		if(*err) goto RET;

		const size_t struct_location = *temp_counter - 1;

		if(parent_type.type	!= TYPE_STRUCT)	{
			fprintf(stderr,	"Expected struct Type at ");
			lexer_print_debug_to_file(stderr, &parent->debug.debug_info);
			fprintf(stderr,	"\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		size_t struct_member_index = SIZE_MAX;
		for(size_t i = 0; i	< parent_type.struct_type.member_count;	i++) {
			if(parent_type.struct_type.member_name_ids[i]
				== parent->struct_access.member_id
			) {
				struct_member_index	= i;
				break;
			}
		}
		if(struct_member_index == SIZE_MAX)	{
			fprintf(
				stderr,
				"No	Member '%s'	in struct Type at ",
				cg->identifiers[parent->struct_access.member_id]
			);
			lexer_print_debug_to_file(stderr, &parent->debug.debug_info);
			fprintf(stderr,	"\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		const Type member_type = scope->tc.types[
			parent_type.struct_type.member_types[struct_member_index]
		];

		const size_t member_location = *temp_counter;
		*temp_counter += 1;
		FPRINTF_OR_ERR(
			cg->output,
			"%%%zi = getelementptr inbounds	",
			member_location
		);
		print_type(cg->output, &scope->tc, member_type,	err);
		if(*err) goto RET;
		FPRINTF_OR_ERR(
			cg->output,
			", ptr %%%zi, i32 %zi\n",
			struct_location,
			struct_member_index
		);

		gen_expr(
			cg,
			&expr,
			member_type,
			temp_counter,
			scope,
			err
		);
		if(*err) goto RET;

		FPUTS_OR_ERR(cg->output, "store ");
		print_type(cg->output, &scope->tc, member_type,	err);
		if(*err) goto RET;
		FPRINTF_OR_ERR(
			cg->output,
			"%%%zi,	ptr	%%%zi\n",
			*temp_counter -	1,
			member_location
		);
	} break;

	default:
		fprintf(stderr,	"Illegal Left-Hand Side	to Assignment at ");
		lexer_print_debug_to_file(stderr, &lhs.debug.debug_info);
		fprintf(stderr,	"\n");
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

RET:
	return;
}

static void	gen_extend_integer(
	CodeGen	*cg,
	size_t *ssa,
	Type current,
	Type needed,
	size_t *temp_counter,
	const Scope	*scope,
	const DebugInfo	*debug,
	Error *err
)
{
	if(types_are_equal(current,	needed)) return;

	if(!types_are_compatible(current, needed)) {
		fprintf(stderr,	"Operand Type '");
		type_print(stderr, &scope->tc, current);
		fprintf(stderr,	"' is Incompatible with	Result Type	'");
		type_print(stderr, &scope->tc, needed);
		fprintf(stderr,	"' at ");
		lexer_print_debug_to_file(stderr, debug);
		fprintf(stderr,	"\n");
	}

	switch(current.type) {
	case TYPE_PRIMITIVE_U8:
	case TYPE_PRIMITIVE_U16:
	case TYPE_PRIMITIVE_U32:
	case TYPE_PRIMITIVE_U64:
		FPRINTF_OR_ERR(
			cg->output,
			"%%%zi = zext nneg ",
			*temp_counter
		);
		print_type(cg->output, &scope->tc, current,	err);
		if(*err) goto RET;
		FPRINTF_OR_ERR(cg->output, " %%%zi to ", *ssa);
		print_type(cg->output, &scope->tc, needed, err);
		if(*err) goto RET;
		FPUTS_OR_ERR(cg->output, "\n");
		*ssa = *temp_counter;
		*temp_counter += 1;
		break;
	
	case TYPE_PRIMITIVE_S8:
	case TYPE_PRIMITIVE_S16:
	case TYPE_PRIMITIVE_S32:
	case TYPE_PRIMITIVE_S64:
		FPRINTF_OR_ERR(
			cg->output,
			"%%%zi = sext ",
			*temp_counter
		);
		print_type(cg->output, &scope->tc, current,	err);
		if(*err) goto RET;
		FPRINTF_OR_ERR(cg->output, " %%%zi to ", *ssa);
		print_type(cg->output, &scope->tc, needed, err);
		if(*err) goto RET;
		FPUTS_OR_ERR(cg->output, "\n");
		*ssa = *temp_counter;
		*temp_counter += 1;
		break;

	default:
		fprintf(stderr,	"Unable	to Extend Type '");
		type_print(stderr, &scope->tc, current);
		fprintf(stderr,	"' to Type '");
		type_print(stderr, &scope->tc, needed);
		fprintf(stderr,	"' at ");
		lexer_print_debug_to_file(stderr, debug);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}
RET:
	return;
}

static void gen_deref(
	CodeGen	*cg,
	size_t index,
	Type expected,
	size_t *temp_counter,
	Scope *scope,
	Error *err
)
{
	const AstNode *expr	= &cg->nodes[index];

	Type ptr_type = type_of_expr(
		cg,
		scope,
		expr->deref.ptr,
		err
	);
	if(*err) goto RET;

	gen_expr(
		cg,
		&cg->nodes[expr->deref.ptr],
		(Type) {.type = TYPE_NONE},
		temp_counter,
		scope,
		err
	);
	const size_t deref_ptr_ssa = *temp_counter - 1;

	if(ptr_type.type != TYPE_POINTER_CONST
		&& ptr_type.type != TYPE_POINTER_VAR
	) {
		if(ptr_type.type == TYPE_POINTER_ABYSS) {
			fprintf(stderr,	"Attempting	to Read	from Abyssal Pointer at	");
			lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
			fprintf(stderr,	"\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
		fprintf(stderr,	"Expected Pointer Type at ");
		lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
		fprintf(stderr,	", found Type '");
		type_print(stderr, &scope->tc, ptr_type);
		fprintf(stderr,	"' instead.\n");
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

	if(expected.type) {
		if(!types_are_compatible(ptr_type, expected))	{
			fprintf(stderr,	"Cannot Cast Value of Type '");
			type_print(stderr, &scope->tc, ptr_type);
			fprintf(stderr,	"' to Type '");
			type_print(stderr, &scope->tc, expected);
			fprintf(stderr,	"' at ");
			lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
			fprintf(stderr,	"\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
	}

	FPRINTF_OR_ERR(cg->output, "%%%zi =	load ",	*temp_counter);
	print_type(
		cg->output,
		&scope->tc,
		scope->tc.types[ptr_type.pointer.base],
		err
	);
	if(*err) goto RET;

	FPRINTF_OR_ERR(cg->output, ", ptr %%%zi\n",	deref_ptr_ssa);
	*temp_counter += 1;

RET:
	return;
}

static void gen_expr(
	CodeGen	*cg,
	const AstNode *expr,
	Type expected,
	size_t *temp_counter,
	Scope *scope,
	Error *err
)
{
	switch(expr->type) {
	case AST_IDENT:
		do {} while(0);
		const Var *var = find_var(scope, expr->ident.id);
		if(!var->declared) {
			fprintf(
				stderr,
				"Variable '%s' used before it is declared at ",
				cg->identifiers[expr->ident.id]
			);
			lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
			fprintf(stderr,	"\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
		if(expected.type &&	!types_are_compatible(var->type, expected))	{
			fprintf(stderr,	"Expected Type '");
			type_print(stderr, &scope->tc, expected);
			fprintf(stderr,	"',	found Type '");
			type_print(stderr, &scope->tc, var->type);
			fprintf(stderr,	"' at ");
			lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
			fprintf(stderr,	"\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
		if(var->arg) {
			FPRINTF_OR_ERR(
				cg->output,
				"%%%zi = insertvalue {",
				*temp_counter
			);
			*temp_counter += 1;
			print_type(
				cg->output,
				&scope->tc,
				var->type,
				err
			);
			if(*err) goto RET;
			FPUTS_OR_ERR(cg->output, "} poison, ");
			print_type(
				cg->output,
				&scope->tc,
				var->type,
				err
			);
			if(*err) goto RET;
			FPRINTF_OR_ERR(
				cg->output,
				" %%%s, 0\n",
				cg->identifiers[var->id]
			);

			FPRINTF_OR_ERR(
				cg->output,
				"%%%zi = extractvalue {",
				*temp_counter
			);
			*temp_counter += 1;
			print_type(
				cg->output,
				&scope->tc,
				var->type,
				err
			);
			if(*err) goto RET;
			FPRINTF_OR_ERR(
				cg->output,
				"} %%%zi, 0\n",
				*temp_counter - 2
			);
		} else {
			FPRINTF_OR_ERR(cg->output, "%%%zi =	load ",	*temp_counter);
			*temp_counter += 1;
			print_type(cg->output, &scope->tc, var->type, err);
			if(*err) goto RET;
			FPRINTF_OR_ERR(
				cg->output,
				", ptr %%%s\n",
				cg->identifiers[var->id]
			);
		}
		break;

	case AST_INT_LIT:
		if(!expected.type) {
			fprintf(stderr,	"No	Destination	Type for Integer Literal at	");
			lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
			fprintf(stderr,	"\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
		FPRINTF_OR_ERR(cg->output, "%%%zi = alloca ", *temp_counter);
		*temp_counter += 1;
		print_type(cg->output, &scope->tc, expected, err);
		if(*err) goto RET;
		FPUTS_OR_ERR(cg->output, "\nstore ");
		print_type(cg->output, &scope->tc, expected, err);
		if(*err) goto RET;
		FPRINTF_OR_ERR(
			cg->output,
			" %ji, ptr %%%zi\n",
			expr->int_lit.val,
			*temp_counter - 1
		);
		FPRINTF_OR_ERR(cg->output, "%%%zi = load ", *temp_counter);
		*temp_counter += 1;
		print_type(cg->output,  &scope->tc, expected, err);
		if(*err) goto RET;
		FPRINTF_OR_ERR(cg->output, ", ptr %%%zi\n", *temp_counter - 2);
		break;

	case AST_MUL:
	case AST_DIV:
	case AST_ADD:
	case AST_SUB: {
		if(!type_is_arithmetic(expected)) {
			fprintf(
				stderr,
				"Expected Arithmetic Result from Operation at "
			);
			lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
			fprintf(stderr,	"\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		const Type lhs_type = type_of_expr(
			cg,
			scope,
			expr->binop.lhs,
			err
		);
		if(*err) goto RET;

		gen_expr(
			cg,
			&cg->nodes[expr->binop.lhs],
			expected,
			temp_counter,
			scope,
			err
		);
		if(*err) goto RET;
		size_t lhs_ssa = *temp_counter - 1;

		const Type rhs_type = type_of_expr(
			cg,
			scope,
			expr->binop.rhs,
			err
		);
		if(*err) goto RET;

		gen_expr(
			cg,
			&cg->nodes[expr->binop.rhs],
			expected,
			temp_counter,
			scope,
			err
		);
		if(*err) goto RET;
		size_t rhs_ssa = *temp_counter - 1;

		Type needed;
		if(expected.type) {
			needed = expected;
		} else {
			if(types_are_compatible(lhs_type, rhs_type)) {
				needed = rhs_type;
			} else if(types_are_compatible(rhs_type, lhs_type))	{
				needed = lhs_type;
			} else {
				fprintf(
					stderr,
					"Attempting	to Perform Arithmetic on "
					"Non-Compatible	Types '"
				);
				type_print(stderr, &scope->tc, lhs_type);
				fprintf(stderr,	"' and '");
				type_print(stderr, &scope->tc, rhs_type);
				fprintf(stderr,	"' at ");
				lexer_print_debug_to_file(
					stderr,
					&expr->debug.debug_info
				);
				fprintf(stderr,	"\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
		}

		gen_extend_integer(
			cg,
			&lhs_ssa,
			lhs_type,
			expected,
			temp_counter,
			scope,
			&expr->debug.debug_info,
			err
		);
		if(*err) goto RET;

		gen_extend_integer(
			cg,
			&rhs_ssa,
			rhs_type,
			expected,
			temp_counter,
			scope,
			&expr->debug.debug_info,
			err
		);
		if(*err) goto RET;

		FPRINTF_OR_ERR(cg->output, "%%%zi =	", *temp_counter);
		switch(expr->type) {
		case AST_MUL:
			FPUTS_OR_ERR(cg->output, "mul ");
			break;
		case AST_DIV:
			if(type_is_unsigned(lhs_type)) {
				FPUTS_OR_ERR(cg->output, "udiv ");
			} else {
				FPUTS_OR_ERR(cg->output, "sdiv ");
			}
			break;
		case AST_ADD:
			FPUTS_OR_ERR(cg->output, "add ");
			break;
		case AST_SUB:
			FPUTS_OR_ERR(cg->output, "sub ");
			break;
		default:
			assert(0);
			break;
		}
		print_type(cg->output, &scope->tc, lhs_type, err);
		if(*err) goto RET;
		FPRINTF_OR_ERR(cg->output, " %%%zi, %%%zi\n", lhs_ssa, rhs_ssa);
		*temp_counter += 1;
	} break;

	case AST_FN_CALL:
		gen_fn_call(cg,	expr, expected,	temp_counter, scope, err);
		break;

	case AST_DEREF: {
		Type ptr_type = type_of_expr(
			cg,
			scope,
			expr->deref.ptr,
			err
		);
		if(*err) goto RET;

		gen_expr(
			cg,
			&cg->nodes[expr->deref.ptr],
			ptr_type,
			temp_counter,
			scope,
			err
		);
		if(*err) goto RET;
		const size_t deref_ptr_ssa = *temp_counter - 1;

		if(ptr_type.type != TYPE_POINTER_CONST
			&& ptr_type.type != TYPE_POINTER_VAR
		) {
			if(ptr_type.type == TYPE_POINTER_ABYSS) {
				fprintf(stderr,	"Attempting	to Read	from Abyssal Pointer at	");
				lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
				fprintf(stderr,	"\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
			fprintf(stderr,	"Expected Pointer Type at ");
			lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
			fprintf(stderr,	", found Type '");
			type_print(stderr, &scope->tc, ptr_type);
			fprintf(stderr,	"' instead.\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		Type base = scope->tc.types[ptr_type.pointer.base];

		if(expected.type) {
			if(!types_are_compatible(base, expected)) {
				fprintf(stderr,	"Cannot	Cast Value of Type '");
				type_print(stderr, &scope->tc, ptr_type);
				fprintf(stderr,	"' to Type '");
				type_print(stderr, &scope->tc, expected);
				fprintf(stderr,	"' at ");
				lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
				fprintf(stderr,	"\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
		}

		FPRINTF_OR_ERR(cg->output, "%%%zi =	load ",	*temp_counter);
		print_type(cg->output, &scope->tc, base, err);
		if(*err) goto RET;
		FPRINTF_OR_ERR(cg->output, ", ptr %%%zi\n",	deref_ptr_ssa);
		*temp_counter += 1;
	} break;

	case AST_ADDR: {
		const AstNode addr_obj	= cg->nodes[expr->addr.base];

		Type ptr_type;

		switch(addr_obj.type) {
		case AST_IDENT: {
			size_t ident_index = SIZE_MAX;
			for(size_t i = 0; i	< scope->var_count;	i++) {
				if(scope->vars[i].id ==	addr_obj.ident.id) {
					ident_index	= i;
					break;
				}
			}

			if(ident_index == SIZE_MAX)	{
				fprintf(
					stderr,
					"Attempting	to take	Address	of Undeclared Variable '%s'"
					" at ",
					cg->identifiers[addr_obj.ident.id]
				);
				lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
				fprintf(
					stderr,
					"\n(Note: You cannot take the Address of a Parameter)\n"
				);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			if(!scope->vars[ident_index].declared) {
				fprintf(
					stderr,
					"Attempting	to take	Address	of Variable	'%s' at	",
					cg->identifiers[addr_obj.ident.id]
				);
				lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
				fprintf(stderr,	" before it	is declared.");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			size_t ident_type_index = SIZE_MAX;
			for(size_t i = 0; i	< scope->tc.count; i++)	{
				if(types_are_equal(
					scope->vars[ident_index].type,
					scope->tc.types[i]
				)) {
					ident_type_index = i;
					break;
				}
			}
			if(ident_type_index == SIZE_MAX) {
				ident_type_index = types_register(
					&scope->tc,
					scope->vars[ident_index].type,
					err
				);
				if(*err) goto RET;
			}
			ptr_type = (Type) {
				.pointer = {
					.type =	scope->vars[ident_index].mut
						? TYPE_POINTER_VAR : TYPE_POINTER_CONST,
					.base =	ident_type_index,
				},
			};

			if(expected.type) {
				if(!types_are_compatible(ptr_type, expected)){
					fprintf(stderr,	"Cannot Coerce Pointer of Type '");
					type_print(stderr, &scope->tc, ptr_type);
					fprintf(stderr,	"' to Type '");
					type_print(stderr, &scope->tc, expected);
					fprintf(stderr,	"' at ");
					lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
					fprintf(stderr, "\n");
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}
			}


			FPRINTF_OR_ERR(
				cg->output,
				"%%%zi = getelementptr ",
				*temp_counter
			);
			*temp_counter += 1;
			print_type(
				cg->output,
				&scope->tc,
				scope->vars[ident_index].type,
				err
			);
			if(*err) goto RET;

			FPRINTF_OR_ERR(
				cg->output,
				", ptr %%%s, i32 0\n",
				cg->identifiers[addr_obj.ident.id]
			);
		} break;

		case AST_SUBSCRIPT: {
			Type arr_type = type_of_expr(
				cg,
				scope,
				addr_obj.subscript.arr,
				err
			);
			if(*err) goto RET;

			Type elem_type = scope->tc.types[arr_type.slice.base];

			gen_expr(
				cg,
				&cg->nodes[addr_obj.subscript.index],
				(Type) {.type = TYPE_PRIMITIVE_U64},
				temp_counter,
				scope,
				err
			);
			if(*err) goto RET;
			size_t index_ssa = *temp_counter - 1;

			switch(arr_type.type) {
			case TYPE_ARRAY:
				gen_expr(
					cg,
					&(AstNode) {
						.addr = {
							.type = AST_ADDR,
							.debug_info = expr->debug.debug_info,
							.base = addr_obj.subscript.arr,
						},
					},
					(Type) {.type = TYPE_NONE},
					temp_counter,
					scope,
					err
				);
				if(*err) goto RET;
				break;

			case TYPE_SLICE_CONST:
			case TYPE_SLICE_VAR:
			case TYPE_SLICE_ABYSS:
				gen_expr(
					cg,
					&cg->nodes[addr_obj.subscript.arr],
					arr_type,
					temp_counter,
					scope,
					err
				);
				if(*err) goto RET;

				FPRINTF_OR_ERR(
					cg->output,
					"%%%zi = extractvalue {ptr, i64} %%%zi, 0\n",
					*temp_counter, *temp_counter - 1
				);
				*temp_counter += 1;
				break;

			default:
				fprintf(stderr, "Cannot Subscript non-array Type '");
				type_print(stderr, &scope->tc, arr_type);
				fprintf(stderr, "' at ");
				lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			FPRINTF_OR_ERR(
				cg->output,
				"%%%zi = getelementptr ",
				*temp_counter
			);
			*temp_counter += 1;
			print_type(cg->output, &scope->tc, elem_type, err);
			if(*err) goto RET;
			FPRINTF_OR_ERR(
				cg->output,
				", ptr %%%zi, i64 %%%zi\n",
				*temp_counter - 2,
				index_ssa
			);

			TypeType subs_ptr_access;
			switch(arr_type.type) {
			case TYPE_SLICE_CONST:
				subs_ptr_access = TYPE_POINTER_CONST;
				break;
			case TYPE_SLICE_VAR:
				subs_ptr_access = TYPE_POINTER_VAR;
				break;
			case TYPE_SLICE_ABYSS:
				subs_ptr_access = TYPE_POINTER_ABYSS;
				break;
			case TYPE_ARRAY: {
				AstNode parent = cg->nodes[addr_obj.subscript.arr];
				bool seeking = true;
				while(seeking) {
					switch(parent.type) {
					case AST_IDENT: {
						Var *ident = find_var(scope, parent.ident.id);
						subs_ptr_access = ident->mut
							? TYPE_POINTER_VAR
							: TYPE_POINTER_CONST;
						seeking = false;
					} break;
					case AST_STRUCT_ACCESS: {
						Type struct_type = type_of_expr(
							cg,
							scope,
							parent.struct_access.parent,
							err
						);
						if(*err) goto RET;

						Type member_type = {.type = TYPE_NONE};
						for(
							size_t i = 0;
							i < struct_type.struct_type.member_count;
							i++
						) {
							if(struct_type.struct_type.member_name_ids[i]
								== parent.struct_access.member_id
							) {
								member_type = scope->tc.types[
									struct_type.struct_type.member_types[i]
								];
								break;
							}
						}
						if(member_type.type == TYPE_NONE) {
							fprintf(
								stderr,
								"No Member '%s' in Type at ",
								cg->identifiers[parent.struct_access.member_id]
							);
							lexer_print_debug_to_file(
								stderr,
								&parent.debug.debug_info
							);
							fprintf(stderr, "\n");
							*err = ERROR_UNEXPECTED_DATA;
							goto RET;
						}

						switch(member_type.type) {
						case TYPE_POINTER_CONST:
						case TYPE_POINTER_ABYSS:
						case TYPE_POINTER_VAR:
							subs_ptr_access = member_type.type;
							seeking = false;
							break;
						case TYPE_SLICE_CONST:
							subs_ptr_access = TYPE_POINTER_CONST;
							seeking = false;
							break;
						case TYPE_SLICE_ABYSS:
							subs_ptr_access = TYPE_POINTER_ABYSS;
							seeking = false;
							break;
						case TYPE_SLICE_VAR:
							subs_ptr_access = TYPE_POINTER_VAR;
							seeking = false;
							break;
						default:
							parent = cg->nodes[parent.struct_access.parent];
							break;
						}
					} break;
					case AST_DEREF: {
						parent = cg->nodes[parent.deref.ptr];
					} break;
					case AST_SUBSCRIPT: {
						parent = cg->nodes[parent.subscript.arr];
					} break;
					case AST_FN_CALL: {
						for(size_t i = 0; i < cg->fn_sig_count; i++) {
							if(cg->fn_sigs[i].id == parent.fn_call.fn_id) {
								switch(cg->fn_sigs[i].ret.type) {
								case TYPE_POINTER_CONST:
									subs_ptr_access = TYPE_POINTER_CONST;
									break;
								case TYPE_POINTER_ABYSS:
									subs_ptr_access = TYPE_POINTER_ABYSS;
									break;
								case TYPE_POINTER_VAR:
									subs_ptr_access = TYPE_POINTER_VAR;
									break;
								case TYPE_SLICE_CONST:
									subs_ptr_access = TYPE_POINTER_CONST;
									break;
								case TYPE_SLICE_ABYSS:
									subs_ptr_access = TYPE_POINTER_ABYSS;
									break;
								case TYPE_SLICE_VAR:
									subs_ptr_access = TYPE_POINTER_VAR;
									break;
								default:
									subs_ptr_access = TYPE_POINTER_CONST;
									break;
								}
								seeking = false;
								break;
							}
						}
						// fn exists because we were able to generate it before
					} break;
					default:
						subs_ptr_access = TYPE_POINTER_CONST;
						seeking = false;
						break;
					}
				}
			} break;

			default:
				assert(0);
				break;
			}

			ptr_type = (Type) {
				.pointer = {
					.type = subs_ptr_access,
					.base = arr_type.slice.base,
				},
			};

			if(expected.type) {
				if(!types_are_compatible(ptr_type, expected)){
					fprintf(stderr,	"Cannot	Coerce Pointer of Type '");
					type_print(stderr, &scope->tc, ptr_type);
					fprintf(stderr,	"' to Type '");
					type_print(stderr, &scope->tc, expected);
					fprintf(stderr,	"' at ");
					lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}
			}
		} break;

		case AST_STRUCT_ACCESS: {
			Type struct_type = type_of_expr(
				cg,
				scope,
				expr->struct_access.parent,
				err
			);
			if(*err) goto RET;

			gen_expr(
				cg,
				&cg->nodes[expr->struct_access.parent],
				(Type) {.type = TYPE_NONE},
				temp_counter,
				scope,
				err
			);
			if(*err) goto RET;
			const size_t struct_ssa = *temp_counter - 1;

			size_t struct_member_index = SIZE_MAX;
			for(size_t i = 0; i < struct_type.struct_type.member_count; i++) {
				if(struct_type.struct_type.member_name_ids[i]
					== expr->struct_access.member_id
				) {
					struct_member_index = i;
					break;
				}
			}

			if(struct_member_index == SIZE_MAX) {
				fprintf(
					stderr,
					"No Member '%s' in Struct Type at ",
					cg->identifiers[expr->struct_access.member_id]
				);
				lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}


			FPRINTF_OR_ERR(
				cg->output,
				"%%%zi = getelementptr ",
				*temp_counter
			);
			*temp_counter += 1;
			print_type(cg->output, &scope->tc, struct_type, err);
			FPRINTF_OR_ERR(
				cg->output,
				", ptr %%%zi, i32 %zi\n",
				struct_ssa,
				struct_member_index
			);

			bool seeking = true;
			TypeType ptr_access;
			while(seeking) {
				AstNode parent = cg->nodes[expr->struct_access.parent];
				switch(parent.type) {
					case AST_IDENT: {
						Var *ident = find_var(scope, parent.ident.id);
						ptr_access = ident->mut
							? TYPE_POINTER_VAR
							: TYPE_POINTER_CONST;
						seeking = false;
					} break;
					case AST_STRUCT_ACCESS: {
						Type struct_type = type_of_expr(
							cg,
							scope,
							parent.struct_access.parent,
							err
						);
						if(*err) goto RET;

						Type member_type = {.type = TYPE_NONE};
						for(
							size_t i = 0;
							i < struct_type.struct_type.member_count;
							i++
						) {
							if(struct_type.struct_type.member_name_ids[i]
								== parent.struct_access.member_id
							) {
								member_type = scope->tc.types[
									struct_type.struct_type.member_types[i]
								];
								break;
							}
						}
						if(member_type.type == TYPE_NONE) {
							fprintf(
								stderr,
								"No Member '%s' in Type at ",
								cg->identifiers[parent.struct_access.member_id]
							);
							lexer_print_debug_to_file(
								stderr,
								&parent.debug.debug_info
							);
							fprintf(stderr, "\n");
							*err = ERROR_UNEXPECTED_DATA;
							goto RET;
						}

						switch(member_type.type) {
						case TYPE_POINTER_CONST:
						case TYPE_POINTER_ABYSS:
						case TYPE_POINTER_VAR:
							ptr_access = member_type.type;
							seeking = false;
							break;
						case TYPE_SLICE_CONST:
							ptr_access = TYPE_POINTER_CONST;
							seeking = false;
							break;
						case TYPE_SLICE_ABYSS:
							ptr_access = TYPE_POINTER_ABYSS;
							seeking = false;
							break;
						case TYPE_SLICE_VAR:
							ptr_access = TYPE_POINTER_VAR;
							seeking = false;
							break;
						default:
							parent = cg->nodes[parent.struct_access.parent];
							break;
						}
					} break;
					case AST_DEREF: {
						parent = cg->nodes[parent.deref.ptr];
					} break;
					case AST_SUBSCRIPT: {
						parent = cg->nodes[parent.subscript.arr];
					} break;
					case AST_FN_CALL: {
						for(size_t i = 0; i < cg->fn_sig_count; i++) {
							if(cg->fn_sigs[i].id == parent.fn_call.fn_id) {
								switch(cg->fn_sigs[i].ret.type) {
								case TYPE_POINTER_CONST:
									ptr_access = TYPE_POINTER_CONST;
									break;
								case TYPE_POINTER_ABYSS:
									ptr_access = TYPE_POINTER_ABYSS;
									break;
								case TYPE_POINTER_VAR:
									ptr_access = TYPE_POINTER_VAR;
									break;
								case TYPE_SLICE_CONST:
									ptr_access = TYPE_POINTER_CONST;
									break;
								case TYPE_SLICE_ABYSS:
									ptr_access = TYPE_POINTER_ABYSS;
									break;
								case TYPE_SLICE_VAR:
									ptr_access = TYPE_POINTER_VAR;
									break;
								default:
									ptr_access = TYPE_POINTER_CONST;
									break;
								}
								seeking = false;
								break;
							}
						}
						// fn exists because we were able to generate it before
					} break;
				default:
					ptr_access = TYPE_POINTER_CONST;
					seeking = false;
					break;
				}
			}
			ptr_type = (Type) {
				.pointer = {
					.type = ptr_access,
					.base = struct_type.struct_type
						.member_types[struct_member_index],
				},
			};

			if(expected.type) {
				if(!types_are_compatible(ptr_type, expected)){
					fprintf(stderr,	"Cannot	Coerce Pointer of Type '");
					type_print(stderr, &scope->tc, ptr_type);
					fprintf(stderr,	"' to Type '");
					type_print(stderr, &scope->tc, expected);
					fprintf(stderr,	"' at ");
					lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}
			}
		} break;
		default:
			fprintf(stderr,	"Cannot	take Address of	Value at ");
			lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
			fprintf(stderr,	"\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		if(
			expected.type == TYPE_SLICE_CONST
			|| expected.type == TYPE_SLICE_ABYSS
			|| expected.type == TYPE_SLICE_VAR
		) {
			FPRINTF_OR_ERR(
				cg->output,
				"%%%zi = insertvalue {ptr, i64} poison, ptr %%%zi, 0\n",
				*temp_counter,
				*temp_counter - 1
			);
			*temp_counter += 1;

			FPRINTF_OR_ERR(
				cg->output,
				"%%%zi = insertvalue {ptr, i64} %%%zi, i64 ",
				*temp_counter,
				*temp_counter - 1
			);

			if(scope->tc.types[ptr_type.pointer.base].type == TYPE_ARRAY) {
				FPRINTF_OR_ERR(
					cg->output,
					"%ji",
					scope->tc.types[ptr_type.pointer.base].array.len
				);
			} else {
				FPUTS_OR_ERR(cg->output, "1");
			}

			FPUTS_OR_ERR(cg->output, ", 1\n");
			*temp_counter += 1;
		}
	} break;

	case AST_SUBSCRIPT: {
		Type arr_type = type_of_expr(
			cg,
			scope,
			expr->subscript.arr,
			err
		);
		if(*err) goto RET;

		gen_expr(
			cg,
			&cg->nodes[expr->subscript.arr],
			arr_type,
			temp_counter,
			scope,
			err
		);
		if(*err) goto RET;
		const size_t arr_ssa = *temp_counter - 1;

		gen_expr(
			cg,
			&cg->nodes[expr->subscript.index],
			(Type) {.type = TYPE_PRIMITIVE_U64},
			temp_counter,
			scope,
			err
		);
		if(*err) goto RET;
		const size_t index_ssa = *temp_counter - 1;

		FPRINTF_OR_ERR(
			cg->output,
			"%%%zi = extractvalue ",
			*temp_counter
		);
		*temp_counter += 1;
		print_type(cg->output, &scope->tc, arr_type, err);
		if(*err) goto RET;
		FPRINTF_OR_ERR(cg->output, " %%%zi, 0\n", arr_ssa);
		FPRINTF_OR_ERR(
			cg->output,
			"%%%zi = getelementptr inbounds ",
			*temp_counter
		);
		*temp_counter += 1;
		Type elem_type = scope->tc.types[arr_type.array.base];
		print_type(cg->output, &scope->tc, elem_type, err);
		if(*err) goto RET;
		FPRINTF_OR_ERR(
			cg->output,
			", ptr %%%zi, i64 %%%zi\n",
			*temp_counter - 2,
			index_ssa
		);

		FPRINTF_OR_ERR(cg->output, "%%%zi = load ", *temp_counter);
		*temp_counter += 1;
		print_type(cg->output, &scope->tc, elem_type, err);
		if(*err) goto RET;
		FPRINTF_OR_ERR(cg->output, ", ptr %%%zi\n", *temp_counter - 2);
	} break;

	case AST_ARRAY_LIT: {
		Type elem_type = type_of_expr(
			cg,
			scope,
			expr->array_lit.elems[0],
			err
		);
		if(*err) goto RET;

		size_t *elem_ssas = malloc(
			expr->array_lit.elem_count * sizeof(size_t)
		);
		CHECK_MALLOC(elem_ssas);

		for(size_t i = 0; i < expr->array_lit.elem_count; i++) {
			gen_expr(
				cg,
				&cg->nodes[expr->array_lit.elems[i]],
				elem_type,
				temp_counter,
				scope,
				err
			);
			if(*err) {
				free(elem_ssas);
			}
			elem_ssas[i] = *temp_counter - 1;
		}

		for(size_t i = 0; i < expr->array_lit.elem_count; i++) {
			if(!fprintf(
				cg->output,
				"%%%zi = insertvalue [%ji x ",
				*temp_counter,
				expr->array_lit.elem_count
			)) {
				free(elem_ssas);
				fprintf(stderr, "Unable to Write to IR File\n");
				*err = ERROR_IO;
				goto RET;
			}
			*temp_counter += 1;

			print_type(cg->output, &scope->tc, elem_type, err);
			if(*err) {
				free(elem_ssas);
				goto RET;
			}

			if(i) {
				if(!fprintf(cg->output, "] %%%zi, ", *temp_counter - 2)) {
					free(elem_ssas);
					fprintf(stderr, "Unable to Write to IR File\n");
					*err = ERROR_IO;
					goto RET;
				}
			} else {
				if(!fprintf(cg->output, "] poison, ")) {
					free(elem_ssas);
					fprintf(stderr, "Unable to Write to IR File\n");
					*err = ERROR_IO;
					goto RET;
				}
			}

			print_type(cg->output, &scope->tc, elem_type, err);
			if(*err) {
				free(elem_ssas);
				goto RET;
			}

			if(!fprintf(cg->output, " %%%zi, %zi\n", elem_ssas[i], i)) {
				free(elem_ssas);
				fprintf(stderr, "Unable to Write to IR File\n");
				*err = ERROR_IO;
				goto RET;
			}
		}
		free(elem_ssas);
	} break;

	case AST_STRUCT_LIT: {
		if(expected.type != TYPE_STRUCT) {
			fprintf(
				stderr,
				"Cannot Coerce struct Literal to non-struct Type at "
			);
			lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		size_t *elem_ssas = malloc(
			expr->struct_lit.member_count * sizeof*elem_ssas
		);
		CHECK_MALLOC(elem_ssas);

		for(size_t i = 0; i < expr->struct_lit.member_count; i++) {
			size_t elem_index = SIZE_MAX;
			for(size_t j = 0; j < expected.struct_type.member_count; j++) {
				if(expected.struct_type.member_name_ids[j]
					== expr->struct_lit.member_name_ids[i]
				) {
					elem_index = j;
					break;
				}
			}

			if(elem_index == SIZE_MAX) {
				fprintf(
					stderr,
					"Struct Literal has Member '%s', "
					"but expected type does not at ",
					cg->identifiers[expr->struct_lit.member_name_ids[i]]
				);
				lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
				fprintf(stderr, "\n");
				free(elem_ssas);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
			gen_expr(
				cg,
				&cg->nodes[expr->struct_lit.member_values[elem_index]],
				scope->tc.types[expected.struct_type.member_types[elem_index]],
				temp_counter,
				scope,
				err
			);
			if(*err) {
				free(elem_ssas);
				goto RET;
			}

			elem_ssas[elem_index] = *temp_counter - 1;
		}

		int io_error = 0;
		for(size_t i = 0; i < expected.struct_type.member_count; i++) {
			io_error += fprintf(
				cg->output,
				"%%%zi = insertvalue ",
				*temp_counter
			) < 0;
			*temp_counter += 1;

			print_type(cg->output, &scope->tc, expected, err);
			*err += io_error;
			if(i) {
				io_error += fprintf(
					cg->output,
					" %%%zi, ",
					*temp_counter - 2
				) < 0;
			} else {
				io_error += fputs(" poison, ", cg->output) < 0;
			}

			print_type(
				cg->output,
				&scope->tc,
				scope->tc.types[expected.struct_type.member_types[i]],
				err
			);
			io_error += *err;

			io_error += fprintf(
				cg->output,
				" %%%zi, %zi\n",
				elem_ssas[i],
				i
			) < 0;
		}

		if(io_error) {
			fprintf(stderr, "Could Not Write to Output File!\n");
			free(elem_ssas);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		free(elem_ssas);
	} break;

	case AST_STRUCT_ACCESS: {
		Type parent_type = type_of_expr(
			cg,
			scope,
			expr->struct_access.parent,
			err
		);
		if(*err) goto RET;

		if(parent_type.type != TYPE_STRUCT) {
			fprintf(
				stderr,
				"No Member '%s' in non-struct Type at ",
				cg->identifiers[expr->struct_access.member_id]
			);
			lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		gen_expr(
			cg,
			&cg->nodes[expr->struct_access.parent],
			parent_type,
			temp_counter,
			scope,
			err
		);
		if(*err) goto RET;

		size_t parent_ssa = *temp_counter - 1;

		size_t elem_index = SIZE_MAX;
		for(size_t i = 0; i < parent_type.struct_type.member_count; i++) {
			if(parent_type.struct_type.member_name_ids[i]
				== expr->struct_access.member_id
			) {
				elem_index = i;
				break;
			}
		}

		if(elem_index == SIZE_MAX) {
			fprintf(
				stderr,
				"No Member '%s' in Type '",
				cg->identifiers[expr->struct_access.member_id]
			);
			type_print(stderr, &scope->tc, parent_type);
			fprintf(stderr, "' at ");
			lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		FPRINTF_OR_ERR(cg->output, "%%%zi = extractvalue ", *temp_counter);
		*temp_counter += 1;
		print_type(cg->output, &scope->tc, parent_type, err);
		if(*err) goto RET;
		FPRINTF_OR_ERR(
			cg->output,
			" %%%zi, %zi\n",
			parent_ssa,
			elem_index
		);
	} break;

	case AST_STRING_LIT: {
		FPRINTF_OR_ERR(
			cg->output,
			"%%%zi = insertvalue {ptr, i64} poison, ptr @.S%zi, 0\n"
			"%%%zi = sub i64 @.SL%zi, 1\n"
			"%%%zi = insertvalue {ptr, i64} %%%zi, i64 %%%zi, 1\n",
			*temp_counter, expr->string_lit.id,
			*temp_counter + 1, expr->string_lit.id,
			*temp_counter + 2, *temp_counter, *temp_counter + 1
		);
		*temp_counter += 3;
	} break;

	case AST_ZSTRING_LIT: {
		FPRINTF_OR_ERR(
			cg->output,
			"%%%zi = insertvalue {ptr, i64} poison, ptr @.S%zi, 0\n"
			"%%%zi = insertvalue {ptr, i64} %%%zi, i64 @.SL%zi, 1\n",
			*temp_counter, expr->string_lit.id,
			*temp_counter + 1, *temp_counter, expr->string_lit.id
		);
		*temp_counter += 2;
	} break;

	case AST_CSTRING_LIT: {
		FPRINTF_OR_ERR(
			cg->output,
			"%%%zi = insertvalue {ptr} poison, ptr @.S%zi, 0\n"
			"%%%zi = extractvalue {ptr} %%%zi, 0\n",
			*temp_counter, expr->string_lit.id,
			*temp_counter + 1, *temp_counter
		);
		*temp_counter += 2;
	} break;

	default:
		fprintf(stderr,	"Expected Expression at	");
		lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
		fprintf(stderr,	"\n");
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}
RET:
	return;
}

static void	gen_fn_def(
	CodeGen *cg,
	size_t index,
	size_t fnnum,
	const Scope *global_scope,
	Error *err
)
{
	Scope scope;
	scope_init(&scope, global_scope, err);
	if(*err) goto RET;

	DynArr vars;
	dynarr_init(&vars, sizeof(Var));

	assert(cg->nodes[index].type ==	AST_FN_DEF);
	const AstNode fn = cg->nodes[index];

	const FnSig	sig	= cg->fn_sigs[fnnum];

	const AstNode block	= cg->nodes[fn.fn_def.block];
	if(block.type == AST_EXTERN) {
		FPUTS_OR_ERR(cg->output, "declare dso_local ");
		print_type(cg->output, &scope.tc, sig.ret, err);
		if(*err) goto RET;

		FPRINTF_OR_ERR(cg->output, " @%s(", cg->strings[sig.linkage_name]);

		for(size_t i = 0; i	< sig.arg_count; i++) {
			if(sig.args[i].type == TYPE_SLICE_CONST
			   || sig.args[i].type == TYPE_SLICE_ABYSS
			   || sig.args[i].type == TYPE_SLICE_VAR
			) {
				if(fprintf(
					cg->output,
					"ptr noundef %%%s.0, i64 noundef %%%s.1\n",
					cg->identifiers[sig.arg_ids[i]],
					cg->identifiers[sig.arg_ids[i]]
				) < 0) {
					fprintf(stderr, "Failed to Write to IR File\n");
					*err = ERROR_IO;
					dynarr_clean(&vars);
					goto RET;
				}
			} else {
				print_type(cg->output, &scope.tc, sig.args[i], err);
				if(*err) {
					dynarr_clean(&vars);
				}

				if(!fprintf(
					cg->output,
					" noundef %%%s",
					cg->identifiers[sig.arg_ids[i]]
				)) {
					fprintf(stderr, "Failed to Write to IR File!\n");
					*err = ERROR_IO;
					dynarr_clean(&vars);
					goto RET;
				}
			}

			if(i != sig.arg_count - 1) {
				if(fprintf(cg->output, ", ") < 0) {
					fprintf(stderr, "Failed to Write to IR File!\n");
					*err = ERROR_IO;
					dynarr_clean(&vars);
					goto RET;
				}
			}
		}
		FPUTS_OR_ERR(cg->output, ")\n");

		goto RET;
	}

	FPUTS_OR_ERR(cg->output,  "define dso_local ");
	print_type(cg->output, &scope.tc, sig.ret, err);
	if(*err) goto RET;

	FPRINTF_OR_ERR(cg->output, " @%s(", cg->identifiers[sig.id]);

	for(size_t i = 0; i	< sig.arg_count; i++) {
		if(sig.args[i].type == TYPE_SLICE_CONST
		   || sig.args[i].type == TYPE_SLICE_ABYSS
		   || sig.args[i].type == TYPE_SLICE_VAR
		) {
			if(fprintf(
				cg->output,
				"ptr noundef %%%s.0, i64 noundef %%%s.1\n",
				cg->identifiers[sig.arg_ids[i]],
				cg->identifiers[sig.arg_ids[i]]
			) < 0) {
				fprintf(stderr, "Failed to Write to IR File\n");
				*err = ERROR_IO;
				dynarr_clean(&vars);
				goto RET;
			}
		} else {
			print_type(cg->output, &scope.tc, sig.args[i], err);
			if(*err) {
				dynarr_clean(&vars);
			}

			if(!fprintf(
				cg->output,
				" noundef %%%s",
				cg->identifiers[sig.arg_ids[i]]
			)) {
				fprintf(stderr, "Failed to Write to IR File!\n");
				*err = ERROR_IO;
				dynarr_clean(&vars);
				goto RET;
			}
		}

		if(i != sig.arg_count - 1) {
			if(fprintf(cg->output, ", ") < 0) {
				fprintf(stderr, "Failed to Write to IR File!\n");
				*err = ERROR_IO;
				dynarr_clean(&vars);
				goto RET;
			}
		}

		dynarr_push(
			&vars,
			&(Var) {
				.id = sig.arg_ids[i],
				.type = sig.args[i],
				.mut = false,
				.declared = true,
				.arg = true,
			},
			err
		);
		if(*err) {
			dynarr_clean(&vars);
			goto RET;
		}
	}

	FPUTS_OR_ERR(cg->output, ") {\n");


	for(size_t i = 0; i < sig.arg_count; i ++) {
		if(sig.args[i].type == TYPE_SLICE_CONST
		   || sig.args[i].type == TYPE_SLICE_ABYSS
		   || sig.args[i].type == TYPE_SLICE_VAR
		) {
			FPRINTF_OR_ERR(
				cg->output,
				"%%%s.ptr = insertvalue {ptr, i64} poison, ptr %%%s.0, 0\n"
				"%%%s = insertvalue {ptr, i64} %%%s.ptr, i64 %%%s.1, 1\n",
				cg->identifiers[sig.arg_ids[i]],
				cg->identifiers[sig.arg_ids[i]],
				cg->identifiers[sig.arg_ids[i]],
				cg->identifiers[sig.arg_ids[i]],
				cg->identifiers[sig.arg_ids[i]]
			);
		}
	}

	for(size_t i = 0; i	< block.block.statement_count; i++)	{
		const AstNode *statement = &cg->nodes[block.block.statements[i]];

		if(statement->type == AST_VAR_DECL)	{
			Type type =	type_from_ast(
				&scope.tc,
				cg->nodes,
				statement->var_decl.data_type,
				err
			);
			if(type.type == TYPE_ARRAY && !type.array.len) {
				if(!statement->var_decl.initial) {
					fprintf(
						stderr,
						"Cannot Infer Length of Array '%s' without Initial "
						"value at ",
						cg->identifiers[statement->var_decl.id]
					);
					lexer_print_debug_to_file(
						stderr,
						&statement->debug.debug_info
					);
					fprintf(stderr, "\n");
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}
				Type init = type_of_expr(
					cg,
					&scope,
					statement->var_decl.initial,
					err
				);
				if(*err) goto RET;
				if(init.type != TYPE_ARRAY
				   || init.array.base != type.array.base
				) {
					fprintf(
						stderr,
						"Cannot Assign non-Array to Array-Type Variable '%s' "
						" at ",
						cg->identifiers[statement->var_decl.id]
					);
					lexer_print_debug_to_file(
						stderr,
						&statement->debug.debug_info
					);
					fprintf(stderr, "\n");
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}

				type.array.len = init.array.len;
			}
			if(*err) {
				if(vars.data) free(vars.data);
				goto RET;
			}

			if(!statement->var_decl.mut && !statement->var_decl.initial)	{
				fprintf(
					stderr,
					"Declaring Uninitialized const variable	'%s' at	",
					cg->identifiers[statement->var_decl.id]
				);
				lexer_print_debug_to_file(
					stderr,
					&statement->debug.debug_info
				);
				fprintf(stderr,	"\n");
				// No ERROR	because	compilation	can	continue
			}

			dynarr_push(
				&vars,
				&(Var) {
					.id	= statement->var_decl.id,
					.type =	type,
					.mut = statement->var_decl.mut,
					.declared =	false,
				},
				err
			);
			if(*err) goto RET; // realloc fail returns NULL
		}
	}

	scope.vars = vars.data;
	scope.var_count	= vars.count;

	size_t varnum =	sig.arg_count;
	size_t temp_counter	= 1;
	bool returned = false;
	for(size_t i = 0; i	< block.block.statement_count; i++)	{
		const AstNode *statement = &cg->nodes[block.block.statements[i]];

		switch(statement->type)	{
		case AST_VAR_DECL:
			scope.vars[varnum].declared	= true;

			FPRINTF_OR_ERR(
				cg->output,
				"%%%s = alloca ",
				cg->identifiers[statement->var_decl.id]
			);
			print_type(
				cg->output,
				&scope.tc,
				scope.vars[varnum].type,
				err
			);
			if(*err) goto RET;
			FPUTS_OR_ERR(cg->output, "\n");

			if(statement->var_decl.initial)	{
				gen_expr(
					cg,
					&cg->nodes[statement->var_decl.initial],
					scope.vars[varnum].type,
					&temp_counter,
					&scope,
					err
				);
				FPUTS_OR_ERR(cg->output, "store ");
				print_type(
					cg->output,
					&scope.tc,
					scope.vars[varnum].type,
					err
				);
				if(*err) goto RET;
				FPRINTF_OR_ERR(
					cg->output,
					" %%%zi, ptr %%%s\n",
					temp_counter - 1,
					cg->identifiers[scope.vars[varnum].id]
				);
			}
			varnum++;
			break;

		case AST_RET:
			if(statement->ret.return_val) {
				gen_expr(
					cg,
					&cg->nodes[statement->ret.return_val],
					sig.ret,
					&temp_counter,
					&scope,
					err
				);
				if(*err) goto RET;
				FPUTS_OR_ERR(cg->output, "ret ");
				print_type(cg->output, &scope.tc, sig.ret, err);
				if(*err) goto RET;
				FPRINTF_OR_ERR(cg->output, " %%%zi\n", temp_counter - 1);
			} else {
				if(sig.ret.type != TYPE_PRIMITIVE_VOID) {
					fprintf(
						stderr,
						"Function with a non-void Return Type returns void at "
					);
					lexer_print_debug_to_file(
						stderr,
						&statement->debug.debug_info
					);
					fprintf(stderr, "\n");
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}

				FPUTS_OR_ERR(cg->output, "ret void\n");
			}
			returned = true;
			break;

		case AST_ASSIGN:
		case AST_ADD_ASSIGN:
		case AST_SUB_ASSIGN:
		case AST_MUL_ASSIGN:
		case AST_DIV_ASSIGN:
			gen_assign(cg, statement, &temp_counter, &scope, err);
			if(*err) goto RET;
			break;

		case AST_FN_CALL:
			gen_fn_call(
				cg,
				statement,
				(Type) {.type =	TYPE_PRIMITIVE_VOID},
				&temp_counter,
				&scope,
				err
			);
			if(*err) goto RET;
			break;

		case AST_DISCARD:
			gen_expr(
				cg,
				&cg->nodes[statement->discard.value],
				(Type) {.type = TYPE_NONE},
				&temp_counter,
				&scope,
				err
			);
			if(*err) goto RET;
			break;

		default:
			fprintf(stderr,	"Expected Statement	at ");
			lexer_print_debug_to_file(
				stderr,
				&statement->debug.debug_info
			);
			fprintf(stderr,	"\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
	}

	if(!returned) {
		if(sig.ret.type != TYPE_PRIMITIVE_VOID) {
			fprintf(
				stderr,
				"Function with a non-void Return Type does not return at "
			);
			lexer_print_debug_to_file(stderr, &fn.debug.debug_info);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		FPUTS_OR_ERR(cg->output, "ret void\n");
	}

	FPUTS_OR_ERR(cg->output, "}\n\n");

RET:
	scope_clean(&scope);
	return;
}

void codegen_gen(CodeGen *cg, Error	*err)
{
	Scope global_scope;
	scope_init(&global_scope, NULL,	err);
	if(*err) goto RET;

	DynArr fn_sigs = {
		.data =	cg->fn_sigs,
		.elem_size = sizeof(*cg->fn_sigs),
		.count = 0,
		.capacity =	0,
	};

	FPRINTF_OR_ERR(
		cg->output,
		"source_filename = \"%s\"\n"
		"!0 = !{i32 8, !\"PIC Level\", i32 2}\n"
		"!1 = !{i32 7, !\"PIE Level\", i32 2}\n"
		"!2 = !{i32 7, !\"uwtable\", i32 2}\n"
		"!3 = !{i32 7, !\"frame-pointer\", i32 2}\n"
		"!4 = !{!\"wyrt version 0.1.0\"}\n"
		"!llvm.module.flags = !{!0, !1, !2, !3}\n"
		"!llvm.ident = !{!4}\n\n",
		cg->nodes[0].debug.debug_info.file
	);
	cg->metadata_counter = 5;

	for(size_t i = 0; i < cg->string_count; i++) {
		size_t len = strlen(cg->strings[i]) + 1;
		FPRINTF_OR_ERR(
			cg->output,
			"@.S%zi = internal constant [%zi x i8] c\"",
			i, len
		);
		for(size_t j = 0; j < len; j++) {
			switch(cg->strings[i][j]) {
			case '\n':
				FPUTS_OR_ERR(cg->output, "\\0A");
				break;
			case '\"':
				FPUTS_OR_ERR(cg->output, "\\22");
				break;
			default:
				do {} while(0);
				char c[2] = {cg->strings[i][j], 0};
				FPUTS_OR_ERR(cg->output, c);
				break;
			}
		}

		FPRINTF_OR_ERR(
			cg->output,
			"\\00\"\n"
			"@.SL%zi = internal constant i64 %zi\n",
			i, len
		);
	}

	assert(cg->nodes[0].type ==	AST_MODULE);
	size_t *statements = cg->nodes[0].module.statements;
	const size_t statement_count = cg->nodes[0].module.statement_count;

	for(size_t i = 0; i	< statement_count; i++)	{
		const size_t index = statements[i];
		if(cg->nodes[index].type ==	AST_FN_DEF)	{
			const AstNode *type	= &cg->nodes[
				cg->nodes[index].fn_def.fn_type
			];

			assert(type->type == AST_FN_TYPE);

			Type *args = malloc(type->fn_type.arg_count	* sizeof*args);
			CHECK_MALLOC(args);
			size_t *arg_ids = malloc(type->fn_type.arg_count * sizeof*arg_ids);
			CHECK_MALLOC(arg_ids);

			for(size_t j = 0; j	< type->fn_type.arg_count; j++)	{
				args[j]	= type_from_ast(
					&global_scope.tc,
					cg->nodes,
					type->fn_type.args[2*j+1],
					err
				);
				if(*err) {
					free(args);
					free(arg_ids);
					goto RET;
				}

				AstNode ident = cg->nodes[type->fn_type.args[2*j]];
				assert(ident.type == AST_IDENT);
				arg_ids[j] = ident.ident.id;
			}

			Type ret = type_from_ast(
				&global_scope.tc,
				cg->nodes,
				type->fn_type.ret_type,
				err
			);

			if(*err) {
				free(args);
				free(arg_ids);
				goto RET;
			}

			size_t id = cg->nodes[cg->nodes[index].fn_def.ident].ident.id;
			size_t linkage_name = SIZE_MAX;
		   	if(cg->nodes[cg->nodes[index].fn_def.block].type == AST_EXTERN) {
				const AstNode name = cg->nodes[cg->nodes[index].fn_def.block];
				linkage_name = cg->nodes[name.extrn.name].string_lit.id;
			}

			dynarr_push(
				&fn_sigs,
				&(FnSig) {
					.id = id,
					.linkage_name = linkage_name,
					.ret = ret,
					.arg_count = type->fn_type.arg_count,
					.args =	args,
					.arg_ids = arg_ids,
				},
				err
			);

			if(*err) {
				free(args);
				free(arg_ids);
				goto RET;
			}
		}
	}

	cg->fn_sigs	= fn_sigs.data;
	cg->fn_sig_count = fn_sigs.count;

	size_t fnnum = 0;
	for(size_t i = 0; i	< cg->nodes[0].module.statement_count; i++)	{
		const size_t index = statements[i];
		switch(cg->nodes[index].type) {
		case AST_FN_DEF:
			gen_fn_def(cg, index, fnnum, &global_scope, err);
			if(*err) goto RET;
			fnnum += 1;
			break;

		default:
			fprintf(stderr,	"Invalid Module-Level Statement	at ");
			lexer_print_debug_to_file(
				stderr,
				&cg->nodes[index].debug.debug_info
			);
			fprintf(stderr,	"\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
	}

RET:
	scope_clean(&global_scope);
	return;
}


void scope_init(Scope *scope, const	Scope *parent, Error *err)
{
	*scope = (Scope) { 0 };

	if(parent) {
		if(parent->params) {
			scope->params =	malloc(parent->param_count * sizeof*scope->params);
			CHECK_MALLOC(scope->params);
			memcpy(scope->params, parent->params, parent->param_count);
			scope->param_count = parent->param_count;
		}
		if(parent->vars) {
			scope->vars	= malloc(parent->var_count * sizeof*scope->vars);
			CHECK_MALLOC(scope->vars);
			memcpy(scope->vars,	parent->vars, parent->var_count);
			scope->var_count = parent->var_count;
		}
		if(parent->tc.types) {
			types_copy(&scope->tc, &parent->tc, err);
			if(*err) goto RET;
		}

	} else {
		types_init(&scope->tc, err);
		if(*err) goto RET;
	}

RET:
	if(*err) scope_clean(scope);
	return;
}

void scope_clean(const Scope *scope)
{
	if(scope->params) free(scope->params);
	if(scope->vars)	free(scope->vars);
	types_clean(&scope->tc);
}
