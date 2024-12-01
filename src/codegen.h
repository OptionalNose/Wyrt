#pragma once

#include "parser.h"

typedef struct {
	FILE *output;

	const AstNode *nodes;
	size_t node_count;
	char *const *identifiers;

	uintmax_t md_counter;
	uintmax_t ssa_counter;
} CodeGen;

void codegen_init(
		CodeGen *cg,
		FILE *output,
		const AstNode *nodes,
		size_t node_count,
		char *const *identifiers
);

void codegen_gen(CodeGen *cg, Error *err);
