#pragma once

#include "parser.h"

typedef struct {
	FILE *output;

	const AstNode *nodes;
	size_t node_count;
	char *const *identifiers;
} CodeGen;

void codegen_init(
		CodeGen *cg,
		FILE *output,
		const AstNode *nodes,
		size_t node_count,
		char *const *identifiers
);

void codegen_gen(CodeGen *cg, bool exec, Error *err);
