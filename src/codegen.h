#pragma once

#include "parser.h"
typedef union {
	enum {
		TYPE_INVALID,
		TYPE_PRIMITIVE_U8,
		TYPE_PRIMITIVE_VOID,
	} type;
} Type;

typedef struct {
	size_t id;
	Type ret;
	size_t arg_count;
	Type *args;
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
