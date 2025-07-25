#pragma once

#include <stddef.h>

#include "parser.h"

#include "types.h"

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
	size_t id;
	Type ret;
	size_t arg_count;
	Type *args;
	size_t *arg_ids;
} FnSig;

typedef struct {
	size_t id;
	Type type;
	bool mut;
	bool declared;
	bool arg;
} Var;

typedef struct {
	Var *params;
	size_t param_count;
	Var *vars;
	size_t var_count;
	TypeContext tc;
} Scope;

typedef struct {
	FILE *output;
	const char *target_triple;

	const AstNode *nodes;
	size_t node_count;
	char *const *identifiers;
	size_t fn_sig_count;
	FnSig *fn_sigs;

	size_t metadata_counter;
} CodeGen;

void codegen_init(
	CodeGen *cg,
	FILE *output,
	const char *target_triple,
	const AstNode *nodes,
	size_t node_count,
	char *const *identifiers
);

void codegen_clean(const CodeGen *cg);

void codegen_gen(CodeGen *cg, Error *err);

void scope_init(Scope *scope, const Scope *parent, Error *err);
void scope_clean(const Scope *scope);
