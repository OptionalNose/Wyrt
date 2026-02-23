#pragma once

#include "lexer.h"

typedef enum {
	AST_NONE,
	AST_MODULE,
	AST_FN_DEF,
	AST_FN_TYPE,
	AST_IDENT,
	AST_BLOCK,
	AST_RET,
	AST_INT_LIT,

	AST_MUL,
	AST_DIV,
	AST_ADD,
	AST_SUB,

	AST_VAR_DECL,

	AST_FN_CALL,

	AST_ASSIGN,
	AST_ADD_ASSIGN,
	AST_SUB_ASSIGN,
	AST_MUL_ASSIGN,
	AST_DIV_ASSIGN,

	AST_DEREF,
	AST_ADDR,
	AST_POINTER_CONST,
	AST_POINTER_VAR,
	AST_POINTER_ABYSS,

	AST_ARRAY,
	AST_SLICE_CONST,
	AST_SLICE_VAR,
	AST_SLICE_ABYSS,

	AST_SUBSCRIPT,

	AST_ARRAY_LIT,

	AST_STRUCT_TYPE,
	AST_STRUCT_LIT,
	AST_STRUCT_ACCESS,

	AST_STRING_LIT,
	AST_ZSTRING_LIT,
	AST_CSTRING_LIT,

	AST_EXTERN,
	AST_DISCARD,

	AST_TYPEDEF,
	AST_ARROW
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
		size_t ident;
		size_t fn_type;
		size_t block;
	} fn_def;

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
		size_t *statements;
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

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t lhs;
		size_t rhs;
	} binop;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		bool mut;
		size_t id;
		size_t data_type;
		size_t initial; // 0 == none
	} var_decl;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t fn_id;
		size_t arg_count;
		size_t *args;
	} fn_call;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t var;
		size_t expr;
	} assign;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t base;
	} addr;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t base_type;
	} pointer_type;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t ptr;
	} deref;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t elem_type;
		size_t len; //0 == _
	} array;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t elem_type;
	} slice;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t arr;
		size_t index;
	} subscript;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t elem_count;
		size_t *elems;
	} array_lit;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t member_count;
		size_t *member_name_ids;
		size_t *member_types;
	} struct_type;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t member_count;
		size_t *member_name_ids;
		size_t *member_values;
	} struct_lit;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t parent;
		size_t member_id;
	} struct_access;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t id;
	} string_lit;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t name;
	} extrn;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t value;
	} discard;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t parent;
		size_t member;
	} arrow;

	struct {
		AstNodeType type;
		DebugInfo debug_info;
		size_t id;
		size_t backing;
	} typdef;
} AstNode;

typedef struct {
	AstNode *nodes;
	size_t len;
	size_t cap;
} NodeList;

void nodelist_alloc(NodeList *list, size_t n, Error *err);
void nodelist_push(NodeList *list, AstNode node, Error *err);
AstNode nodelist_pop(NodeList *list);
void nodelist_clean(NodeList *list);

#define PARSE_STATE_LIST \
	X(MODULE) \
	X(FN_DEF) \
	X(IDENT) \
	X(FN_TYPE) \
	X(FN_TYPE_LIST) \
	X(FN_TYPE_ARG) \
	X(FN_TYPE_RET) \
	X(TYPE) \
	X(FN_BODY) \
	X(BLOCK) \
	X(BLOCK_LIST) \
	X(EXPR) \
	X(VAR_DECL_INIT) \
	X(ASSIGNMENT) \
	X(STRUCT_TYPE) \
	X(EXTERN) \
	X(SEMICOLON)

typedef enum {
#define X(n) PARSE_STATE_ ##n,
PARSE_STATE_LIST
#undef X
} ParseStateType;

typedef struct {
	ParseStateType type;
	size_t ref; // index into ast
} ParseState;

typedef struct {
	ParseState *state;
	size_t len;
	size_t cap;
} ParseStack;

void parsestack_alloc(ParseStack *ps, size_t n, Error *err);
void parsestack_push(ParseStack *ps, ParseState state, Error *err);
ParseState parsestack_pop(ParseStack *ps);
ParseState *parsestack_top(ParseStack *ps);
ParseState *parsestack_from_top(ParseStack *ps, size_t i);

typedef struct {
	Token const *tokens;
	NodeList ast;
	ParseStack parse_stack;
	char *const *identifiers;
	char *const *strings;
} Parser;

void parser_init(
	Parser *prs,
	Token const *tokens,
	char *const *identifiers,
	char *const *strings
);
void parser_clean(Parser *prs);

void parser_print_ast(Parser *prs, FILE *file);
void parser_parse(Parser *prs, Error *err);
