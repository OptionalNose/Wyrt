#include "codegen.h"

#include <string.h>
#include <stddef.h>

#include "types.h"

#define INVALID_AST_NODE(err_msg, node_ptr) do {\
	fputs("Error: " err_msg " at ", stderr); \
	lexer_print_debug_to_file(stderr, &(node_ptr)->debug.debug_info); \
	fputc('\n', stderr); \
	*err = ERROR_UNEXPECTED_DATA; \
	goto RET; } while(0);

#define FPRINTF_OR_ERR(file, fmt, ...) \
	do { \
		if(fprintf(file, fmt, __VA_ARGS__) < 0) { \
			fprintf(stderr, "Error: Unable to Write to IR File\n"); \
			*err = ERROR_IO; \
			goto RET; \
		} \
	} while (0)

#define FPUTS_OR_ERR(file, str) \
	do { \
		if(fputs(str, file) <= 0) { \
			fprintf(stderr, "Error: Unable to Write to IR File\n"); \
			*err = ERROR_IO; \
			goto RET; \
		} \
	} while (0)

typedef struct {
	ptrdiff_t id;
	Type type; 
	bool mut;
} Var;

typedef struct {
	Var *vars; // - == not declared yet
	size_t var_count;
	Var *reg_params;
	size_t reg_param_count;
	Var *stack_params;
	size_t stack_param_count;
	TypeContext tc;
} Scope;

void codegen_init(
		CodeGen *cg,
		FILE *output,
		const AstNode *nodes,
		size_t node_count,
		char *const *identifiers
)
{
	*cg = (CodeGen) {
		.output = output,
		.nodes = nodes,
		.node_count = node_count,
		.identifiers = identifiers,
		.fn_sig_count = 0,
		.fn_sigs = NULL,
	};
}

static void save_regs(FILE *file, Error *err)
{
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
RET:
	return;
}

static void restore_regs(FILE *file, Error *err)
{
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
	switch(which) {
	case 0:
		switch(index) {
		case 0:
			FPUTS_OR_ERR(cg->output, "rdi");
			goto RET;
		case 1:
			FPUTS_OR_ERR(cg->output, "rsi");
			goto RET;
		case 2:
			FPUTS_OR_ERR(cg->output, "rdx");
			goto RET;
		case 3:
			FPUTS_OR_ERR(cg->output, "rcx");
			goto RET;
		case 4:
			FPUTS_OR_ERR(cg->output, "r8");
			goto RET;
		case 5:
			FPUTS_OR_ERR(cg->output, "r9");
			goto RET;
		}
		break;
	case 1:
		FPRINTF_OR_ERR(cg->output, "[rbp+%ji]", 16 + index * 8);
		break;
	case 2:
		FPRINTF_OR_ERR(cg->output, "[rbp-%ji]", 8*(index+1));
		break;
	}

RET:
	return;
}

static void gen_expr(CodeGen *cg, size_t index, Type expected, Scope *scope, Error *err);

//SysV-ABI
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
		fprintf(stderr, " expected %ji, found %ji\n", sig.arg_count, arg_count);
		*err = ERROR_UNDEFINED;
		goto RET;
	}

	for(size_t i = 0; i < (arg_count > 6 ? 6 : arg_count); i++) {
		Type type = type_from_ast(&scope->tc, cg->nodes, sig.args[i], err);
		if(*err) goto RET;
		gen_expr(cg, call->fn_call.args[i], type, scope, err);
		if(*err) goto RET;

		switch(i) {
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
	}

	for(size_t i = arg_count - 1; i >= 6; i--) {
		Type type = type_from_ast(&scope->tc, cg->nodes, sig.args[i], err);
		if(*err) goto RET;
		gen_expr(cg, call->fn_call.args[i], type, scope, err);
		if(*err) goto RET;

		FPUTS_OR_ERR(cg->output, "push rax\n");
	}


	if(arg_count > 6) {
		FPRINTF_OR_ERR(
			cg->output,
			"call %s\n"
			"add rsp, %ji\n",
			cg->identifiers[call->fn_call.fn_id],
			8 * (arg_count - 6)
		);
	} else {
		FPRINTF_OR_ERR(
			cg->output,
			"call %s\n",
			cg->identifiers[call->fn_call.fn_id]
		);
	}


RET:
	return;
}

