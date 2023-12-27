#include <stdio.h>
#include <stdbool.h>

#include "lexer.h"

typedef struct {
	char *srcFile;
	char *tokenDumpFile; // NULL == Do not dump
} CmdlineOptions;

int main(int argc, char **argv)
{
	CmdlineOptions options = { 0 };
	Error err = ERROR_OK;

	DynArr tokens = { 0 };
	DynArr identifiers = { 0 };

	FILE *file = NULL;
	
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

	Lexer lexer;
	lexer_init(&lexer, options.srcFile, &err);
	if(err) goto RET;


	dynarr_init(&tokens, sizeof(Token));
	dynarr_init(&identifiers, sizeof(char *));

	lexer_tokenize(&lexer, &tokens, &identifiers, &err);
	if(err) goto RET;

	file = fopen(options.tokenDumpFile, "w");
	if(options.tokenDumpFile) {
		if(!file) {
			fprintf(stderr, "Unable to Open Token Dump File.\n");
			err = ERROR_NOT_FOUND;
			goto RET;
		}

		printf("INFO: %zi Tokens.\n", tokens.count);
		for(size_t i = 0; i < tokens.count; i++) {
			lexer_print_token_to_file(
				file, dynarr_at(&tokens, i), &identifiers
			);
		}
	}

	
RET:
	free(options.srcFile);
	free(options.tokenDumpFile);
	lexer_clean(&lexer);
	lexer_clean_identifiers(&identifiers);
	dynarr_clean(&tokens);
	if(file) fclose(file);
	return err;
}
