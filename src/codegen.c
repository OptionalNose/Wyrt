#include "codegen.h"

#include <string.h>
#include <inttypes.h>

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

typedef union {
	enum {
		TYPE_INVALID,
		TYPE_PRIMITIVE_U8,
		TYPE_PRIMITIVE_VOID,
	} type;
} Type;


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
	};
}

Type identifier_to_type(size_t ident_id)
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

//clobber: rax, rdi
//requires: rdx == 0
void gen_expr(CodeGen *cg, size_t index, Type expected, DynArr const* vars, Error *err)
{
	const AstNode *expr = &cg->nodes[index];

	switch(expr->type) {
	case AST_ADD:
		gen_expr(cg, expr->binop.lhs, expected, vars, err);
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
			do {} while(0);

			size_t indx = SIZE_MAX;
			for(size_t i = 0; i < vars->count; i++) {
				if(cg->nodes[expr->binop.rhs].ident.id == *(size_t*)dynarr_at(vars, i)) {
					indx = i;
					break;
				}
			}
			
			if(indx == SIZE_MAX) {
				INVALID_AST_NODE("Undeclared variable ", &cg->nodes[expr->binop.rhs]);
			}

			FPRINTF_OR_ERR(
				cg->output,
				"add rax, [rbp-%ji]\n",
				8*(indx+1)
			);
			break;

		default:
			FPUTS_OR_ERR(
				cg->output,
				"push rax\n"
			);

			gen_expr(cg, expr->binop.rhs, expected, vars, err);
			if(*err) goto RET;

			FPUTS_OR_ERR(
				cg->output,
				"mov rdi, rax\n"
				"pop rax\n"
				"add rax, rdi\n"
			);
			break;
		}

		break;
	
	case AST_SUB:
		gen_expr(cg, expr->binop.lhs, expected, vars, err);
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
			do {} while(0);

			size_t indx = SIZE_MAX;
			for(size_t i = 0; i < vars->count; i++) {
				if(cg->nodes[expr->binop.rhs].ident.id == *(size_t*)dynarr_at(vars, i)) {
					indx = i;
					break;
				}
			}
			
			if(indx == SIZE_MAX) {
				INVALID_AST_NODE("Undeclared variable ", &cg->nodes[expr->binop.rhs]);
			}

			FPRINTF_OR_ERR(
				cg->output,
				"sub rax, [rbp-%ji]\n",
				8*(indx+1)
			);
			break;

		default:
			FPUTS_OR_ERR(
				cg->output,
				"push rax\n"
			);

			gen_expr(cg, expr->binop.rhs, expected, vars, err);
			if(*err) goto RET;

			FPUTS_OR_ERR(
				cg->output,
				"mov rdi, rax\n"
				"pop rax\n"
				"sub rax, rdi\n"
			);
			break;
		}

		break;
	
	case AST_MUL:
		gen_expr(cg, expr->binop.lhs, expected, vars, err);
		if(*err) goto RET;

		switch(cg->nodes[expr->binop.rhs].type) {
		case AST_INT_LIT:
			FPRINTF_OR_ERR(
				cg->output,
				"mov rdi, %ji\n",
				cg->nodes[expr->binop.rhs].int_lit.val
			);
			break;

		case AST_IDENT:
			do {} while(0);

			size_t indx = SIZE_MAX;
			for(size_t i = 0; i < vars->count; i++) {
				if(cg->nodes[expr->binop.rhs].ident.id == *(size_t*)dynarr_at(vars, i)) {
					indx = i;
					break;
				}
			}
			
			if(indx == SIZE_MAX) {
				INVALID_AST_NODE("Undeclared variable ", &cg->nodes[expr->binop.rhs]);
			}

			FPRINTF_OR_ERR(
				cg->output,
				"mov rdi, [rbp-%ji]\n",
				8*(indx+1)
			);
			break;

		default:
			FPUTS_OR_ERR(
				cg->output,
				"push rax\n"
			);

			gen_expr(cg, expr->binop.rhs, expected, vars, err);
			if(*err) goto RET;

			FPUTS_OR_ERR(
				cg->output,
				"mov rdi, rax\n"
				"pop rax\n"
			);
			break;
		}

		FPUTS_OR_ERR(cg->output, "mul rdi\n");
		break;

	case AST_DIV:
		gen_expr(cg, expr->binop.lhs, expected, vars, err);
		if(*err) goto RET;

		switch(cg->nodes[expr->binop.rhs].type) {
		case AST_INT_LIT:
			FPRINTF_OR_ERR(
				cg->output,
				"mov rdi, %ji\n",
				cg->nodes[expr->binop.rhs].int_lit.val
			);
			break;

		case AST_IDENT:
			do {} while(0);

			size_t indx = SIZE_MAX;
			for(size_t i = 0; i < vars->count; i++) {
				if(cg->nodes[expr->binop.rhs].ident.id == *(size_t*)dynarr_at(vars, i)) {
					indx = i;
					break;
				}
			}
			
			if(indx == SIZE_MAX) {
				INVALID_AST_NODE("Undeclared variable ", &cg->nodes[expr->binop.rhs]);
			}

			FPRINTF_OR_ERR(
				cg->output,
				"mov rdi, [rbp-%ji]\n",
				8*(indx + 1)
			);
			break;

		default:
			FPUTS_OR_ERR(
				cg->output,
				"push rax\n"
			);

			gen_expr(cg, expr->binop.rhs, expected, vars, err);
			if(*err) goto RET;

			FPUTS_OR_ERR(
				cg->output,
				"mov rdi, rax\n"
				"pop rax\n"
			);
			break;
		}

		FPUTS_OR_ERR(cg->output, "div rdi\n");
		break;
	
	case AST_INT_LIT:
		FPRINTF_OR_ERR(
			cg->output,
			"mov rax, %ji\n",
			expr->int_lit.val
		);
		break;
	
	case AST_IDENT:
		do {} while(0);
		
		size_t indx = SIZE_MAX;
		for(size_t i = 0; i < vars->count; i++) {
			if(cg->nodes[expr->binop.rhs].ident.id == *(size_t*)dynarr_at(vars, i)) {
				indx = i;
				break;
			}
		}
		
		if(indx == SIZE_MAX) {
			INVALID_AST_NODE("Undeclared variable ", &cg->nodes[expr->binop.rhs]);
		}

		FPRINTF_OR_ERR(
			cg->output,
			"mov rax, [rbp-%ji]\n",
			8 * (indx+1)
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

//clobbers: rax, rdx, rdi, rbp
void gen_fn_def(CodeGen *cg, size_t index, Error *err)
{
	DynArr vars = { 0 };
	dynarr_init(&vars, sizeof (size_t));

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
		// const AstNode *arg_ident = &cg->nodes[fn_type->fn_type.args[2*i]];
		const AstNode *arg_type = &cg->nodes[fn_type->fn_type.args[2*i + 1]];

		if(arg_type->type != AST_IDENT) {
			INVALID_AST_NODE(
				"Illegal Type for Function Parameter", arg_type
			);
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
			dynarr_push(&vars, &statement->var_decl.id, err);
			if(*err) goto RET;
		}
	}

	FPRINTF_OR_ERR(
		cg->output,
		"mov rbp, rsp\n"
		"sub rsp, %ji\n",
		8 * (vars.count + 1)
	);

	for(size_t i = 0; i < block->block.statement_count; i++) {
		const AstNode *statement = &cg->nodes[block->block.statements[i]];

		switch(statement->type) {
		case AST_RET:
			FPUTS_OR_ERR(cg->output, "xor edx, edx\n");
			gen_expr(cg, statement->ret.return_val, return_type, &vars, err);
			if(*err) goto RET;
	
			FPRINTF_OR_ERR(
				cg->output,
				"add rsp, %ji\n"
				"ret\n",
				8 * (vars.count + 1)
			);

			break;

		case AST_VAR_DECL:
			do {} while(0);

			if(statement->var_decl.initial) {
				Type var_type = identifier_to_type(cg->nodes[statement->var_decl.data_type].ident.id);	
				if(var_type.type == TYPE_INVALID) {
					INVALID_AST_NODE("Invalid Variable Type", &cg->nodes[statement->var_decl.data_type]);
				}

				FPUTS_OR_ERR(cg->output, "xor edx, edx\n");
				gen_expr(cg, statement->var_decl.initial, var_type, &vars, err);
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
		default:
			INVALID_AST_NODE("Expected Statement", statement);
		}
	}

RET:
	if(vars.data) dynarr_clean(&vars);
	return;	
}

void codegen_gen(CodeGen *cg, bool exec, Error *err)
{
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
