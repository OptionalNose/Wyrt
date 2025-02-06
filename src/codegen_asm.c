#include "codegen.h"

#include <string.h>
#include <stddef.h>

static void save_regs(FILE *file, PlatformType plat, Error *err)
{
	switch(plat) {
	case PLATFORM_LINUX:
		FPUTS_OR_ERR(
			file,
			"push rcx\n"
			"push rdx\n"
			"push rsi\n"
			"push rdi\n"
			"push r8\n"
			"push r9\n"
			"push r10\n"
			"push r11\n"
		);
		break;
	case PLATFORM_WINDOWS:
		FPUTS_OR_ERR(
			file,
			"push rcx\n"
			"push rdx\n"
			"push r8\n"
			"push r9\n"
			"push r10\n"
			"push r11\n"
		);
		break;
	}
RET:
	return;
}

static void restore_regs(FILE *file, PlatformType plat, Error *err)
{
	switch(plat) {
	case PLATFORM_LINUX:
		FPUTS_OR_ERR(
			file,
			"pop r11\n"
			"pop r10\n"
			"pop r9\n"
			"pop r8\n"
			"pop rdi\n"
			"pop rsi\n"
			"pop rdx\n"
			"pop rcx\n"
		);
		break;
	case PLATFORM_WINDOWS:
		FPUTS_OR_ERR(
			file,
			"pop r11\n"
			"pop r10\n"
			"pop r9\n"
			"pop r8\n"
			"pop rdx\n"
			"pop rcx\n"
		);
		break;
	}
RET:
	return;
}

static FnSig find_sig(size_t id, FnSig *sigs, size_t count, Error *err)
{
	FnSig sig = { 0 };
	bool found = false;

	for(size_t i = 0; i < count; i++) {
		if(id == sigs[i].id) {
			found = true;
			sig = sigs[i];
			break;
		}
	}

	if(!found) *err = ERROR_UNDEFINED;
	return sig;
}

static Var find_in_scope(CodeGen *cg, size_t id, const Scope *scope, const DebugInfo *debug, size_t *which, size_t *index, Error *err)
{	
	Var ret = { 0 };

	for(size_t i = 0; i < scope->reg_param_count; i++) {
		if(id == scope->reg_params[i].id) {
			*which = 0;
			*index = i;
			ret = scope->reg_params[i];
			goto RET;
		}
	}

	for(size_t i = 0; i < scope->stack_param_count; i++) {
		if(id == scope->stack_params[i].id) {
			*which = 1;
			*index = i;
			ret = scope->stack_params[i];
			goto RET;
		}
	}

	for(size_t i = 0; i < scope->var_count; i++) {
		if(id == scope->vars[i].id) {
			*which = 2;
			*index = i;
			ret = scope->vars[i];
			goto RET;
		}
	}

	fprintf(stderr, "Undeclared Variable '%s' at ", cg->identifiers[id]);
	lexer_print_debug_to_file(stderr, debug);
	fprintf(stderr, "\n");
	*err = ERROR_UNDEFINED;

RET:
	return ret;
}

static void print_in_scope(CodeGen *cg, const Scope *scope, size_t which, size_t index, Error *err)
{
	size_t size;

	switch(which) {
	case 0:
		size = types_get_size(&scope->tc, scope->reg_params[index].type);
		switch(cg->plat) {
		case PLATFORM_LINUX:
			switch(index) {
			case 0:
				switch(size) {
				case 1:
					FPUTS_OR_ERR(cg->output, "dil");
					break;
				case 2:
					FPUTS_OR_ERR(cg->output, "di");
					break;
				case 4:
					FPUTS_OR_ERR(cg->output, "edi");
					break;
				case 8:
					FPUTS_OR_ERR(cg->output, "rdi");
					break;
				}
				goto RET;
			case 1:
				switch(size) {
				case 1:
					FPUTS_OR_ERR(cg->output, "sil");
					break;
				case 2:
					FPUTS_OR_ERR(cg->output, "si");
					break;
				case 4:
					FPUTS_OR_ERR(cg->output, "esi");
					break;
				case 8:
					FPUTS_OR_ERR(cg->output, "rsi");
					break;
				}
				goto RET;
			case 2:
				switch(size) {
				case 1:
					FPUTS_OR_ERR(cg->output, "dl");
					break;
				case 2:
					FPUTS_OR_ERR(cg->output, "dx");
					break;
				case 4:
					FPUTS_OR_ERR(cg->output, "edx");
					break;
				case 8:
					FPUTS_OR_ERR(cg->output, "rdx");
					break;
				}
				goto RET;
			case 3:
				switch(size) {
				case 1:
					FPUTS_OR_ERR(cg->output, "cl");
					break;
				case 2:
					FPUTS_OR_ERR(cg->output, "cx");
					break;
				case 4:
					FPUTS_OR_ERR(cg->output, "ecx");
					break;
				case 8:
					FPUTS_OR_ERR(cg->output, "rcx");
					break;
				}
				goto RET;
			case 4:
				switch(size) {
				case 1:
					FPUTS_OR_ERR(cg->output, "r8b");
					break;
				case 2:
					FPUTS_OR_ERR(cg->output, "r8w");
					break;
				case 4:
					FPUTS_OR_ERR(cg->output, "r8d");
					break;
				case 8:
					FPUTS_OR_ERR(cg->output, "r8");
					break;
				}
				goto RET;
			case 5:
				switch(size) {
				case 1:
					FPUTS_OR_ERR(cg->output, "r9b");
					break;
				case 2:
					FPUTS_OR_ERR(cg->output, "r9w");
					break;
				case 4:
					FPUTS_OR_ERR(cg->output, "r9d");
					break;
				case 8:
					FPUTS_OR_ERR(cg->output, "r9");
					break;
				}
				goto RET;
			}
			break;
		case PLATFORM_WINDOWS:
			switch(index) {
			case 0:
				switch(size) {
				case 1:
					FPUTS_OR_ERR(cg->output, "cl");
					break;
				case 2:
					FPUTS_OR_ERR(cg->output, "cx");
					break;
				case 4:
					FPUTS_OR_ERR(cg->output, "ecx");
					break;
				case 8:
					FPUTS_OR_ERR(cg->output, "rcx");
					break;
				}
				goto RET;
			case 1:
				switch(size) {
				case 1:
					FPUTS_OR_ERR(cg->output, "dl");
					break;
				case 2:
					FPUTS_OR_ERR(cg->output, "dx");
					break;
				case 4:
					FPUTS_OR_ERR(cg->output, "edx");
					break;
				case 8:
					FPUTS_OR_ERR(cg->output, "rdx");
					break;
				}
				goto RET;
			case 2:
				switch(size) {
				case 1:
					FPUTS_OR_ERR(cg->output, "r8b");
					break;
				case 2:
					FPUTS_OR_ERR(cg->output, "r8w");
					break;
				case 4:
					FPUTS_OR_ERR(cg->output, "r8d");
					break;
				case 8:
					FPUTS_OR_ERR(cg->output, "r8");
					break;
				}
				goto RET;
			case 3:
				switch(size) {
				case 1:
					FPUTS_OR_ERR(cg->output, "r9b");
					break;
				case 2:
					FPUTS_OR_ERR(cg->output, "r9w");
					break;
				case 4:
					FPUTS_OR_ERR(cg->output, "r9d");
					break;
				case 8:
					FPUTS_OR_ERR(cg->output, "r9");
					break;
				}
				goto RET;
			}
			break;
		}
	case 1:
		FPRINTF_OR_ERR(cg->output, "[rbp+%zi]", scope->stack_params[index].start);
		break;
	case 2:
		FPRINTF_OR_ERR(cg->output, "[rbp-%zi]", -1 * scope->vars[index].start);
		break;
	}

RET:
	return;
}

static void gen_expr(CodeGen *cg, size_t index, Type expected, Scope *scope, Error *err);

