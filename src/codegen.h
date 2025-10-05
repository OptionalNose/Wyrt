#pragma once

#include <stddef.h>
#include <libgccjit.h>

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

typedef enum {
	GEN_EXE,
	GEN_SHR,
	GEN_OBJ,
	GEN_ASM
} GenType;

typedef struct {
	size_t id;
	size_t linkage_name;
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
} Var;

typedef struct {
	Var *params;
	size_t param_count;
	Var *vars;
	size_t var_count;
	TypeContext tc;
	
	gcc_jit_lvalue **gcc_vars;
	gcc_jit_rvalue **gcc_params;
} Scope;

typedef struct {
	const AstNode *nodes;
	size_t node_count;
	char *const *identifiers;
	char *const *strings;
	size_t string_count;
	FnSig *fn_sigs;
	gcc_jit_function **fns;
	size_t fn_count;

	gcc_jit_context *gcc;

	Type *named_types;
	char **named_type_names;
	gcc_jit_type **named_types_gcc;
	size_t named_type_count;
} CodeGen;

void codegen_init(
	CodeGen *cg,
	const AstNode *nodes,
	size_t node_count,
	char *const *identifiers,
	char *const *strings,
	size_t string_count
);

void codegen_clean(const CodeGen *cg);

void codegen_gen(CodeGen *cg, GenType gen_type, const char *path, const char *ir_dump, Error *err);

void scope_init(Scope *scope, const Scope *parent, Error *err);
void scope_clean(const Scope *scope);
