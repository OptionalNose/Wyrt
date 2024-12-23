#include "codegen.h"

#include <string.h>
#include <stddef.h>


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
	ptrdiff_t *vars; // - == not declared yet
	size_t var_count;
	size_t *reg_params;
	size_t reg_param_count;
	size_t *stack_params;
	size_t stack_param_count;
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

static Type identifier_to_type(size_t ident_id)
{
	switch(ident_id) {
	case 0:
		return (Type) {.type = TYPE_PRIMITIVE_U8};
	case 1:
		return (Type) {.type = TYPE_PRIMITIVE_VOID};
	default:
		return (Type) {.type = TYPE_INVALID};
	} 
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

static void find_in_scope(CodeGen *cg, size_t id, const Scope *scope, const DebugInfo *debug, Error *err)
{
	for(size_t i = 0; i < scope->reg_param_count; i++) {
		if(id == scope->reg_params[i]) {
			switch(i) {
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
		}
	}

	for(size_t i = 0; i < scope->stack_param_count; i++) {
		if(id == scope->stack_params[i]) {
			FPRINTF_OR_ERR(cg->output, "[rbp+%ji]", 16 + i * 8);
			goto RET;
		}
	}

	for(size_t i = 0; i < scope->var_count; i++) {
		if(id == scope->vars[i]) {
			FPRINTF_OR_ERR(cg->output, "[rbp-%ji]", 8*(i+1));
			goto RET;
		}
	}

	fprintf(stderr, "Undeclared Variable '%s' at ", cg->identifiers[id]);
	lexer_print_debug_to_file(stderr, debug);
	fprintf(stderr, "\n");
	*err = ERROR_UNDEFINED;

RET:
	return;
}

static void gen_expr(CodeGen *cg, size_t index, Type expected, const Scope *scope, Error *err);

//SysV-ABI
static void gen_fn_call(CodeGen *cg, size_t index, const Scope *scope, Error *err)
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
		gen_expr(cg, call->fn_call.args[i], sig.args[i], scope, err);
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
		gen_expr(cg, call->fn_call.args[i], sig.args[i], scope, err);
		if(*err) goto RET;

		FPUTS_OR_ERR(cg->output, "push rax\n");
	}


	FPRINTF_OR_ERR(
		cg->output,
		"call %s\n"
		"add rsp, %ji\n",
		cg->identifiers[call->fn_call.fn_id],
		8 * (arg_count - 6)
	);


RET:
	return;
}

