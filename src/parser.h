#pragma once

#include "lexer.h"
#include "util.h"

typedef enum {
	AST_NONE,
	AST_DECL,
	AST_FN_TYPE,
	AST_CONST,
	AST_IDENT,
	AST_BLOCK,
	AST_RET,
	AST_INT_LIT,
	AST_MODULE,
} AstNodeType;

typedef union {
	AstNodeType type;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
	} debug;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t access;
		size_t ident;
		size_t data_type;
	 	size_t initial; // 0 == NONE
	} decl;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t *args; // (arg, type)
		size_t arg_count;
		size_t ret_type;
	} fn_type;
	
	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t *statements; // (arg, type)
		size_t statement_count;
	} block;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t return_val;
	} ret;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		intmax_t val;
	} int_lit;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t id;
	} ident;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t *statements;
		size_t statement_count;
	} module;
	
} AstNode;

void parser_gen_ast(
	Token const *tokens, size_t token_count,
	AstNode **nodes, size_t *node_count,
	char *const *identifiers,
	Error *err
);

void parser_clean_ast(AstNode *nodes, size_t node_count);

void parser_print_ast_to_file(
	FILE *file,
	AstNode *nodes, size_t node_count, char *const *identifiers
);