//SysV-ABI
static void gen_expr(CodeGen *cg, size_t index, Type expected, Scope *scope, Error *err)
{
	const AstNode *expr = &cg->nodes[index];

	switch(expr->type) {
	case AST_ADD:
		gen_expr(cg, expr->binop.lhs, expected, scope, err);
		if(*err) goto RET;

		switch(cg->nodes[expr->binop.rhs].type) {
		case AST_INT_LIT:
			FPRINTF_OR_ERR(
				cg->output,
				"add rax, %zi\n",
				cg->nodes[expr->binop.rhs].int_lit.val
			);
			break;

		case AST_IDENT:
			FPUTS_OR_ERR(cg->output, "add rax, ");
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

			FPUTS_OR_ERR(
				cg->output,
				"mov r10, rax\n"
				"pop rax\n"
				"add rax, r10\n"
			);
			break;
		}
		break;
	
	case AST_SUB:
		gen_expr(cg, expr->binop.lhs, expected, scope, err);
		if(*err) goto RET;

		switch(cg->nodes[expr->binop.rhs].type) {
		case AST_INT_LIT:
			FPRINTF_OR_ERR(
				cg->output,
				"sub rax, %zi\n",
				cg->nodes[expr->binop.rhs].int_lit.val
			);
			break;

		case AST_IDENT:
			FPUTS_OR_ERR(cg->output, "sub rax, ");
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

			FPUTS_OR_ERR(
				cg->output,
				"mov r10, rax\n"
				"pop rax\n"
				"sub rax, r10\n"
			);
			break;
		}
		break;
	
	case AST_MUL:
		gen_expr(cg, expr->binop.lhs, expected, scope, err);
		if(*err) goto RET;

		switch(cg->nodes[expr->binop.rhs].type) {
		case AST_INT_LIT:
			FPRINTF_OR_ERR(
				cg->output,
				"mov r10, %ji\n",
				cg->nodes[expr->binop.rhs].int_lit.val
			);
			break;

		case AST_IDENT:
			FPUTS_OR_ERR(cg->output, "mov r10, ");
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

			FPUTS_OR_ERR(
				cg->output,
				"mov r10, rax\n"
				"pop rax\n"
			);
			break;
		}

		FPUTS_OR_ERR(
			cg->output,
			"push rdx\n"
			"xor edx, edx\n"
			"mul r10\n"
			"pop rdx\n"
		);
		break;

	case AST_DIV:
		gen_expr(cg, expr->binop.lhs, expected, scope, err);
		if(*err) goto RET;

		switch(cg->nodes[expr->binop.rhs].type) {
		case AST_INT_LIT:
			FPRINTF_OR_ERR(
				cg->output,
				"mov r10, %ji\n",
				cg->nodes[expr->binop.rhs].int_lit.val
			);
			break;

		case AST_IDENT:
			FPUTS_OR_ERR(cg->output, "mov r10, ");
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

			FPUTS_OR_ERR(
				cg->output,
				"mov r10, rax\n"
				"pop rax\n"
			);
			break;
		}

		FPUTS_OR_ERR(
			cg->output,
			"push rdx\n"
			"xor edx, edx\n"
			"div r10\n"
			"pop rdx\n"
		);
		break;
	
	case AST_INT_LIT:
		FPRINTF_OR_ERR(
			cg->output,
			"mov rax, %ji\n",
			expr->int_lit.val
		);
		break;
	
	case AST_IDENT:
		FPUTS_OR_ERR(cg->output, "mov rax, ");
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

		print_in_scope(cg, scope, ident_which, ident_index, err);
		if(*err) goto RET;
		FPUTS_OR_ERR(cg->output, "\n");
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
		switch(types_get_size(addr.type)) {
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

	case AST_FN_CALL:
		save_regs(cg->output, err);
		if(*err) goto RET;

		gen_fn_call(cg, index, scope, err);
		if(*err) goto RET;

		restore_regs(cg->output, err);
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
		
		FPRINTF_OR_ERR(
			cg->output,
			"lea rax, [rbp-%zi]\n",
			8 * (addr_targ + 1)
		);
		break;

	default:
		INVALID_AST_NODE("Expected Binary Operation", expr);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

RET:
	return;
}

//SysV-ABI
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

		if(reg_params.count <= 5) {
			dynarr_push(
				&reg_params,
				&(Var) {
					.id = arg_ident->ident.id,
					.type = t,
					.mut = false,
				},
				err
			);
			if(*err) goto RET;
		} else {
			dynarr_push(
				&stack_params,
				&(Var) {
					.id = arg_ident->ident.id,
					.type = t,
					.mut = false,
				},
				err
			);
			if(*err) goto RET;
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

	for(size_t i = 0; i < block->block.statement_count; i++) {
		const AstNode *statement = &cg->nodes[block->block.statements[i]];

		if(statement->type == AST_VAR_DECL) {
			Type t = type_from_ast(&tc, cg->nodes, statement->var_decl.data_type, err);
			if(*err) goto RET;

			dynarr_push(
				&vars,
				&(Var) {
					.id = -1 * statement->var_decl.id, // mark as undeclared initially
					.type = t,  
					.mut = statement->var_decl.mut,
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
		8 * (vars.count + 1)
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
				8 * (vars.count + 1)
			);
			break;

		case AST_VAR_DECL:
			scope.vars[decl_count].id *= -1; //mark as declared
			decl_count += 1;

			if(statement->var_decl.initial) {
				gen_expr(cg, statement->var_decl.initial, scope.vars[decl_count].type, &scope, err);
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

				FPRINTF_OR_ERR(
					cg->output,
					"mov qword [rbp-%zi], rax\n",
					8*(indx+1)
				);
			} else {
				if(!statement->var_decl.mut) {
					fprintf(stderr, "WARNING: Constant Variable '%s' at ", cg->identifiers[statement->var_decl.id]);
					lexer_print_debug_to_file(stderr, &statement->var_decl.debug_info);
					fprintf(stderr, " is not initialized.\n");
				}
			}

			break;

		case AST_FN_CALL:
			save_regs(cg->output, err);
			if(*err) goto RET;

			gen_fn_call(cg, block->block.statements[i], &scope, err);
			if(*err) goto RET;

			restore_regs(cg->output, err);
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
								fprintf(stderr, "Attempting to Assign to Constant Variable '%s' at ", cg->identifiers[var->ident.id]);
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

					switch(statement->type) {
					default: // silence compiler warnings
					case AST_ASSIGN:
						FPRINTF_OR_ERR(
							cg->output,
							"mov [rbp-%zi], rax\n",
							8 * (indx + 1)
						);
						break;
					case AST_ADD_ASSIGN:
						FPRINTF_OR_ERR(
							cg->output,
							"add [rbp-%zi], rax\n",
							8 * (indx + 1)
						);
						break;
					case AST_SUB_ASSIGN:
						FPRINTF_OR_ERR(
							cg->output,
							"sub [rbp-%zi], rax\n",
							8 * (indx + 1)
						);
						break;
					case AST_MUL_ASSIGN:
						FPRINTF_OR_ERR(
							cg->output,
							"push rdx\n"
							"xor edx, edx\n"
							"mov r10, rax\n"
							"mov rax, qword [rbp-%zi]\n"
							"mul r10\n"
							"mov [rbp-%zi], rax\n"
							"pop rdx\n",
							8 * (indx + 1),
							8 * (indx + 1)
						);
						break;
					case AST_DIV_ASSIGN:
						FPRINTF_OR_ERR(
							cg->output,
							"push rdx\n"
							"xor edx, edx\n"
							"mov r10, rax\n"
							"mov rax, qword [rbp-%zi]\n"
							"div r10\n"
							"mov [rbp-%zi], rax\n"
							"pop rdx\n",
							8 * (indx + 1),
							8 * (indx + 1)
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
				size_t size = types_get_size(ptr.type);
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
						"push rdx\n"
						"xor edx, edx\n"
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
						"mul r10\n"
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
					FPUTS_OR_ERR(
						cg->output,
						"\n"
						"pop rdx\n"
					);
					break;
				case AST_DIV_ASSIGN:
					FPRINTF_OR_ERR(
						cg->output,
						"push rdx\n"
						"xor edx, edx\n"
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
						"mul r10\n"
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
					FPUTS_OR_ERR(
						cg->output,
						"]\n"
						"pop rdx\n"
					);
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

void codegen_gen(CodeGen *cg, bool exec, Error *err)
{
	DynArr fn_sigs;
	dynarr_init(&fn_sigs, sizeof (FnSig));
	const AstNode *module = &cg->nodes[0];

	FPUTS_OR_ERR(
		cg->output,
		"bits 64\n"
		"section .text\n"
	);

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


void codegen_clean(const CodeGen *cg)
{
	for(size_t i = 0; i < cg->fn_sig_count; i++) {
		free(cg->fn_sigs[i].args);
	}
	if(cg->fn_sigs) free(cg->fn_sigs);
}