//SysV-ABI
static void gen_expr(CodeGen *cg, size_t index, Type expected, const Scope *scope, Error *err)
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
				"add rax, %ji\n",
				cg->nodes[expr->binop.rhs].int_lit.val
			);
			break;

		case AST_IDENT:
			FPUTS_OR_ERR(cg->output, "add rax, ");
			find_in_scope(cg, cg->nodes[expr->binop.rhs].ident.id, scope, &cg->nodes[expr->binop.rhs].debug.debug_info, err);
			if(*err) goto RET;
			FPUTS_OR_ERR(cg->output, "\n");
			break;

		default:
			FPUTS_OR_ERR(
				cg->output,
				"push rax\n"
			);

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
				"sub rax, %ji\n",
				cg->nodes[expr->binop.rhs].int_lit.val
			);
			break;

		case AST_IDENT:
			FPUTS_OR_ERR(cg->output, "sub rax, ");
			find_in_scope(cg, cg->nodes[expr->binop.rhs].ident.id, scope, &cg->nodes[expr->binop.rhs].debug.debug_info, err);
			if(*err) goto RET;
			FPUTS_OR_ERR(cg->output, "\n");
			break;

		default:
			FPUTS_OR_ERR(
				cg->output,
				"push rax\n"
			);

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
			find_in_scope(cg, cg->nodes[expr->binop.rhs].ident.id, scope, &cg->nodes[expr->binop.rhs].debug.debug_info, err);
			if(*err) goto RET;
			FPUTS_OR_ERR(cg->output, "\n");
			break;

		default:
			FPUTS_OR_ERR(
				cg->output,
				"push rax\n"
			);

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
			find_in_scope(cg, cg->nodes[expr->binop.rhs].ident.id, scope, &cg->nodes[expr->binop.rhs].debug.debug_info, err);
			if(*err) goto RET;
			FPUTS_OR_ERR(cg->output, "\n");
			break;

		default:
			FPUTS_OR_ERR(
				cg->output,
				"push rax\n"
			);

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
		find_in_scope(cg, expr->ident.id, scope, &expr->debug.debug_info, err);
		if(*err) goto RET;
		FPUTS_OR_ERR(cg->output, "\n");
		break;
	
	case AST_FN_CALL:
		save_regs(cg->output, err);
		if(*err) goto RET;

		gen_fn_call(cg, index, scope, err);
		if(*err) goto RET;

		restore_regs(cg->output, err);
		if(*err) goto RET;

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
	dynarr_init(&vars, sizeof (size_t));

	DynArr reg_params = { 0 };
	dynarr_init(&reg_params, sizeof (size_t));

	DynArr stack_params = { 0 };
	dynarr_init(&stack_params, sizeof (size_t));

	const AstNode *fn_def = &cg->nodes[index];
	const AstNode *ident = &cg->nodes[fn_def->fn_def.ident];
	const AstNode *fn_type = &cg->nodes[fn_def->fn_def.fn_type];

	if(fn_type->type != AST_FN_TYPE) {
		INVALID_AST_NODE("Expected Function Type", fn_type);
	}

	const AstNode *ret_type = &cg->nodes[fn_type->fn_type.ret_type];

	if(ret_type->type != AST_IDENT) {
		INVALID_AST_NODE("Expected Type", ret_type);
	}

	const Type return_type = identifier_to_type(ret_type->ident.id);

	if(return_type.type == TYPE_INVALID) {
		INVALID_AST_NODE("Invalid Return Type", ret_type);
	}

	for(size_t i = 0; i < fn_type->fn_type.arg_count; i++) {
		const AstNode *arg_ident = &cg->nodes[fn_type->fn_type.args[2*i]];
		const AstNode *arg_type = &cg->nodes[fn_type->fn_type.args[2*i + 1]];

		if(arg_type->type != AST_IDENT) {
			INVALID_AST_NODE(
				"Illegal Type for Function Parameter", arg_type
			);
		}

		if(reg_params.count <= 5) {
			dynarr_push(&reg_params, &arg_ident->ident.id, err);
			if(*err) goto RET;
		} else {
			dynarr_push(&stack_params, &arg_ident->ident.id, err);
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
			dynarr_push(&vars, &(size_t) { -1 * statement->var_decl.id }, err); // mark as undeclared initially
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
	};

	FPRINTF_OR_ERR(
		cg->output,
		"push rbp\n"
		"mov rbp, rsp\n"
		"sub rsp, %ji\n",
		8 * (vars.count + 1)
	);

	size_t decl_count = 0;
	for(size_t i = 0; i < block->block.statement_count; i++) {
		const AstNode *statement = &cg->nodes[block->block.statements[i]];

		switch(statement->type) {
		case AST_RET:
			gen_expr(cg, statement->ret.return_val, return_type, &scope, err);
			if(*err) goto RET;
	
			FPRINTF_OR_ERR(
				cg->output,
				"add rsp, %ji\n"
				"pop rbp\n"
				"ret\n",
				8 * (vars.count + 1)
			);
			break;

		case AST_VAR_DECL:
			scope.vars[decl_count] *= -1; //mark as declared
			decl_count += 1;

			if(statement->var_decl.initial) {
				Type var_type = identifier_to_type(cg->nodes[statement->var_decl.data_type].ident.id);	
				if(var_type.type == TYPE_INVALID) {
					INVALID_AST_NODE("Invalid Variable Type", &cg->nodes[statement->var_decl.data_type]);
				}

				gen_expr(cg, statement->var_decl.initial, var_type, &scope, err);
				if(*err) goto RET;

				size_t indx = SIZE_MAX;
				for(size_t i = 0; i < vars.count; i++) {
					if(statement->var_decl.id == *(size_t*)dynarr_at(&vars, i)) {
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
					"mov qword [rbp-%ji], rax\n",
					8*(indx+1)
				);
			}

			break;

		case AST_FN_CALL:
			save_regs(cg->output, err);
			if(*err) goto RET;

			gen_fn_call(cg, i, &scope, err);
			if(*err) goto RET;

			restore_regs(cg->output, err);
			if(*err) goto RET;

			break;
		default:
			INVALID_AST_NODE("Expected Statement", statement);
		}
	}

RET:
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

			Type *args = malloc(type->fn_type.arg_count * sizeof *args);
			CHECK_MALLOC(args);

			for(size_t i = 0; i < type->fn_type.arg_count; i++) {
				args[i] = identifier_to_type(cg->nodes[type->fn_type.args[2*i+1]].ident.id);
			}

			dynarr_push(
				&fn_sigs,
				&(FnSig) {
					.id = cg->nodes[cg->nodes[index].fn_def.ident].ident.id,
					.ret = identifier_to_type(type->fn_type.ret_type),
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
