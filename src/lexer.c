#include "lexer.h"

#include <ctype.h>
#include <string.h>
#include <inttypes.h>

void lexer_init(Lexer *lex, char *file_path, Error *err)
{
	lex->file_path = file_path;

	FILE *file = fopen(file_path, "rb");
	if(!file) {
		fprintf(stderr, "Input File Does Not Exist.\n");
		*err = ERROR_NOT_FOUND;
		goto RET;
	}

	fseek(file, 0, SEEK_END);
	lex->file_length = ftell(file);
	fseek(file, 0, SEEK_SET);
	
	lex->file_contents = malloc(lex->file_length);
	CHECK_MALLOC(lex->file_contents);
	
	if(
		fread(
			lex->file_contents,
			1, lex->file_length,
			file
		) < lex->file_length
	) {
		fprintf(stderr, "Error Reading from Input File.\n");
		*err = ERROR_IO;
		goto RET;
	}
	
	
RET:
	if(file) fclose(file);
	return;
}

void lexer_clean(Lexer *lex)
{
	if(lex->file_contents) free(lex->file_contents);
}

void lexer_clean_identifiers(char **identifiers, size_t identifier_count)
{
	if(!identifiers) return;
	for(size_t i = 5; i < identifier_count; i++) {
		free(identifiers[i]);
	}
	free(identifiers);
}

static int get_char(
	Lexer *lex,
	size_t *pos, uint32_t *line, uint32_t *col, uint32_t *prev_col
)
{
	if(++*pos >= lex->file_length) {
		return EOF;
	}
	*prev_col = *col;
	char ret = lex->file_contents[*pos - 1];
	if(ret == '\n') {
		*col = 0;
		*line += 1;
	} else {
		*col += 1;
	}

	return ret;
}

static int skip_whitespace(
	Lexer *lex,
	size_t *pos, uint32_t *line, uint32_t *col, uint32_t *prev_col
)
{
	int c = ' ';
	while(isspace(c)) {
		c = get_char(lex, pos, line, col, prev_col);
	}

	return c;
}

static bool is_valid_in_identifier(char c)
{
	return isalnum(c) || c == '_' || c == '@';
}

static void backup(
	Lexer *lex,
	size_t *pos,
	uint32_t *line,
	uint32_t *col,
	uint32_t prev_col
)
{
	if(lex->file_contents[--*pos] == '\n') {
		--*line;
		*col = prev_col;
	} else {
		--*col;
	}
}

