#include <stdio.h>
#include <stdbool.h>

#include "lexer.h"
#include "parser.h"

typedef struct {
	char *srcFile;
	char *tokenDumpFile; // NULL == Do not dump
	char *astDumpFile;
} CmdlineOptions;

int main(int argc, char **argv)
{
	CmdlineOptions options = { 0 };
	Error err = ERROR_OK;

	Token *tokens = NULL;
	char **identifiers = NULL;
	size_t identifier_count = 0;
	size_t token_count = 0;

	AstNode *nodes = NULL;
	size_t node_count = 0;

	Lexer lexer = { 0 };
	
	for(int i = 1; i < argc; i++) {
		int string_length = 0;
		char garbage;
		if(
			sscanf(argv[i], "-h%c", &garbage)
			|| sscanf(argv[i], "--help%c", &garbage)
		) {
			printf(
				"Usage: wyrt [options] file\n"
				"Options:\n"
				"\t--token-dump=<path>\t\t\t\tDump Lexed Tokens into file <path>\n"
				"\t--ast-dump=<path>\t\t\t\tDump Parsed AST Nodes into file <path>\n"
			);
			goto RET;
		} else if(sscanf(argv[i], "--token-dump=%c", &garbage)) {
			sscanf(argv[i], "--token-dump=%*s%n", &string_length);
			if(string_length == 0) {
				err = ERROR_UNEXPECTED_DATA;
				fprintf(stderr, "Expected File Path.\n");
				goto RET;
			}
			char *path = malloc(string_length + 1);
			if(!path) {
				err = ERROR_OUT_OF_MEMORY;
				goto RET;
			}

			sscanf(argv[i], "--token-dump=%s", path);
			options.tokenDumpFile = path;
			printf("INFO: token dump to '%s'\n", path);
		} else if(sscanf(argv[i], "--ast-dump=%c", &garbage)) {
			sscanf(argv[i], "--ast-dump=%*s%n", &string_length);
			if(string_length == 0) {
				err = ERROR_UNEXPECTED_DATA;
				fprintf(stderr, "Expected File Path.\n");
				goto RET;
			}
			char *path = malloc(string_length + 1);
			if(!path) {
				err = ERROR_OUT_OF_MEMORY;
				goto RET;
			}

			sscanf(argv[i], "--ast-dump=%s", path);
			options.astDumpFile = path;
			printf("INFO: AST dump to '%s'\n", path);
		} else if(sscanf(argv[i], "%*1[^-]%c", &garbage)) {
			sscanf(argv[i], "%*1[^-]%*s%n", &string_length);
			char *path = malloc(string_length + 1);
			if(!path) {
				err = ERROR_OUT_OF_MEMORY;
				goto RET;
			}
			sscanf(argv[i], "%s", path);
			options.srcFile = path;
		} else {
			fprintf(
				stderr,
				"Unknown Option: '%s'. "
				"Run with '--help' to see list of available options.\n",
				argv[i]
			);
			err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
	}

	if(!options.srcFile) {
		fprintf(stderr, "No input files.\n");
		err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}


	lexer_init(&lexer, options.srcFile, &err);
	if(err) goto RET;

	lexer_tokenize(
		&lexer,
		&tokens, &token_count,
		&identifiers, &identifier_count,
		&err
	);
	if(err) goto RET;

	parser_gen_ast(
		tokens, token_count, &nodes, &node_count, identifiers, &err
	);
	if(err) goto RET;
	
	if(options.tokenDumpFile) {
		FILE *file = fopen(options.tokenDumpFile, "w");
		if(!file) {
			fprintf(stderr, "Unable to Open Token Dump File.\n");
			err = ERROR_NOT_FOUND;
			goto RET;
		}

		printf("INFO: %zi Tokens.\n", token_count);
		for(size_t i = 0; i < token_count; i++) {
			lexer_print_token_to_file(
				file, &tokens[i], identifiers
			);
		}
		fclose(file);
	}
	
	if(options.astDumpFile) {
		FILE *file = fopen(options.astDumpFile, "w");
		if(!file) {
			fprintf(stderr, "Unable to Open AST Dump File.\n");
			err = ERROR_NOT_FOUND;
			goto RET;
		}

		printf("INFO: %zi AST Nodes.\n", node_count);

		parser_print_ast_to_file(
			file,
			nodes, node_count, identifiers
		);
	}
	
RET:
	free(options.srcFile);
	free(options.tokenDumpFile);
	free(options.astDumpFile);
	lexer_clean(&lexer);
	lexer_clean_identifiers(identifiers, identifier_count);
	parser_clean_ast(nodes, node_count);
	free(nodes);
	free(tokens);
	return err;
}