static void gen_fn_call(CodeGen *cg, size_t index, Scope *scope, Error *err)
{
	const AstNode *call = &cg->nodes[index];

	FnSig sig = find_sig(call->fn_call.fn_id, cg->fn_sigs, cg->fn_sig_count, err);
	if(*err) {
		fprintf(stderr, "Undeclared function '%s' at ", cg->identifiers[call->fn_call.fn_id]);
		lexer_print_debug_to_file(stderr, &call->fn_call.debug_info); 
		fprintf(stderr, "\n");
		*err = ERROR_UNDEFINED;
		goto RET;
	}

	size_t arg_count = call->fn_call.arg_count;

	if(arg_count != sig.arg_count) {
		fprintf(stderr, "Incorrect Number of Function Arguments at ");
		lexer_print_debug_to_file(stderr, &call->fn_call.debug_info);
		fprintf(stderr, " expected %zi, found %zi\n", sig.arg_count, arg_count);
		*err = ERROR_UNDEFINED;
		goto RET;
	}

	switch(cg->plat) {
	case PLATFORM_LINUX:
		do {} while(0);

		size_t reg_params = 0;
		size_t end_of_regs = 0;
		bool slice_split = false; 
		for(size_t i = 0; i < arg_count; i++) {
			Type type = type_from_ast(&scope->tc, cg->nodes, sig.args[i], err);
			if(*err) goto RET;

			size_t param_size = types_get_size(&scope->tc, type);
			if(reg_params <= 5 && param_size <= 8) {
				gen_expr(cg, call->fn_call.args[i], type, scope, err);
				if(*err) goto RET;
				switch(reg_params) {
				case 0:
					FPUTS_OR_ERR(cg->output, "mov rdi, rax\n");
					break;
				case 1:
					FPUTS_OR_ERR(cg->output, "mov rsi, rax\n");
					break;
				case 2:
					FPUTS_OR_ERR(cg->output, "mov rdx, rax\n");
					break;
				case 3:
					FPUTS_OR_ERR(cg->output, "mov rcx, rax\n");
					break;
				case 4:
					FPUTS_OR_ERR(cg->output, "mov r8, rax\n");
					break;
				case 5:
					FPUTS_OR_ERR(cg->output, "mov r9, rax\n");
					break;
				}
				reg_params += 1;
			} else if(
				type.type == TYPE_SLICE_CONST
				|| type.type == TYPE_SLICE_VAR
				|| type.type == TYPE_SLICE_ABYSS
			) {
				switch(reg_params) {
				case 0:
					gen_expr(cg, call->fn_call.args[i], type, scope, err);
					if(*err) goto RET;
					FPUTS_OR_ERR(
						cg->output,
						"pop rdi\n"
						"pop rsi\n"
					);
					reg_params += 2;
					break;
				case 1:
					gen_expr(cg, call->fn_call.args[i], type, scope, err);
					if(*err) goto RET;
					FPUTS_OR_ERR(
						cg->output,
						"pop rsi\n"
						"pop rdx\n"
					);
					reg_params += 2;
					break;
				case 2:
					gen_expr(cg, call->fn_call.args[i], type, scope, err);
					if(*err) goto RET;
					FPUTS_OR_ERR(
						cg->output,
						"pop rdx\n"
						"pop rcx\n"
					);
					reg_params += 2;
					break;
				case 3:
					gen_expr(cg, call->fn_call.args[i], type, scope, err);
					if(*err) goto RET;
					FPUTS_OR_ERR(
						cg->output,
						"pop rcx\n"
						"pop r8\n"
					);
					reg_params += 2;
					break;
				case 4:
					gen_expr(cg, call->fn_call.args[i], type, scope, err);
					if(*err) goto RET;
					FPUTS_OR_ERR(
						cg->output,
						"pop r8\n"
						"pop r9\n"
					);
					reg_params += 2;
					break;
				case 5:
					reg_params += 1;
					slice_split = true;
					break;
				}
			}

			if(reg_params > 5) {
				end_of_regs = i;
				break;
			}
		}

		size_t stack_arg_size = 0;
		for(size_t i = arg_count - 1; i >= 0; i--) {
			Type type = type_from_ast(&scope->tc, cg->nodes, sig.args[i], err);
			if(*err) goto RET;
			size_t param_size = types_get_size(&scope->tc, type);
			
			if(param_size <= 8 && i <= end_of_regs) {
				if(i == 0) break;
				else continue;
			}

			gen_expr(cg, call->fn_call.args[i], type, scope, err); 
			if(*err) goto RET;

			if(param_size <= 8) { //gen_expr places size >8 on stack
				FPUTS_OR_ERR(cg->output, "push rax\n");
			}
			stack_arg_size += param_size > 8 ? param_size : 8;

			if(i == 0) break;
		}

		if(slice_split) {
			Type type = type_from_ast(&scope->tc, cg->nodes, sig.args[end_of_regs], err);
			if(*err) goto RET;
			gen_expr(cg, call->fn_call.args[end_of_regs], type, scope, err);
			if(*err) goto RET;

			FPUTS_OR_ERR(
				cg->output,
				"pop r9\n"
			);
			// len left on stack, where it needs to be as arg
			
			stack_arg_size += 8;
		}

		if(stack_arg_size) {
			FPRINTF_OR_ERR(
				cg->output,
				"call %s\n"
				"add rsp, %zi\n",
				cg->identifiers[call->fn_call.fn_id],
				stack_arg_size
			);
		} else {
			FPRINTF_OR_ERR(
				cg->output,
				"call %s\n",
				cg->identifiers[call->fn_call.fn_id]
			);
		}
		break;

	case PLATFORM_WINDOWS:
		do {} while(0);

		reg_params = 0;
		end_of_regs = 0;
		slice_split = false;
		for(size_t i = 0; i < arg_count; i++) {
			Type type = type_from_ast(&scope->tc, cg->nodes, sig.args[i], err);
			if(*err) goto RET;

			size_t param_size = types_get_size(&scope->tc, type);
			if(reg_params <= 3 && param_size <= 8) {
				gen_expr(cg, call->fn_call.args[i], type, scope, err);
				if(*err) goto RET;
				switch(i) {
				case 0:
					FPUTS_OR_ERR(cg->output, "mov rcx, rax\n");
					break;
				case 1:
					FPUTS_OR_ERR(cg->output, "mov rdx, rax\n");
					break;
				case 2:
					FPUTS_OR_ERR(cg->output, "mov r8, rax\n");
					break;
				case 3:
					FPUTS_OR_ERR(cg->output, "mov r9, rax\n");
					break;
				}
				reg_params += 1;
			} else if(
				type.type == TYPE_SLICE_CONST
				|| type.type == TYPE_SLICE_ABYSS
				|| type.type == TYPE_SLICE_VAR
			) {
				switch(reg_params) {
				case 0:
					gen_expr(cg, call->fn_call.args[i], type, scope, err);
					if(*err) goto RET;
					FPUTS_OR_ERR(
						cg->output,
						"pop rcx\n"
						"pop rdx\n"
					);
					reg_params += 2;
					break;
				case 1:
					gen_expr(cg, call->fn_call.args[i], type, scope, err);
					if(*err) goto RET;
					FPUTS_OR_ERR(
						cg->output,
						"pop rdx\n"
						"pop r8\n"
					);
					reg_params += 2;
					break;
				case 2:
					gen_expr(cg, call->fn_call.args[i], type, scope, err);
					if(*err) goto RET;
					FPUTS_OR_ERR(
						cg->output,
						"pop r8\n"
						"pop r9\n"
					);
					reg_params += 2;
					break;
				case 3:
					slice_split = true;
					reg_params += 1;
					break;
				}
			}

			if(reg_params > 3) {
				end_of_regs = i;
				break;
			}
		}

		stack_arg_size = 0;
		for(size_t i = arg_count - 1; i >= 0; i--) {
			Type type = type_from_ast(&scope->tc, cg->nodes, sig.args[i], err);
			if(*err) goto RET;
			size_t param_size = types_get_size(&scope->tc, type);
			
			if(param_size <= 8 && i <= end_of_regs) {
				if(i == 0) break;
				else continue;
			}

			gen_expr(cg, call->fn_call.args[i], type, scope, err); //places size > 8 on stack
			if(*err) goto RET;

			if(param_size <= 8) { //places size >8 on stack
				FPUTS_OR_ERR(cg->output, "push rax\n");
			}

			stack_arg_size += param_size > 8 ? param_size : 8;

			if(i == 0) break;
		}

		if(slice_split) {
			Type type = type_from_ast(&scope->tc, cg->nodes, sig.args[end_of_regs], err);
			if(*err) goto RET;
			gen_expr(cg, call->fn_call.args[end_of_regs], type, scope, err);
			if(*err) goto RET;

			FPUTS_OR_ERR(
				cg->output,
				"pop r9\n"
			);
			// len left on stack, where it needs to be as arg
			
			stack_arg_size += 8;
		}

		if(stack_arg_size) {
			FPRINTF_OR_ERR(
				cg->output,
				"call %s\n"
				"add rsp, %zi\n",
				cg->identifiers[call->fn_call.fn_id],
				stack_arg_size
			);
		} else {
			FPRINTF_OR_ERR(
				cg->output,
				"call %s\n",
				cg->identifiers[call->fn_call.fn_id]
			);
		}
		break;
	}



RET:
	return;
}

