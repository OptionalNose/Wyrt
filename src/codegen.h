#pragma once

#include <stddef.h>

#include "parser.h"

#include "types.h"

typedef struct {
	size_t id;
	size_t ret; // indices into nodes
	size_t arg_count;
	size_t *args; // indices into nodes
} FnSig;

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
	ptrdiff_t start; //- == on stack, 0 == reg param, + == stack param
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

typedef struct {
	FILE *output;
	PlatformType plat;

	const AstNode *nodes;
	size_t node_count;
	char *const *identifiers;
	size_t fn_sig_count;
	FnSig *fn_sigs;
} CodeGen;

void codegen_init(
		CodeGen *cg,
		FILE *output,
		PlatformType plat,
		const AstNode *nodes,
		size_t node_count,
		char *const *identifiers
);

void codegen_clean(const CodeGen *cg);

void codegen_gen(CodeGen *cg, bool exec, Error *err);

void codegen_asm(CodeGen *cg, bool exec, Error *err);