void lexer_tokenize(
	Lexer *lex,
	Token **tokens, size_t *token_count,
	char ***identifiers, size_t *identifier_count,
	Error *err
)
{
	DynArr toks;
	DynArr idents;
	dynarr_init(&toks, sizeof(Token));
	dynarr_init(&idents, sizeof(char *));

	size_t pos = 0;
	uint32_t line = 1;
	uint32_t col = 1;
	uint32_t prev_col = 1;

	dynarr_push(&idents, &(char *){"u8"}, err);
	if(*err) goto RET;
	dynarr_push(&idents, &(char *){"u16"}, err);
	if(*err) goto RET;
	dynarr_push(&idents, &(char *){"u32"}, err);
	if(*err) goto RET;
	dynarr_push(&idents, &(char *){"u64"}, err);
	if(*err) goto RET;
	dynarr_push(&idents, &(char *){"void"}, err);
	if(*err) goto RET;
	

	DynArr string_builder;
	dynarr_init(&string_builder, sizeof(char));

	while(pos < lex->file_length) {
		int c = skip_whitespace(lex, &pos, &line, &col, &prev_col);
		if(c == EOF) {
			goto RET;
		}

		string_builder.count = 0;
		Token tok = (Token) {
			.debug = {
				.type = TOKEN_NONE,
				.debug_info.line = line,
				.debug_info.col = col,
				.debug_info.file = lex->file_path,
			}
		};
		
		dynarr_push(&string_builder, &c, err);
		if(*err) goto RET;

		switch(*(char *)dynarr_at(&string_builder, 0)) {
		case ':':
			tok.type = TOKEN_COLON;
			goto NEXT_TOK;
		case '(':
			tok.type = TOKEN_LPAREN;
			goto NEXT_TOK;
		case ')':
			tok.type = TOKEN_RPAREN;
			goto NEXT_TOK;
		case '=':
			tok.type = TOKEN_ASSIGN;
			goto NEXT_TOK;
		case '{':
			tok.type = TOKEN_LCURLY;
			goto NEXT_TOK;
		case '}':
			tok.type = TOKEN_RCURLY;
			goto NEXT_TOK;
		case ';':
			tok.type = TOKEN_SEMICOLON;
			goto NEXT_TOK;
		case '*':
			c = get_char(lex, &pos, &line, &col, &prev_col);
			if(c == '=') {
				tok.type = TOKEN_MUL_ASSIGN;
				goto NEXT_TOK;
			}
			backup(lex, &pos, &line, &col, prev_col);
			tok.type = TOKEN_STAR;
			goto NEXT_TOK;
		case '/':
			c = get_char(lex, &pos, &line, &col, &prev_col);
			if(c == '=') {
				tok.type = TOKEN_DIV_ASSIGN;
				goto NEXT_TOK;
			}
			backup(lex, &pos, &line, &col, prev_col);
			tok.type = TOKEN_FSLASH;
			goto NEXT_TOK;
		case '+':
			c = get_char(lex, &pos, &line, &col, &prev_col);
			if(c == '=') {
				tok.type = TOKEN_ADD_ASSIGN;
				goto NEXT_TOK;
			}
			backup(lex, &pos, &line, &col, prev_col);
			tok.type = TOKEN_PLUS;
			goto NEXT_TOK;
		case '-':
			c = get_char(lex, &pos, &line, &col, &prev_col);
			if(c == '=') {
				tok.type = TOKEN_SUB_ASSIGN;
				goto NEXT_TOK;
			}
			backup(lex, &pos, &line, &col, prev_col);
			tok.type = TOKEN_MINUS;
			goto NEXT_TOK;			
		case ',':
			tok.type = TOKEN_COMMA;
			goto NEXT_TOK;
		case '&':
			tok.type = TOKEN_AMPERSAND;
			goto NEXT_TOK;
		case '[':
			tok.type = TOKEN_LSQUARE;
			goto NEXT_TOK;
		case ']':
			tok.type = TOKEN_RSQUARE;
			goto NEXT_TOK;
		case '.':
			tok.type = TOKEN_PERIOD;
			goto NEXT_TOK;
		default:
			break;
		}
	
		if(isdigit(*(char *)dynarr_at(&string_builder, 0))) {
			intmax_t val = *(char *)dynarr_at(&string_builder, 0) - '0';
			while(
				isdigit(
					c = get_char(lex, &pos, &line, &col, &prev_col)
				)
			) {
				val *= 10;
				val += c - '0';
			}
			if(isalpha(c)) {
				fprintf(stderr, "Invalid Integer Literal at ");
				lexer_print_debug_to_file(stderr, &tok.debug.debug_info);
				fputc('\n', stderr);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
			
			tok.type = TOKEN_INT_LIT;
			tok.int_lit.val = val;
			backup(lex, &pos, &line, &col, prev_col);
			goto NEXT_TOK;
		}

		if(isalpha(c) || c == '_' || c == '@') {
			while(
				is_valid_in_identifier(
					c = get_char(lex, &pos, &line, &col, &prev_col)
				)
				&& c != EOF
			) {
				dynarr_push(&string_builder, &c, err);
				if(*err) goto RET;
			}
			backup(lex, &pos, &line, &col, prev_col);
			if(string_builder.count == 1) {
				if(*(char*)dynarr_at(&string_builder, 0) == '_') {
					tok.type = TOKEN_UNDERSCORE;
					goto NEXT_TOK;
				}
			}
			dynarr_push(&string_builder, &(char){'\0'}, err);
			if(*err) goto RET;

			if(strcmp(string_builder.data, "return") == 0) {
				tok.type = TOKEN_RETURN;
				goto NEXT_TOK;
			} else if(strcmp(string_builder.data, "const") == 0) {
				tok.type = TOKEN_CONST;
				goto NEXT_TOK;
			} else if(strcmp(string_builder.data, "fn") == 0) {
				tok.type = TOKEN_FN;
				goto NEXT_TOK;
			} else if(strcmp(string_builder.data, "var") == 0) {
				tok.type = TOKEN_VAR;
				goto NEXT_TOK;
			} else if(strcmp(string_builder.data, "abyss") == 0) {
				tok.type = TOKEN_ABYSS;
				goto NEXT_TOK;
			} else if(strcmp(string_builder.data, "struct") == 0) {
				tok.type = TOKEN_STRUCT;
				goto NEXT_TOK;
			}

			bool found = false;
			size_t id;
			for(size_t i = 0; i < idents.count; i++) {
				if(
					strcmp(
						string_builder.data,
						*(char **)dynarr_at(&idents, i)
					) == 0
				) {
					found = true;
					id = i;
				}
			}
			if(!found) {
				id = idents.count;
				char *identifier = malloc(string_builder.count + 1);
				CHECK_MALLOC(identifier);
				strcpy(identifier, string_builder.data);
				dynarr_push(&idents, &identifier, err);
				if(*err) goto RET;
			}

			tok.ident.id = id;
			tok.type = TOKEN_IDENT;
			goto NEXT_TOK;
		}

NEXT_TOK:
		dynarr_push(&toks, &tok, err);
		if(*err) goto RET;
	}
	
RET:
	dynarr_push(
		&toks,
		&(Token) {
			.debug = {
				.type = TOKEN_EOF,
				.debug_info = {
					.file = lex->file_path,
					.line = line,
					.col = col,
				},
			},
		},
		err
	);

	dynarr_clean(&string_builder);
	*identifiers = (char **)idents.data;
	*identifier_count = idents.count;
	*tokens = (Token *)toks.data;
	*token_count = toks.count;
	return;
}

void lexer_print_token_to_file(
	FILE *file,
	Token const *tok,
	char *const *identifiers
)
{
	switch(tok->type) {
	case TOKEN_NONE:
		fputs("NONE", file);
		break;
	case TOKEN_EOF:
		fputs("EOF", file);
		break;
	case TOKEN_COLON:
		fputs(":", file);
		break;
	case TOKEN_LPAREN:
		fputs("(", file);
		break;
	case TOKEN_RPAREN:
		fputs(")", file);
		break;
	case TOKEN_ASSIGN:
		fputs("=", file);
		break;
	case TOKEN_ADD_ASSIGN:
		fputs("+=", file);
		break;
	case TOKEN_MUL_ASSIGN:
		fputs("*=", file);
		break;
	case TOKEN_SUB_ASSIGN:
		fputs("-=", file);
		break;
	case TOKEN_DIV_ASSIGN:
		fputs("/=", file);
		break;
	case TOKEN_LCURLY:
		fputs("{", file);
		break;
	case TOKEN_RCURLY:
		fputs("}", file);
		break;
	case TOKEN_SEMICOLON:
		fputs(";", file);
		break;
	case TOKEN_RETURN:
		fputs("return", file);
		break;
	case TOKEN_CONST:
		fputs("const", file);
		break;
	case TOKEN_FN:
		fputs("fn", file);
		break;
	case TOKEN_VAR:
		fputs("var", file);
		break;
	case TOKEN_ABYSS:
		fputs("abyss", file);
	case TOKEN_IDENT:
		fprintf(
			file,
			"Identifier '%s'",
			identifiers[tok->ident.id]
		);
		break;
	case TOKEN_INT_LIT:
		fprintf(file, "Int '%ji'", tok->int_lit.val);
		break;
	case TOKEN_STAR:
		fprintf(file, "'*'");
		break;
	case TOKEN_FSLASH:
		fprintf(file, "'/'");
		break;
	case TOKEN_PLUS:
		fprintf(file, "'+'");
		break;
	case TOKEN_MINUS:
		fprintf(file, "'-'");
		break;
	case TOKEN_COMMA:
		fprintf(file, "','");
		break;
	case TOKEN_AMPERSAND:
		fprintf(file, "'&'");
		break;
	case TOKEN_UNDERSCORE:
		fprintf(file, "'_'");
		break;
	case TOKEN_LSQUARE:
		fprintf(file, "'['");
		break;
	case TOKEN_RSQUARE:
		fprintf(file, "']'");
		break;
	case TOKEN_PERIOD:
		fprintf(file, ".");
		break;
	case TOKEN_STRUCT:
		fprintf(file, "struct");
		break;
	}
	
	fputs(" at ", file);
	lexer_print_debug_to_file(file, &tok->debug.debug_info);
	fputc('\n', file);
}

void lexer_print_debug_to_file(FILE *file, DebugInfo const *debug)
{
	fprintf(
		file,
		"%s:%"PRIu32":%"PRIu32,
		debug->file, debug->line, debug->col
	);
}
