#pragma once

#include "lexer.h"

// Relative Offset
// 'next' field for chains: 0 == end
typedef int16_t Offset;

typedef enum {
	AST_NONE,
	AST_MODULE,
	AST_FN_DEF,
	AST_FN_TYPE,
	AST_IDENT,
	AST_RET,
	AST_INT_LIT,
	AST_CHAR_LIT,

	AST_BLOCK,

	AST_MUL,
	AST_DIV,
	AST_ADD,
	AST_SUB,

	AST_COMP_EQ,
	AST_COMP_GE,
	AST_COMP_LE,
	AST_COMP_NE,
	AST_COMP_GT,
	AST_COMP_LT,

	AST_LOGIC_AND,
	AST_LOGIC_OR,
	AST_LOGIC_NOT,

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
	AST_ARROW,
	
	AST_IF
} AstNodeType;

typedef struct {
	AstNodeType type;
	DebugInfo debug;
	Offset next;
} AstNodeCommon;

typedef union {
	AstNodeType type;
	AstNodeCommon com;

	struct {
		AstNodeCommon com;
		Id id;
		Offset fn_type;
		Offset block;
	} fn_def;

	struct {
		AstNodeCommon com;
		Offset args;
		Offset ret_type;
		uint8_t arg_count; // Fast Checking of signature
	} fn_type;

	struct {
		AstNodeCommon com;
		Offset return_val;
	} ret;

	struct {
		AstNodeCommon com;
		intmax_t val;
	} int_lit;

	struct {
		AstNodeCommon com;
		Id id;
	} ident;

	struct {
		AstNodeCommon com;
		Offset statements;
	} module;

	struct {
		AstNodeCommon com;
		Offset statements;
	} block;

	struct {
		AstNodeCommon com;
		Offset lhs;
		Offset rhs;
	} binop;

	struct {
		AstNodeCommon com;
		Id id;
		Offset data_type;
		Offset initial;
		bool mut;
	} var_decl;

	struct {
		AstNodeCommon com;
		Id fn_id;
		Offset args;
		uint8_t arg_count; // Fast Signature Checking
	} fn_call;

	struct {
		AstNodeCommon com;
		Offset var;
		Offset expr;
	} assign;

	struct {
		AstNodeCommon com;
		Offset val;
	} unary_op;

	struct {
		AstNodeCommon com;
		Offset base_type;
	} pointer_type;

	struct {
		AstNodeCommon com;
		Offset elem_type;
		size_t len;
	} array;

	struct {
		AstNodeCommon com;
		Offset elem_type;
	} slice;

	struct {
		AstNodeCommon com;
		Offset arr;
		Offset index;
	} subscript;

	struct {
		AstNodeCommon com;
		Offset elems;
		uint32_t elem_count; // Fast Type Checking
	} array_lit;

	struct {
		AstNodeCommon com;
		Offset member_names;
		Offset member_types;
		uint8_t member_count; // Fast Type Checking
	} struct_type;

	struct {
		AstNodeCommon com;
		Id parent_id; // 0 == anonymous
		Offset member_names;
		Offset member_values;
		uint8_t member_count; // Fast Type Checking
	} struct_lit;

	struct {
		AstNodeCommon com;
		Id member_id;
		Offset parent;
	} struct_access;

	struct {
		AstNodeCommon com;
		size_t id;
	} string_lit;

	struct {
		AstNodeCommon com;
		Offset name;
	} extrn;

	struct {
		AstNodeCommon com;
		Offset value;
	} discard;

	struct {
		AstNodeCommon com;
		Offset parent;
		Offset member;
	} arrow;

	struct {
		AstNodeCommon com;
		Id id;
		Offset backing;
	} typdef;

	struct {
		AstNodeCommon com;
		char val;
	} char_lit;

	struct {
		AstNodeCommon com;
		Offset decl; // 0 == None
		Offset condition; // 0 == None
		Offset block;
		Offset else_block;
	} if_statement;
} AstNode;

typedef struct {
	AstNode *nodes;
	size_t len;
	size_t cap;
} NodeList;

void nodelist_alloc(NodeList *list, size_t n, Error *err);
void nodelist_push(NodeList *list, AstNode node, Error *err);
AstNode nodelist_pop(NodeList *list);

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
	X(VAR_DECL) \
	X(VAR_DECL_INIT) \
	X(ASSIGNMENT) \
	X(STRUCT_TYPE) \
	X(EXTERN) \
	X(SEMICOLON) \
	X(IF) \
	X(CONDITION) \
	X(ELSE) \
	X(RPAREN)

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
