#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "lexer.h"
#include "parser.h"
#include "codegen.h"

typedef struct {
	char *src_file;
	char *token_dump_file; // NULL == Do not dump
	char *ast_dump_file;
	char *ir_dump_file;
	char *output_file;
	bool compile_only;
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
	CodeGen codegen = { 0 };
	
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
				"\t--ir-dump=<path>\t\t\t\tDump LLVM IR into file <path>\n"
				"\t-c\t\t\t\t\t\tCompile Only, do not link\n"
				"\t-o <path>\t\t\t\t\tOutput to <path>\n"
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
			options.token_dump_file = path;
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
			options.ast_dump_file = path;
			printf("INFO: AST dump to '%s'\n", path);
		} else if(sscanf(argv[i], "--ir-dump=%c", &garbage)) {
			sscanf(argv[i], "--ir-dump=%*s%n", &string_length);
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

			sscanf(argv[i], "--ir-dump=%s", path);
			options.ir_dump_file = path;
		} else if (sscanf(argv[i], "-c%c", &garbage)) {
			options.compile_only = true;
		} else if(sscanf(argv[i], "-o%c", &garbage)) {
			i += 1;
			if(i > argc) {
				fprintf(stderr, "Expected Output File.\n");
				err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			sscanf(argv[i], "%*s%n", &string_length);
			if(string_length == 0) {
				fprintf(stderr, "Expected Output File.\n");
				err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
			
			char *path = malloc(string_length + 1);
			if(!path) {
				err = ERROR_OUT_OF_MEMORY;
				goto RET;
			}

			sscanf(argv[i], "%s", path);
			options.output_file = path;
		} else if(sscanf(argv[i], "%*1[^-]%c", &garbage)) {
			sscanf(argv[i], "%*1[^-]%*s%n", &string_length);
			char *path = malloc(string_length + 1);
			if(!path) {
				err = ERROR_OUT_OF_MEMORY;
				goto RET;
			}
			sscanf(argv[i], "%s", path);
			options.src_file = path;
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

	if(!options.src_file) {
		fprintf(stderr, "No input files.\n");
		err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}


	lexer_init(&lexer, options.src_file, &err);
	if(err) goto RET;

	lexer_tokenize(
		&lexer,
		&tokens, &token_count,
		&identifiers, &identifier_count,
		&err
	);
	if(err) goto RET;

	if(options.token_dump_file) {
		FILE *file = fopen(options.token_dump_file, "w");
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

	parser_gen_ast(
		tokens, token_count, &nodes, &node_count, identifiers, &err
	);
	if(err) goto RET;
	
	if(options.ast_dump_file) {
		FILE *file = fopen(options.ast_dump_file, "w");
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

	char *ir_file;
	char _ir_tmpnam[L_tmpnam];
	if(options.ir_dump_file) {
		ir_file = options.ir_dump_file;
	} else {
		tmpnam(_ir_tmpnam);
		ir_file = _ir_tmpnam;
	}

	codegen_init(&codegen);

	codegen_gen(
		&codegen, nodes, node_count, identifiers, ir_file, &err
	);
	if(err) goto RET;

	char *obj_file;
	char _obj_tmpnam[L_tmpnam];
	if(options.compile_only) {
		obj_file = options.output_file ? options.output_file : "a.o";
	} else {
		tmpnam(_obj_tmpnam);
		obj_file = _obj_tmpnam;
	}

	StringBuilder compile = { 0 };
	string_builder_printf(
		&compile, &err, "clang -c -x ir %s -o %s", ir_file, obj_file
	);
	if(err) goto RET;

	system(compile.str);

	if(!options.ir_dump_file) remove(ir_file);

	
	StringBuilder link = { 0 };
	if(!options.compile_only) {
		char *output_file;
		if(options.output_file) {
			output_file = options.output_file;
		} else {
			output_file = "a.out";
		}
	
		string_builder_printf(
			&link, &err, "clang -o %s %s",
			output_file, obj_file
		);
		if(err) goto RET;

		system(link.str);

		remove(obj_file);
	}
	
RET:
	free(link.str);
	free(compile.str);
	free(options.output_file);
	free(options.src_file);
	free(options.token_dump_file);
	free(options.ast_dump_file);
	free(options.ir_dump_file);
	lexer_clean(&lexer);
	lexer_clean_identifiers(identifiers, identifier_count);
	parser_clean_ast(nodes, node_count);
	free(nodes);
	free(tokens);
	return err;
}