static void gen_expr(CodeGen *cg, size_t index, Type expected, Scope *scope, Error *err)
{
	const AstNode *expr = &cg->nodes[index];

	const char *raxstr;
	const char *r10str;
	switch(types_get_size(&scope->tc, expected)) {
	case 1:
		raxstr = "al";
		r10str = "r10b";
		break;
	case 2:
		raxstr = "ax";
		r10str = "r10w";
		break;
	case 4:
		raxstr = "eax";
		r10str = "r10d";
		break;
	case 8:
		raxstr = "rax";
		r10str = "r10";
		break;
	}

	switch(expr->type) {
	case AST_ADD:
		if(!type_is_arithmetic(expected)){
			fprintf(stderr, "Cannot Perform Addition on Non-Arithmetic Type ");
			type_print(stderr, &scope->tc, expected);
			fprintf(stderr, " at ");
			lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
		gen_expr(cg, expr->binop.lhs, expected, scope, err);
		if(*err) goto RET;

		switch(cg->nodes[expr->binop.rhs].type) {
		case AST_INT_LIT:
			FPRINTF_OR_ERR(
				cg->output,
				"add %s, %zi\n",
				raxstr,
				cg->nodes[expr->binop.rhs].int_lit.val
			);
			break;

		case AST_IDENT:
			FPRINTF_OR_ERR(cg->output, "add %s, ", raxstr);
			size_t rhs_which, rhs_index;
			Var rhs = find_in_scope(
				cg,
				cg->nodes[expr->binop.rhs].ident.id,
				scope,
				&cg->nodes[expr->binop.rhs].debug.debug_info,
				&rhs_which, &rhs_index, 
				err
			);

			if(!types_are_equal(rhs.type, expected)) {
				fprintf(stderr, "Mismatched types: Expected ");
				type_print(stderr, &scope->tc, expected);
				fprintf(stderr, " found ");
				type_print(stderr, &scope->tc, rhs.type);
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			print_in_scope(cg, scope, rhs_which, rhs_index, err);
			if(*err) goto RET;
			FPUTS_OR_ERR(cg->output, "\n");
			break;

		default:
			FPUTS_OR_ERR(cg->output, "push rax\n");
			gen_expr(cg, expr->binop.rhs, expected, scope, err);
			if(*err) goto RET;

			FPRINTF_OR_ERR(
				cg->output,
				"mov %s, %s\n"
				"pop rax\n"
				"add %s, %s\n",
				r10str, raxstr,
				raxstr, r10str
			);
			break;
		}
		break;
	
	case AST_SUB:
		if(!type_is_arithmetic(expected)){
			fprintf(stderr, "Cannot Perform Addition on Non-Arithmetic Type ");
			type_print(stderr, &scope->tc, expected);
			fprintf(stderr, " at ");
			lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
		gen_expr(cg, expr->binop.lhs, expected, scope, err);
		if(*err) goto RET;

		switch(cg->nodes[expr->binop.rhs].type) {
		case AST_INT_LIT:
			FPRINTF_OR_ERR(
				cg->output,
				"sub %s, %zi\n",
				raxstr,
				cg->nodes[expr->binop.rhs].int_lit.val
			);
			break;

		case AST_IDENT:
			FPRINTF_OR_ERR(cg->output, "sub %s, ", raxstr);
			size_t rhs_which, rhs_index;
			Var rhs = find_in_scope(
				cg,
				cg->nodes[expr->binop.rhs].ident.id,
				scope,
				&cg->nodes[expr->binop.rhs].debug.debug_info,
				&rhs_which, &rhs_index, 
				err
			);

			if(!types_are_equal(rhs.type, expected)) {
				fprintf(stderr, "Mismatched types: Expected ");
				type_print(stderr, &scope->tc, expected);
				fprintf(stderr, " found ");
				type_print(stderr, &scope->tc, rhs.type);
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			print_in_scope(cg, scope, rhs_which, rhs_index, err);
			if(*err) goto RET;
			FPUTS_OR_ERR(cg->output, "\n");
			break;

		default:
			FPUTS_OR_ERR(cg->output, "push rax\n");
			gen_expr(cg, expr->binop.rhs, expected, scope, err);
			if(*err) goto RET;

			FPRINTF_OR_ERR(
				cg->output,
				"mov %s, %s\n"
				"pop rax\n"
				"add %s, %s\n",
				r10str, raxstr,
				raxstr, r10str
			);
			break;
		}
		break;
	
	case AST_MUL:
		if(!type_is_arithmetic(expected)){
			fprintf(stderr, "Cannot Perform Addition on Non-Arithmetic Type ");
			type_print(stderr, &scope->tc, expected);
			fprintf(stderr, " at ");
			lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
		gen_expr(cg, expr->binop.lhs, expected, scope, err);
		if(*err) goto RET;

		switch(cg->nodes[expr->binop.rhs].type) {
		case AST_INT_LIT:
			FPRINTF_OR_ERR(
				cg->output,
				"mov %s, %zi\n",
				r10str,
				cg->nodes[expr->binop.rhs].int_lit.val
			);
			break;

		case AST_IDENT:
			FPRINTF_OR_ERR(cg->output, "mov %s, ", r10str);
			size_t rhs_which, rhs_index;
			Var rhs = find_in_scope(
				cg,
				cg->nodes[expr->binop.rhs].ident.id,
				scope,
				&cg->nodes[expr->binop.rhs].debug.debug_info,
				&rhs_which, &rhs_index, 
				err
			);

			if(!types_are_equal(rhs.type, expected)) {
				fprintf(stderr, "Mismatched types: Expected ");
				type_print(stderr, &scope->tc, expected);
				fprintf(stderr, " found ");
				type_print(stderr, &scope->tc, rhs.type);
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			print_in_scope(cg, scope, rhs_which, rhs_index, err);
			if(*err) goto RET;
			FPUTS_OR_ERR(cg->output, "\n");
			break;

		default:
			FPUTS_OR_ERR(cg->output, "push rax\n");

			gen_expr(cg, expr->binop.rhs, expected, scope, err);
			if(*err) goto RET;

			FPRINTF_OR_ERR(
				cg->output,
				"mov %s, %s\n"
				"pop rax\n",
				r10str, raxstr
			);
			break;
		}

		FPRINTF_OR_ERR(
			cg->output,
			"push rdx\n"
			"xor edx, edx\n"
			"mul %s\n"
			"pop rdx\n",
			r10str
		);
		break;

	case AST_DIV:
		if(!type_is_arithmetic(expected)){
			fprintf(stderr, "Cannot Perform Addition on Non-Arithmetic Type ");
			type_print(stderr, &scope->tc, expected);
			fprintf(stderr, " at ");
			lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
		gen_expr(cg, expr->binop.lhs, expected, scope, err);
		if(*err) goto RET;

		switch(cg->nodes[expr->binop.rhs].type) {
		case AST_INT_LIT:
			FPRINTF_OR_ERR(
				cg->output,
				"mov %s, %zi\n",
				r10str,
				cg->nodes[expr->binop.rhs].int_lit.val
			);
			break;

		case AST_IDENT:
			FPRINTF_OR_ERR(cg->output, "mov %s, ", r10str);
			size_t rhs_which, rhs_index;
			Var rhs = find_in_scope(
				cg,
				cg->nodes[expr->binop.rhs].ident.id,
				scope,
				&cg->nodes[expr->binop.rhs].debug.debug_info,
				&rhs_which, &rhs_index, 
				err
			);

			if(!types_are_equal(rhs.type, expected)) {
				fprintf(stderr, "Mismatched types: Expected ");
				type_print(stderr, &scope->tc, expected);
				fprintf(stderr, " found ");
				type_print(stderr, &scope->tc, rhs.type);
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			print_in_scope(cg, scope, rhs_which, rhs_index, err);
			if(*err) goto RET;
			FPUTS_OR_ERR(cg->output, "\n");
			break;

		default:
			FPUTS_OR_ERR(cg->output, "push rax\n");

			gen_expr(cg, expr->binop.rhs, expected, scope, err);
			if(*err) goto RET;

			FPRINTF_OR_ERR(
				cg->output,
				"mov %s, %s\n"
				"pop rax\n",
				r10str, raxstr
			);
			break;
		}

		FPRINTF_OR_ERR(
			cg->output,
			"push rdx\n"
			"xor edx, edx\n"
			"div %s\n"
			"pop rdx\n",
			r10str
		);
		break;
	
	case AST_INT_LIT:
		FPRINTF_OR_ERR(
			cg->output,
			"mov %s, %zi\n",
			raxstr, expr->int_lit.val
		);
		break;
	
	case AST_IDENT:
		do {} while(0);

		size_t ident_which, ident_index;
		Var var = find_in_scope(
			cg,
			expr->ident.id,
			scope,
			&expr->debug.debug_info,
			&ident_which, &ident_index, 
			err
		);

		if(!types_are_equal(var.type, expected)) {
			fprintf(stderr, "Mismatched types: Expected ");
			type_print(stderr, &scope->tc, expected);
			fprintf(stderr, " found ");
			type_print(stderr, &scope->tc, var.type);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		size_t var_size = types_get_size(&scope->tc, var.type);
		if(var_size <= 8) {
			FPRINTF_OR_ERR(cg->output, "mov %s, ", raxstr);
			print_in_scope(cg, scope, ident_which, ident_index, err);
			if(*err) goto RET;
			FPUTS_OR_ERR(cg->output, "\n");
		} else {
			if(var.start < 0) {
				size_t i;
				for(i = 0; i < var_size / 8; i++) {
					FPRINTF_OR_ERR(
						cg->output,
						"push qword [rbp-%zi]\n",
						(-1 * var.start) + 8*i
					);
				}

				switch(var_size % 8) {
				case 0:
					break;
				case 1:
					FPRINTF_OR_ERR(
						cg->output,
						"push byte [rbp-%zi]\n",
						(-1 * var.start) + 8*i
					);
					break;
				case 2:
					FPRINTF_OR_ERR(
						cg->output,
						"push word [rbp-%zi]\n",
						(-1 * var.start) + 8*i
					);
					break;
				case 4:
					FPRINTF_OR_ERR(
						cg->output,
						"push dword [rbp-%zi]\n",
						(-1 * var.start) + 8*i
					);
					break;
				}
			} else if(var.start > 0) {
				size_t i;
				for(i = 0; i < var_size / 8; i++) {
					FPRINTF_OR_ERR(
						cg->output,
						"push qword [rbp+%zi]\n",
						var.start - 8*i
					);
				}
			} else {
				fprintf(stderr, "[INTERNAL]: Register Paramater of size > 8 at ");
				lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
		}
		break;
	
	case AST_DEREF:
		if(cg->nodes[expr->deref.ptr].type != AST_IDENT) {
			fprintf(stderr, "Expected Indentifier for Dereference at ");
			lexer_print_debug_to_file(stderr, &cg->nodes[expr->deref.ptr].debug.debug_info);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		size_t deref_which, deref_index;
		Var addr = find_in_scope(
			cg,
			cg->nodes[expr->deref.ptr].ident.id,
			scope,
			&cg->nodes[expr->deref.ptr].debug.debug_info,
			&deref_which,
			&deref_index,
			err
		);
		if(*err) goto RET;

		if(
			addr.type.type != TYPE_POINTER_CONST
			&& addr.type.type != TYPE_POINTER_VAR
		) {
			if(addr.type.type == TYPE_POINTER_ABYSS) {
				fprintf(stderr, "Error: Attempting to Read value from Abyssal Pointer at ");
				lexer_print_debug_to_file(stderr, &cg->nodes[expr->deref.ptr].debug.debug_info);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
			fprintf(stderr, "Expected Pointer Type for Dereference at ");
			lexer_print_debug_to_file(stderr, &cg->nodes[expr->deref.ptr].debug.debug_info);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		const char *targsize;
		switch(types_get_size(&scope->tc, addr.type)) {
		case 1:
			targsize = "byte";
			break;
		case 2:
			targsize = "word";
			break;
		case 4:
			targsize = "dword";
			break;
		case 8:
			targsize = "qword";
			break;
		}

		FPRINTF_OR_ERR(
			cg->output,
			"mov rax, %s [",
			targsize
		);
		print_in_scope(cg, scope, deref_which, deref_index, err);
		if(*err) goto RET;
		FPUTS_OR_ERR(cg->output, "]\n");
		break;
	
	case AST_SUBSCRIPT:
		do {} while(0);
		
		if(cg->nodes[expr->subscript.arr].type != AST_IDENT) {
			fprintf(stderr, "Error: Cannot Subscript into non-Identifier at ");
			lexer_print_debug_to_file(stderr, &expr->subscript.debug_info);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		Var subscripted_var;
		{
			size_t which, index;
			subscripted_var = find_in_scope(
				cg,
				cg->nodes[expr->subscript.arr].ident.id,
				scope,
				&cg->nodes[expr->subscript.arr].ident.debug_info,
				&which, &index,
				err
			);
		}

		switch(subscripted_var.type.type) {
		case TYPE_ARRAY:
			if(!types_are_equal(scope->tc.types[subscripted_var.type.array.base], expected)) {
				fprintf(stderr, "Error: Expected Type ");
				type_print(stderr, &scope->tc, expected);
				fprintf(stderr, " found ");
				type_print(stderr, &scope->tc, scope->tc.types[subscripted_var.type.array.base]);
				fprintf(stderr, " at ");
				lexer_print_debug_to_file(stderr, &expr->debug.debug_info); 
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
			break;

		case TYPE_SLICE_CONST:
		case TYPE_SLICE_VAR:
			if(!types_are_equal(scope->tc.types[subscripted_var.type.slice.base], expected)) {
				fprintf(stderr, "Error: Expected Type ");
				type_print(stderr, &scope->tc, expected);
				fprintf(stderr, " found ");
				type_print(stderr, &scope->tc, scope->tc.types[subscripted_var.type.slice.base]);
				fprintf(stderr, " at ");
				lexer_print_debug_to_file(stderr, &expr->debug.debug_info); 
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
			break;

		case TYPE_SLICE_ABYSS:
			fprintf(stderr, "Error: Cannot Take Value of Element of Abyssal Slice at ");
			lexer_print_debug_to_file(stderr, &expr->debug.debug_info); 
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;

		default:
			fprintf(stderr, "Expected Array or Slice Type at ");
			lexer_print_debug_to_file(stderr, &expr->debug.debug_info); 
			fprintf(stderr, " found ");
			type_print(stderr, &scope->tc, subscripted_var.type);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		gen_expr(cg, expr->subscript.arr, subscripted_var.type, scope, err);
		if(*err) goto RET;
		
		gen_expr(cg, expr->subscript.index, (Type) { .type = TYPE_PRIMITIVE_U64 }, scope, err);
		if(*err) goto RET;
		
		size_t expected_size = types_get_size(&scope->tc, expected);

		switch(expected_size) {
		case 1:
			FPUTS_OR_ERR(
				cg->output,
				"mov r11, rax\n"
				"pop r10\n"
				"pop rax\n"
				"mov al, [r10+r11]\n"
			);
			break;
		case 2:
			FPUTS_OR_ERR(
				cg->output,
				"mov r11, rax\n"
				"pop r10\n"
				"pop rax\n"
				"mov ax, [r10+r11*2]\n"
			);
			break;
		case 4:
			FPUTS_OR_ERR(
				cg->output,
				"mov r11, rax\n"
				"pop r10\n"
				"pop rax\n"
				"mov eax, [r10+r11*4]\n"
			);
			break;
		case 8:
			FPUTS_OR_ERR(
				cg->output,
				"mov r11, rax\n"
				"pop r10\n"
				"pop rax\n"
				"mov rax, [r10+r11*8]\n"
			);
			break;
		default:
			FPUTS_OR_ERR(
				cg->output,
				"mov r11, rax\n"
				"pop r10\n"
				"pop rax\n"
			);

			for(size_t i = 0; i <= expected_size / 8; i++) {
				FPUTS_OR_ERR(
					cg->output,
					"push qword [r10+r11*8]\n"
					"inc r11\n"
				);
			}

			switch(expected_size % 8) {
			case 0:
				break;
			case 1:
				FPUTS_OR_ERR(cg->output, "push byte [r10+r11*8]\n");
				break;
			case 2:
				FPUTS_OR_ERR(cg->output, "push word [r10+r11*8]\n");
				break;
			case 4:
				FPUTS_OR_ERR(cg->output, "push dword [r10+r11*8]\n");
				break;
			}
			break;
		}
		break;

	case AST_FN_CALL:
		save_regs(cg->output, cg->plat, err);
		if(*err) goto RET;

		gen_fn_call(cg, index, scope, err);
		if(*err) goto RET;

		restore_regs(cg->output, cg->plat, err);
		if(*err) goto RET;

		break;

	case AST_ADDR:
		if(cg->nodes[expr->addr.base].type != AST_IDENT) {
			fprintf(stderr, "Cannot take Address of non-Identifier at ");
			lexer_print_debug_to_file(stderr, &cg->nodes[expr->addr.base].debug.debug_info);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		size_t addr_targ = SIZE_MAX;
		for(size_t i = 0; i < scope->var_count; i++) {
			if(cg->nodes[expr->addr.base].ident.id == scope->vars[i].id) {
				addr_targ = i;
			}
		}
		
		if(addr_targ == SIZE_MAX) {
			fprintf(stderr, "Undeclared Identifier at ");
			lexer_print_debug_to_file(stderr, &cg->nodes[expr->addr.base].debug.debug_info);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
		
		switch(expected.type) {
		case TYPE_POINTER_CONST:
		case TYPE_POINTER_VAR:
		case TYPE_POINTER_ABYSS:
			FPRINTF_OR_ERR(
				cg->output,
				"lea rax, [rbp-%zi]\n",
				-1 * scope->vars[addr_targ].start
			);
			break;
		case TYPE_SLICE_CONST:
		case TYPE_SLICE_VAR:
		case TYPE_SLICE_ABYSS:
			if(scope->vars[addr_targ].type.type != TYPE_ARRAY) {
				fprintf(stderr, "Error: Cannot obtain Slice from Address of non-Array type ");
				type_print(stderr, &scope->tc, scope->vars[addr_targ].type);
				fprintf(stderr, " at ");
				lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			FPRINTF_OR_ERR(
				cg->output,
				"push qword %zi\n"
				"lea rax, [rbp-%zi]\n"
				"push rax\n",
				scope->vars[addr_targ].type.array.len,
				-1 * scope->vars[addr_targ].start
			 );
			break;	
		default:
			fprintf(stderr, "Error: Attempting to Obtain non-Address Type ");
			type_print(stderr, &scope->tc, expected);
			fprintf(stderr, " at ");
			lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
		break;

	case AST_ARRAY_LIT:
		if(expected.type != TYPE_ARRAY) {
			fprintf(stderr, "Error: Expected non-Array Type ");
			type_print(stderr, &scope->tc, expected);
			fprintf(stderr, " found Array Literal at ");
			lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		if(expected.array.len != expr->array_lit.elem_count) {
			fprintf(
				stderr,
				"Error: Expected %zi-element Array, found %zi-element Array at ",
				expected.array.len, expr->array_lit.elem_count
			);
			lexer_print_debug_to_file(stderr, &expr->debug.debug_info);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		Type elem_type = scope->tc.types[expected.array.base];
		size_t elem_size = types_get_size(&scope->tc, elem_type);
		for(size_t i = expected.array.len - 1; i >= 0; i--) {
			gen_expr(cg, expr->array_lit.elems[i], elem_type, scope, err);
			if(*err) goto RET;
			if(elem_size <= 8) {
				FPUTS_OR_ERR(cg->output, "push rax\n");
			} 
			// gen_expr places size > 8 on stack already
			if(i == 0) break;
		}
		break;

	default:
		INVALID_AST_NODE("Expected Expression", expr);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

RET:
	return;
}

static void gen_fn_def(CodeGen *cg, size_t index, Error *err)
{
	DynArr vars = { 0 };
	dynarr_init(&vars, sizeof (Var));

	DynArr reg_params = { 0 };
	dynarr_init(&reg_params, sizeof (Var));

	DynArr stack_params = { 0 };
	dynarr_init(&stack_params, sizeof (Var));

	TypeContext tc;
	types_init(&tc, err);

	const AstNode *fn_def = &cg->nodes[index];
	const AstNode *ident = &cg->nodes[fn_def->fn_def.ident];
	const AstNode *fn_type = &cg->nodes[fn_def->fn_def.fn_type];

	if(fn_type->type != AST_FN_TYPE) {
		INVALID_AST_NODE("Expected Function Type", fn_type);
	}

	Type ret_type = type_from_ast(&tc, cg->nodes, fn_type->fn_type.ret_type, err);
	if(*err) goto RET;

	for(size_t i = 0; i < fn_type->fn_type.arg_count; i++) {
		const AstNode *arg_ident = &cg->nodes[fn_type->fn_type.args[2*i]];

		Type t = type_from_ast(&tc, cg->nodes, fn_type->fn_type.args[2*i + 1], err);
		if(*err) goto RET;

		size_t type_size = types_get_size(&tc, t);

		switch(cg->plat) {
		case PLATFORM_LINUX:
			if(type_size <= 8 && reg_params.count <= 5) {
				dynarr_push(
					&reg_params,
					&(Var) {
						.id = arg_ident->ident.id,
						.type = t,
						.mut = false,
						.start = 0,
					},
					err
				);
				if(*err) goto RET;
			} else {
				ptrdiff_t last_start;
				if(stack_params.count >= 1) {
					Var last_param = *(Var*)dynarr_from_back(&stack_params, 0);
					last_start = last_param.start;
				} else {
					last_start = 8;
				}

				if(type_size < 8) type_size = 8;

				dynarr_push(
					&stack_params,
					&(Var) {
						.id = arg_ident->ident.id,
						.type = t,
						.mut = false,
						.start = last_start + type_size,
					},
					err
				);
				if(*err) goto RET;
			}
			break;
		case PLATFORM_WINDOWS:
			if(type_size <= 8 && reg_params.count <= 3) {
				dynarr_push(
					&reg_params,
					&(Var) {
						.id = arg_ident->ident.id,
						.type = t,
						.mut = false,
						.start = 0,
					},
					err
				);
				if(*err) goto RET;
			} else {
				ptrdiff_t last_start;
				if(stack_params.count >= 1) {
					Var last_param = *(Var*)dynarr_from_back(&stack_params, 0);
					last_start = last_param.start;
				} else {
					last_start = 8;
				}

				if(type_size < 8) type_size = 8;

				dynarr_push(
					&stack_params,
					&(Var) {
						.id = arg_ident->ident.id,
						.type = t,
						.mut = false,
						.start = last_start + type_size,
					},
					err
				);
				if(*err) goto RET;
			}
			break;
		}
		
	}

	const AstNode *block = &cg->nodes[fn_def->fn_def.block];

	if(block->type != AST_BLOCK) {
		INVALID_AST_NODE("Expected Function Body", block);
	}

	FPRINTF_OR_ERR(
		cg->output,
		"%s:\n",
		cg->identifiers[ident->ident.id]
	);

	ptrdiff_t var_start = -8;
	for(size_t i = 0; i < block->block.statement_count; i++) {
		const AstNode *statement = &cg->nodes[block->block.statements[i]];

		if(statement->type == AST_VAR_DECL) {
			Type t = type_from_ast(&tc, cg->nodes, statement->var_decl.data_type, err);
			if(*err) goto RET;

			if(t.type == TYPE_ARRAY && t.array.len == 0) {
				if(!statement->var_decl.initial) {
					fprintf(stderr, "Error: Cannot Infer Array Length without Initializer at ");
					lexer_print_debug_to_file(stderr, &statement->debug.debug_info);
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}

				if(cg->nodes[statement->var_decl.initial].type != AST_ARRAY_LIT) {
					fprintf(stderr, "Error: Expected Array Literal at ");
					lexer_print_debug_to_file(stderr, &statement->debug.debug_info);
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}

				t.array.len = cg->nodes[statement->var_decl.initial].array_lit.elem_count;
			}

			size_t type_size = types_get_size(&tc, t);
			size_t alignment = type_size > 8 ? 8 : type_size;
			var_start -= type_size + var_start % alignment;

			dynarr_push(
				&vars,
				&(Var) {
					.id = -1 * statement->var_decl.id, // mark as undeclared initially
					.type = t,  
					.mut = statement->var_decl.mut,
					.start = var_start,
				},
				err
			);
			if(*err) goto RET;
		}
	}

	Scope scope = {
		.vars = vars.data,
		.var_count = vars.count,
		.reg_params = reg_params.data,
		.reg_param_count = reg_params.count,
		.stack_params = stack_params.data,
		.stack_param_count = stack_params.count,
		.tc = tc,
	};

	FPRINTF_OR_ERR(
		cg->output,
		"push rbp\n"
		"mov rbp, rsp\n"
		"sub rsp, %zi\n",
		-1 * var_start
	);

	size_t decl_count = 0;
	for(size_t i = 0; i < block->block.statement_count; i++) {
		const AstNode *statement = &cg->nodes[block->block.statements[i]];

		switch(statement->type) {
		case AST_RET:
			if(statement->ret.return_val) {
				gen_expr(cg, statement->ret.return_val, ret_type, &scope, err);
				if(*err) goto RET;
			}
	
			FPRINTF_OR_ERR(
				cg->output,
				"add rsp, %zi\n"
				"pop rbp\n"
				"ret\n",
				-1 * var_start
			);
			break;

		case AST_VAR_DECL:
			scope.vars[decl_count].id *= -1; //mark as declared
			decl_count += 1;

			if(statement->var_decl.initial) {
				gen_expr(cg, statement->var_decl.initial, scope.vars[decl_count-1].type, &scope, err);
				if(*err) goto RET;

				size_t indx = SIZE_MAX;
				for(size_t i = 0; i < vars.count; i++) {
					if(statement->var_decl.id == scope.vars[i].id) {
						indx = i;
						break;
					}
				}
				if(indx == SIZE_MAX) {
					fprintf(stderr, "[INTERNAL] Unallocated Local Variable at ");
					lexer_print_debug_to_file(stderr, &statement->var_decl.debug_info);
					fprintf(stderr, "\n");
					*err = ERROR_TODO; 
					goto RET;
				}

				Type var_type = scope.vars[indx].type;
				if(var_type.type != TYPE_ARRAY) {
					size_t var_size = types_get_size(&scope.tc, var_type);
					switch(var_size) {
					case 1:
						FPRINTF_OR_ERR(
							cg->output,
							"mov byte [rbp-%zi], al\n",
							-1 * scope.vars[indx].start
						);
						break;
					case 2:
						FPRINTF_OR_ERR(
							cg->output,
							"mov word [rbp-%zi], ax\n",
							-1 * scope.vars[indx].start
						);
						break;
					case 4:
						FPRINTF_OR_ERR(
							cg->output,
							"mov dword [rbp-%zi], eax\n",
							-1 * scope.vars[indx].start
						);
						break;
					case 8:
						FPRINTF_OR_ERR(
							cg->output,
							"mov qword [rbp-%zi], rax\n",
							-1 * scope.vars[indx].start
						);
						break;
					default:
						for(size_t j = 0; j < var_size / 8; j++) {
							FPRINTF_OR_ERR(
								cg->output,
								"pop qword [rbp-%zi]",
								-1 * scope.vars[indx].start + var_size - j*8
							);
						}
						switch(var_size % 8) {
						case 0:
							break;
						case 1:
							FPRINTF_OR_ERR(
								cg->output,
								"mov byte [rbp-%zi], al\n",
								-1 * scope.vars[indx].start
							);
							break;
						case 2:
							FPRINTF_OR_ERR(
								cg->output,
								"mov word [rbp-%zi], ax\n",
								-1 * scope.vars[indx].start
							);
							break;
						case 4:
							FPRINTF_OR_ERR(
								cg->output,
								"mov dword [rbp-%zi], eax\n",
								-1 * scope.vars[indx].start
							);
							break;
						}
						break;
					}
				} else {
					size_t elem_size = types_get_size(&scope.tc, scope.tc.types[var_type.array.base]);

					switch(elem_size) {
					case 1:
						for(size_t j = 0; j < var_type.array.len; j++) {
							FPRINTF_OR_ERR(
								cg->output,
								"pop rax\n"
								"mov byte [rbp-%zi], al\n",
								-1 * scope.vars[indx].start - j
							);
						}
						break;
					case 2:
						for(size_t j = 0; j < var_type.array.len; j++) {
							FPRINTF_OR_ERR(
								cg->output,
								"pop rax\n"
								"mov word [rbp-%zi], ax\n",
								-1 * scope.vars[indx].start - j*2
							);
						}
						break;
					case 4:
						for(size_t j = 0; j < var_type.array.len; j++) {
							FPRINTF_OR_ERR(
								cg->output,
								"pop rax\n"
								"mov dword [rbp-%zi], eax\n",
								-1 * scope.vars[indx].start - j*4
							);
						}
						break;
					case 8:
						for(size_t j = 0; j < var_type.array.len; j++) {
							FPRINTF_OR_ERR(
								cg->output,
								"pop qword [rbp-%zi]\n",
								-1 * scope.vars[indx].start - j*8
							);
						}
						break;
					default:
						for(size_t j = 0; j < var_type.array.len; j++) {
							for(size_t k = 0; k < elem_size / 8; k++) {
								FPRINTF_OR_ERR(
									cg->output,
									"pop qword [rbp-%zi]\n",
									-1 * scope.vars[indx].start - j*elem_size - k*8
								);
							}
						}
						break;
					}

				}

			} else {
				if(!statement->var_decl.mut) {
					fprintf(stderr, "WARNING: Constant Variable '%s' at ", cg->identifiers[statement->var_decl.id]);
					lexer_print_debug_to_file(stderr, &statement->var_decl.debug_info);
					fprintf(stderr, " is not initialized.\n");
				}
			}

			break;

		case AST_FN_CALL:
			save_regs(cg->output, cg->plat, err);
			if(*err) goto RET;

			gen_fn_call(cg, block->block.statements[i], &scope, err);
			if(*err) goto RET;

			restore_regs(cg->output, cg->plat, err);
			if(*err) goto RET;
			break;

		case AST_ASSIGN:
		case AST_ADD_ASSIGN:
		case AST_SUB_ASSIGN:
		case AST_MUL_ASSIGN:
		case AST_DIV_ASSIGN:
			do {} while(0);
			
			const AstNode *var = &cg->nodes[statement->assign.var];

			switch(var->type) {
				case AST_IDENT:
					do {} while(0);
					size_t indx = SIZE_MAX;
					for(size_t i = 0; i < vars.count; i++) {
						Var *candidate = &scope.vars[i];
						if(var->ident.id == candidate->id) {
							if(!candidate->mut) {
								fprintf(
									stderr,
									"Attempting to Assign to Constant Variable '%s' at ",
									cg->identifiers[var->ident.id]
								);
								lexer_print_debug_to_file(stderr, &var->debug.debug_info);
								INVALID_AST_NODE("Attempting to Assign to Constant Variable", statement);
							}
							indx = i;
							break;
						}
					}
					if(indx == SIZE_MAX) {
						fprintf(stderr, "Undefined Variable '%s' at ", cg->identifiers[var->ident.id]);
						lexer_print_debug_to_file(stderr, &var->debug.debug_info);
						fprintf(stderr, "\n");
						*err = ERROR_TODO; 
						goto RET;
					}

					gen_expr(cg, statement->assign.expr, scope.vars[indx].type, &scope, err);
					if(*err) goto RET;

					const char *raxstr;
					switch(types_get_size(&scope.tc, scope.vars[indx].type)) {
					case 1:
						raxstr = "al";
						break;
					case 2:
						raxstr = "ax";
						break;
					case 4:
						raxstr = "eax";
						break;
					case 8:
						raxstr = "rax";
						break;
					}

					switch(statement->type) {
					default: // silence compiler warnings
					case AST_ASSIGN:
						FPRINTF_OR_ERR(
							cg->output,
							"mov [rbp-%zi], %s\n",
							-1 * scope.vars[indx].start, raxstr
						);
						break;
					case AST_ADD_ASSIGN:
						FPRINTF_OR_ERR(
							cg->output,
							"add [rbp-%zi], %s\n",
							-1 * scope.vars[indx].start, raxstr
						);
						break;
					case AST_SUB_ASSIGN:
						FPRINTF_OR_ERR(
							cg->output,
							"sub [rbp-%zi], %s\n",
							-1 * scope.vars[indx].start, raxstr
						);
						break;
					case AST_MUL_ASSIGN:
						FPRINTF_OR_ERR(
							cg->output,
							"push rdx\n"
							"xor edx, edx\n"
							"mov r10, rax\n"
							"mov %s, [rbp-%zi]\n"
							"mul r10\n"
							"mov [rbp-%zi], %s\n"
							"pop rdx\n",
							raxstr, -1 * scope.vars[indx].start,
							-1 * scope.vars[indx].start, raxstr
						);
						break;
					case AST_DIV_ASSIGN:
						FPRINTF_OR_ERR(
							cg->output,
							"push rdx\n"
							"xor edx, edx\n"
							"mov r10, rax\n"
							"mov %s, [rbp-%zi]\n"
							"div r10\n"
							"mov [rbp-%zi], %s\n"
							"pop rdx\n",
							raxstr, -1 * scope.vars[indx].start,
							-1 * scope.vars[indx].start, raxstr
						);
						break;
					}
					break;

			case AST_DEREF:
				do {} while(0);

				const AstNode *base = &cg->nodes[var->deref.ptr];

				if(base->type != AST_IDENT) {
					INVALID_AST_NODE("Expected Identifier as Target of Dereference", base);
				}

				size_t which;
				size_t i;
				Var ptr = find_in_scope(cg, base->ident.id, &scope, &base->debug.debug_info, &which, &i, err);
				if(*err) goto RET;
				
				if(
					ptr.type.type != TYPE_POINTER_VAR
					&& ptr.type.type != TYPE_POINTER_ABYSS
				) {
					if(ptr.type.type == TYPE_POINTER_CONST) {
						INVALID_AST_NODE("Cannot Assign to Dereferenced Constant Pointer", base);
					}
					INVALID_AST_NODE("Dereferenced Identifier must be a Pointer Type", base);
				}

				gen_expr(cg, statement->assign.expr, ptr.type, &scope, err);
				if(*err) goto RET;

				const char *sizestr;
				size_t size = types_get_size(&scope.tc, ptr.type);
				switch(size) {
				case 1:
					sizestr = "byte";
					break;
				case 2:
					sizestr = "word";
					break;
				case 4:
					sizestr = "dword";
					break;
				case 8:
					sizestr = "qword";
					break;
				}

				switch(statement->type) {
				default: // silence compiler warnings
				case AST_ASSIGN:
					FPRINTF_OR_ERR(cg->output, "mov %s [", sizestr);
					print_in_scope(cg, &scope, which, i, err);
					if(*err) goto RET;
					FPUTS_OR_ERR(cg->output, "], ");
					switch(size) {
					case 1:
						FPUTS_OR_ERR(cg->output, "al");
						break;
					case 2:
						FPUTS_OR_ERR(cg->output, "ax");
						break;
					case 4:
						FPUTS_OR_ERR(cg->output, "eax");
						break;
					case 8:
						FPUTS_OR_ERR(cg->output, "rax");
						break;
					}
					FPUTS_OR_ERR(cg->output, "\n");
					break;
				case AST_ADD_ASSIGN:
					FPRINTF_OR_ERR(cg->output, "add %s [", sizestr);
					print_in_scope(cg, &scope, which, i, err);
					if(*err) goto RET;
					FPUTS_OR_ERR(cg->output, "], ");
					switch(size) {
					case 1:
						FPUTS_OR_ERR(cg->output, "al");
						break;
					case 2:
						FPUTS_OR_ERR(cg->output, "ax");
						break;
					case 4:
						FPUTS_OR_ERR(cg->output, "eax");
						break;
					case 8:
						FPUTS_OR_ERR(cg->output, "rax");
						break;
					}
					FPUTS_OR_ERR(cg->output, "\n");
					break;
				case AST_SUB_ASSIGN:
					FPRINTF_OR_ERR(cg->output, "sub %s [", sizestr);
					print_in_scope(cg, &scope, which, i, err);
					if(*err) goto RET;
					FPUTS_OR_ERR(cg->output, "], ");
					switch(size) {
					case 1:
						FPUTS_OR_ERR(cg->output, "al");
						break;
					case 2:
						FPUTS_OR_ERR(cg->output, "ax");
						break;
					case 4:
						FPUTS_OR_ERR(cg->output, "eax");
						break;
					case 8:
						FPUTS_OR_ERR(cg->output, "rax");
						break;
					}
					FPUTS_OR_ERR(cg->output, "\n");
					break;
				case AST_MUL_ASSIGN:
					FPRINTF_OR_ERR(
						cg->output,
						"mov r10, rax\n"
						"mov%s rax, %s [",
						size < 4 ? "zx" : "",
						sizestr
					);
					print_in_scope(cg, &scope, which, i, err);
					if(*err) goto RET;
					FPRINTF_OR_ERR(
						cg->output,
						"]\n"
						"push rdx\n"
						"xor edx, edx\n"
						"mul r10\n"
						"pop rdx\n"
						"mov %s [",
						sizestr
					);
					print_in_scope(cg, &scope, which, i, err);
					FPUTS_OR_ERR(cg->output, "], ");
					switch(size) {
					case 1:
						FPUTS_OR_ERR(cg->output, "al");
						break;
					case 2:
						FPUTS_OR_ERR(cg->output, "ax");
						break;
					case 4:
						FPUTS_OR_ERR(cg->output, "eax");
						break;
					case 8:
						FPUTS_OR_ERR(cg->output, "rax");
						break;
					}
					FPUTS_OR_ERR(cg->output, "\n");
					break;
				case AST_DIV_ASSIGN:
					FPRINTF_OR_ERR(
						cg->output,
						"mov r10, rax\n"
						"mov%s rax, %s [",
						size < 4 ? "zx" : "",
						sizestr
					);
					print_in_scope(cg, &scope, which, i, err);
					if(*err) goto RET;
					FPRINTF_OR_ERR(
						cg->output,
						"]\n"
						"push rdx\n"
						"xor edx, edx\n"
						"div r10\n"
						"pop rdx\n"
						"mov %s [",
						sizestr
					);
					print_in_scope(cg, &scope, which, i, err);
					FPUTS_OR_ERR(cg->output, "], ");
					switch(size) {
					case 1:
						FPUTS_OR_ERR(cg->output, "al");
						break;
					case 2:
						FPUTS_OR_ERR(cg->output, "ax");
						break;
					case 4:
						FPUTS_OR_ERR(cg->output, "eax");
						break;
					case 8:
						FPUTS_OR_ERR(cg->output, "rax");
						break;
					}
					FPUTS_OR_ERR(cg->output, "\n");
					break;
				}
				break;
			case AST_SUBSCRIPT:
				do {} while(0);

				base = &cg->nodes[var->deref.ptr];

				if(base->type != AST_IDENT) {
					INVALID_AST_NODE("Expected Identifier as Target of Subscript Operation", base);
				}

				Var slice = find_in_scope(cg, base->ident.id, &scope, &base->debug.debug_info, &which, &i, err);
				if(*err) goto RET;
				
				if(
					slice.type.type != TYPE_SLICE_VAR
					&& slice.type.type != TYPE_SLICE_ABYSS
					&& slice.type.type != TYPE_ARRAY
				) {
					if(ptr.type.type == TYPE_SLICE_CONST) {
						INVALID_AST_NODE("Cannot Assign to Subscripted Constant Slice", base);
					}
					INVALID_AST_NODE("Subscripted Identifier must be a Slice or Array Type", base);
				}

				if(slice.type.type == TYPE_ARRAY) {
					if(!slice.mut) {
						INVALID_AST_NODE("Cannot Assign to Element of Constant Array", base);
					}
				}

				gen_expr(cg, statement->assign.expr, slice.type, &scope, err);
				if(*err) goto RET;

				size = types_get_size(&scope.tc, slice.type);
				switch(size) {
				case 1:
					sizestr = "byte";
					break;
				case 2:
					sizestr = "word";
					break;
				case 4:
					sizestr = "dword";
					break;
				case 8:
					sizestr = "qword";
					break;
				}

				switch(statement->type) {
				default: // silence compiler warnings
				case AST_ASSIGN:
					if(size <= 8) {
						FPRINTF_OR_ERR(cg->output, "mov %s [", sizestr);
						print_in_scope(cg, &scope, which, i, err);
						if(*err) goto RET;
						FPUTS_OR_ERR(cg->output, "], ");
						switch(size) {
						case 1:
							FPUTS_OR_ERR(cg->output, "al");
							break;
						case 2:
							FPUTS_OR_ERR(cg->output, "ax");
							break;
						case 4:
							FPUTS_OR_ERR(cg->output, "eax");
							break;
						case 8:
							FPUTS_OR_ERR(cg->output, "rax");
							break;
						}
						FPUTS_OR_ERR(cg->output, "\n");
					} else {
						for(size_t j = 0; j < size / 8; j++) {
							FPUTS_OR_ERR(
								cg->output,
								"pop rax\n"
								"mov qword ["
							);
							print_in_scope(cg, &scope, which, i, err);
							if(*err) goto RET;
							FPRINTF_OR_ERR(
								cg->output,
								"+%zi], rax",
								j*8
							);
						}
					}
					break;
				case AST_ADD_ASSIGN:
					if(!type_is_arithmetic(scope.tc.types[slice.type.slice.base])) {
						fprintf(stderr, "Cannot Perform Addition on Non-Arithmetic Type ");
						type_print(stderr, &scope.tc, scope.tc.types[slice.type.slice.base]);
						fprintf(stderr, " at ");
						lexer_print_debug_to_file(stderr, &statement->debug.debug_info);
						fprintf(stderr, "\n");
						*err = ERROR_UNEXPECTED_DATA;
						goto RET;
					}
					FPRINTF_OR_ERR(cg->output, "add %s [", sizestr);
					print_in_scope(cg, &scope, which, i, err);
					if(*err) goto RET;
					FPUTS_OR_ERR(cg->output, "], ");
					switch(size) {
					case 1:
						FPUTS_OR_ERR(cg->output, "al");
						break;
					case 2:
						FPUTS_OR_ERR(cg->output, "ax");
						break;
					case 4:
						FPUTS_OR_ERR(cg->output, "eax");
						break;
					case 8:
						FPUTS_OR_ERR(cg->output, "rax");
						break;
					}
					FPUTS_OR_ERR(cg->output, "\n");
					break;
				case AST_SUB_ASSIGN:
					if(!type_is_arithmetic(scope.tc.types[slice.type.slice.base])) {
						fprintf(stderr, "Cannot Perform Subtraction on Non-Arithmetic Type ");
						type_print(stderr, &scope.tc, scope.tc.types[slice.type.slice.base]);
						fprintf(stderr, " at ");
						lexer_print_debug_to_file(stderr, &statement->debug.debug_info);
						fprintf(stderr, "\n");
						*err = ERROR_UNEXPECTED_DATA;
						goto RET;
					}
					FPRINTF_OR_ERR(cg->output, "sub %s [", sizestr);
					print_in_scope(cg, &scope, which, i, err);
					if(*err) goto RET;
					FPUTS_OR_ERR(cg->output, "], ");
					switch(size) {
					case 1:
						FPUTS_OR_ERR(cg->output, "al");
						break;
					case 2:
						FPUTS_OR_ERR(cg->output, "ax");
						break;
					case 4:
						FPUTS_OR_ERR(cg->output, "eax");
						break;
					case 8:
						FPUTS_OR_ERR(cg->output, "rax");
						break;
					}
					FPUTS_OR_ERR(cg->output, "\n");
					break;
				case AST_MUL_ASSIGN:
					if(!type_is_arithmetic(scope.tc.types[slice.type.slice.base])) {
						fprintf(stderr, "Cannot Perform Multiplication on Non-Arithmetic Type ");
						type_print(stderr, &scope.tc, scope.tc.types[slice.type.slice.base]);
						fprintf(stderr, " at ");
						lexer_print_debug_to_file(stderr, &statement->debug.debug_info);
						fprintf(stderr, "\n");
						*err = ERROR_UNEXPECTED_DATA;
						goto RET;
					}
					FPRINTF_OR_ERR(
						cg->output,
						"mov r10, rax\n"
						"mov%s rax, %s [",
						size < 4 ? "zx" : "",
						sizestr
					);
					print_in_scope(cg, &scope, which, i, err);
					if(*err) goto RET;
					FPRINTF_OR_ERR(
						cg->output,
						"]\n"
						"push rdx\n"
						"xor edx, edx\n"
						"mul r10\n"
						"pop rdx\n"
						"mov %s [",
						sizestr
					);
					print_in_scope(cg, &scope, which, i, err);
					FPUTS_OR_ERR(cg->output, "], ");
					switch(size) {
					case 1:
						FPUTS_OR_ERR(cg->output, "al");
						break;
					case 2:
						FPUTS_OR_ERR(cg->output, "ax");
						break;
					case 4:
						FPUTS_OR_ERR(cg->output, "eax");
						break;
					case 8:
						FPUTS_OR_ERR(cg->output, "rax");
						break;
					}
					FPUTS_OR_ERR(cg->output, "]\n");
					break;
				case AST_DIV_ASSIGN:
					if(!type_is_arithmetic(scope.tc.types[slice.type.slice.base])) {
						fprintf(stderr, "Cannot Perform Division on Non-Arithmetic Type ");
						type_print(stderr, &scope.tc, scope.tc.types[slice.type.slice.base]);
						fprintf(stderr, " at ");
						lexer_print_debug_to_file(stderr, &statement->debug.debug_info);
						fprintf(stderr, "\n");
						*err = ERROR_UNEXPECTED_DATA;
						goto RET;
					}
					FPRINTF_OR_ERR(
						cg->output,
						"mov r10, rax\n"
						"mov%s rax, %s [",
						size < 4 ? "zx" : "",
						sizestr
					);
					print_in_scope(cg, &scope, which, i, err);
					if(*err) goto RET;
					FPRINTF_OR_ERR(
						cg->output,
						"]\n"
						"push rdx\n"
						"xor edx, edx\n"
						"div r10\n"
						"pop rdx\n"
						"mov %s [",
						sizestr
					);
					print_in_scope(cg, &scope, which, i, err);
					FPUTS_OR_ERR(cg->output, "], ");
					switch(size) {
					case 1:
						FPUTS_OR_ERR(cg->output, "al");
						break;
					case 2:
						FPUTS_OR_ERR(cg->output, "ax");
						break;
					case 4:
						FPUTS_OR_ERR(cg->output, "eax");
						break;
					case 8:
						FPUTS_OR_ERR(cg->output, "rax");
						break;
					}
					FPUTS_OR_ERR(cg->output, "]\n");
					break;
				}
				break;

			default:
				INVALID_AST_NODE("Expected Lvalue", var);
				goto RET;
			}
			break;
				
		default:
			INVALID_AST_NODE("Expected Statement", statement);
		}
	}

RET:
	types_clean(&tc);
	dynarr_clean(&vars);
	dynarr_clean(&reg_params);
	dynarr_clean(&stack_params);
	return;	
}

void codegen_asm(CodeGen *cg, bool exec, Error *err)
{
	DynArr fn_sigs;
	dynarr_init(&fn_sigs, sizeof (FnSig));
	const AstNode *module = &cg->nodes[0];

	FPUTS_OR_ERR(
		cg->output,
		"bits 64\n"
		"section .text\n"
	);

	switch(cg->plat) {
	case PLATFORM_LINUX:
		if(exec) {
			FPUTS_OR_ERR(
				cg->output,
				"global _start\n"
				"_start:\n"
				"call main\n"
				"mov rdi, rax\n"
				"mov rax, 60\n"
				"syscall\n"
			);
		}
		break;
	case PLATFORM_WINDOWS:
		if(exec) {
			FPUTS_OR_ERR(
				cg->output,
				"extern ExitProcess\n"
				"global _start\n"
				"_start:\n"
				"and rsp, 0xFFFF_FFFF_FFFF_FFF0\n"
				"call main\n"
				"mov rcx, rax\n"
				"sub rsp, 32\n"
				"call ExitProcess\n"
			);
		}
		break;
	}

	for(size_t i = 0; i < module->module.statement_count; i++) {
		size_t index = module->module.statements[i];
		if(cg->nodes[index].type == AST_FN_DEF) {
			const AstNode *type = &cg->nodes[cg->nodes[index].fn_def.fn_type];

			size_t *args = malloc(type->fn_type.arg_count * sizeof *args);
			CHECK_MALLOC(args);

			for(size_t i = 0; i < type->fn_type.arg_count; i++) {
				args[i] = type->fn_type.args[2*i+1];
			}

			dynarr_push(
				&fn_sigs,
				&(FnSig) {
					.id = cg->nodes[cg->nodes[index].fn_def.ident].ident.id,
					.ret = type->fn_type.ret_type,
					.arg_count = type->fn_type.arg_count,
					.args = args,
				},
				err
			);
			if(*err) goto RET;
		}
	}

	cg->fn_sigs = fn_sigs.data;
	cg->fn_sig_count = fn_sigs.count;

	for(size_t i = 0; i < module->module.statement_count; i++) {
		size_t index = module->module.statements[i];
		switch(cg->nodes[index].type) {
		case AST_FN_DEF:
			gen_fn_def(cg, index, err);
			if(*err) goto RET;
			break;
		default:
			INVALID_AST_NODE(
				"Illegal File Scope Statement", &cg->nodes[index]
			);
		}
	}
	
RET:
	return;
}
