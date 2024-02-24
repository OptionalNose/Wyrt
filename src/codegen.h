#pragma once

#include "parser.h"

typedef struct {
	uintmax_t unnamed_counter;
} CodeGen;

void codegen_init(CodeGen *cg);

void codegen_gen(
	CodeGen *cg,
	const AstNode *nodes,
	size_t node_count,
	char *const *identifiers,
	const char *dump_path,
	Error *err
);
