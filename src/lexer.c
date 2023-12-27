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
	free(lex->file_contents);
}

void lexer_clean_identifiers(DynArr *identifiers)
{
	for(size_t i = 3; i < identifiers->count - 1; i++) {
		free(dynarr_at(identifiers, i));
	}
	dynarr_clean(identifiers);
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
	DynArr *tokens,
	DynArr *identifiers,
	Error *err
)
{
	dynarr_init(tokens, sizeof(Token));
	dynarr_init(identifiers, sizeof(char *));

	dynarr_push(identifiers, &(char *){"main"}, err);
	if(*err) goto RET;
	dynarr_push(identifiers, &(char *){"void"}, err);
	if(*err) goto RET;
	dynarr_push(identifiers, &(char *){"u8"}, err);
	if(*err) goto RET;
	
	size_t pos = 0;
	uint32_t line = 1;
	uint32_t col = 1;
	uint32_t prev_col = 1;

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
		default:
			break;
		}
	
		if(isdigit(*(char *)dynarr_at(&string_builder, 0))) {
			intmax_t val = *(char *)dynarr_at(&string_builder, 0) - '0';
			while(
				isdigit(
					c = skip_whitespace(lex, &pos, &line, &col, &prev_col)
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

				if(
					string_builder.count == strlen("const")
					&& !memcmp(
						string_builder.data, "const", strlen("const")
					)
				) {
					tok.type = TOKEN_CONST;
					goto NEXT_TOK;
				} else if(
					string_builder.count == strlen("return")
					&& !memcmp(
						string_builder.data, "return", strlen("return")
					)
				) {
					tok.type = TOKEN_RETURN;
					goto NEXT_TOK;
				}
			}
			dynarr_push(&string_builder, &(char){'\0'}, err);
			if(*err) goto RET;

			bool found = false;
			size_t id;
			for(size_t i = 0; i < identifiers->count; i++) {
				if(
					strcmp(
						string_builder.data, *(char **)dynarr_at(identifiers, i)
					) == 0
				) {
					found = true;
					id = i;
				}
			}
			if(!found) {
				id = identifiers->count;
				dynarr_push(identifiers, &string_builder.data, err);
				if(*err) goto RET;
			}

			tok.ident.id = id;
			tok.type = TOKEN_IDENT;
			backup(lex, &pos, &line, &col, prev_col);
			goto NEXT_TOK;
		}

		c = get_char(lex, &pos, &line, &col, &prev_col);
		if(c == EOF) {
			fprintf(stderr, "Unexpected EOF\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
		dynarr_push(&string_builder, &c, err);

		if(memcmp(string_builder.data, "->", 2)) {
			fprintf(
				stderr,
				"Invalid Token '%c%c' at ",
				*(char *)dynarr_at(&string_builder, 0),
				*(char *)dynarr_at(&string_builder, 1)
			);
			lexer_print_debug_to_file(stderr, &tok.debug.debug_info);
			fputc('\n', stderr);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		tok.type = TOKEN_ARROW;	
		
NEXT_TOK:
		dynarr_push(tokens, &tok, err);
		if(*err) goto RET;
	}
	
RET:
	dynarr_clean(&string_builder);
	return;
}

void lexer_print_token_to_file(
	FILE *file,
	Token *tok,
	DynArr *identifiers
)
{
	switch(tok->type) {
	case TOKEN_NONE:
		fputs("NONE", file);
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
	case TOKEN_LCURLY:
		fputs("{", file);
		break;
	case TOKEN_RCURLY:
		fputs("}", file);
		break;
	case TOKEN_SEMICOLON:
		fputs(";", file);
		break;
	case TOKEN_ARROW:
		fputs("->", file);
		break;
	case TOKEN_RETURN:
		fputs("return", file);
		break;
	case TOKEN_CONST:
		fputs("const", file);
		break;
	case TOKEN_IDENT:
		fprintf(
			file,
			"Identifier '%s'",
			*(char **)dynarr_at(identifiers, tok->ident.id)
		);
		break;
	case TOKEN_INT_LIT:
		fprintf(file, "Int '%ji'", tok->int_lit.val);
		break;
	}
	
	fputs("\t\t\t", file);
	lexer_print_debug_to_file(file, &tok->debug.debug_info);
	fputc('\n', file);
}

void lexer_print_debug_to_file(FILE *file, DebugInfo *const debug)
{
	fprintf(
		file,
		"%s:%"PRIu32":%"PRIu32,
		debug->file, debug->line, debug->col
	);
}
