#pragma once

#include "parser.h"

typedef struct {
	size_t id;
	size_t ret; // indices into nodes
	size_t arg_count;
	size_t *args; // indices into nodes
} FnSig;

typedef struct {
	FILE *output;

	const AstNode *nodes;
	size_t node_count;
	char *const *identifiers;
	size_t fn_sig_count;
	FnSig *fn_sigs;
} CodeGen;

void codegen_init(
		CodeGen *cg,
		FILE *output,
		const AstNode *nodes,
		size_t node_count,
		char *const *identifiers
);

void codegen_clean(const CodeGen *cg);

void codegen_gen(CodeGen *cg, bool exec, Error *err);
