#pragma once 
#include "util.h"

typedef enum {
	TOKEN_NONE,
	TOKEN_EOF,
	
	TOKEN_COLON,
	
	TOKEN_LPAREN,
	TOKEN_RPAREN,
	
	TOKEN_ASSIGN,
	TOKEN_ADD_ASSIGN,
	TOKEN_SUB_ASSIGN,
	TOKEN_MUL_ASSIGN,
	TOKEN_DIV_ASSIGN,
	
	TOKEN_LCURLY,
	TOKEN_RCURLY,

	TOKEN_SEMICOLON,

	TOKEN_RETURN,
	
	TOKEN_CONST,
	TOKEN_VAR,
	TOKEN_ABYSS,
	TOKEN_FN,
	
	TOKEN_IDENT,
	TOKEN_INT_LIT,

	TOKEN_STAR,
	TOKEN_FSLASH,
	TOKEN_PLUS,
	TOKEN_MINUS,

	TOKEN_COMMA,

	TOKEN_AMPERSAND,

	TOKEN_UNDERSCORE,
	
	TOKEN_LSQUARE,
	TOKEN_RSQUARE,

	TOKEN_PERIOD,

	TOKEN_STRUCT
} TokenType;

typedef struct {
	char *file;
	uint32_t line;
	uint32_t col;
} DebugInfo;

typedef union {
	TokenType type;

	struct {
		TokenType type;
		DebugInfo debug_info;
	} debug;
	
	struct {
		TokenType type;
		DebugInfo debug_info;
		size_t id;
	} ident;

	struct {
		TokenType type;
		DebugInfo debug_info;
		intmax_t val;
	} int_lit;
} Token;

typedef struct {
	char *file_contents;
	size_t file_length;
	char *file_path;
} Lexer;

void lexer_init(Lexer *lex, char *file_path, Error *err);
void lexer_clean(Lexer *lex);
void lexer_clean_identifiers(char **identifiers, size_t identifier_count);

void lexer_tokenize(
	Lexer *lex,
	Token **tokens, size_t *token_count,
	char ***identifiers, size_t *identifier_count,
	Error *err
);

void lexer_print_token_to_file(
	FILE *file,
	Token const *tok,
	char *const *identifiers
);

void lexer_print_debug_to_file(FILE *file, DebugInfo const *debug);
