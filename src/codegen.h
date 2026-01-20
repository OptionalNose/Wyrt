#pragma once

#include <stddef.h>

#include "parser.h"
#include "types.h"
#include "backend.h"

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
	
	WyrtLvalue *be_vars;
	WyrtRvalue *be_params;
} Scope;

typedef struct {
	const AstNode *nodes;
	size_t node_count;
	char *const *identifiers;
	char *const *strings;
	size_t string_count;
	FnSig *fn_sigs;
	WyrtFunction **fns;
	size_t fn_count;

	void *dl;
	WyrtBackend be;
	WyrtContext ctx;
} CodeGen;

void codegen_init(
	CodeGen *cg,
	const AstNode *nodes,
	size_t node_count,
	char *const *identifiers,
	char *const *strings,
	size_t string_count,
	char const *dlpath,
	Error *err
);

void codegen_clean(const CodeGen *cg);

void codegen_gen(CodeGen *cg, GenOptions options, const char *path, Error *err);

void scope_init(Scope *scope, const Scope *parent, Error *err);
void scope_clean(const Scope *scope);
