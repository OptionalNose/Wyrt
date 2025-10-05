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
	bool do_not_link;
	bool do_not_assemble;
	bool debug;
} CmdlineOptions;

int match_arg(const char *query, const char *arg)
{
	int i = 0;
	while(*query && *arg) {
		if(*query != *arg) return 0;
		query += 1;
		arg += 1;
		i += 1;
	}
	return i;
}

int main(int argc, char **argv)
{
	CmdlineOptions options = { 0 };

	Error err = ERROR_OK;

	Token *tokens = NULL;
	char **identifiers = NULL;
	size_t identifier_count = 0;
	char **strings = NULL;
	size_t string_count = 0;
	size_t token_count = 0;

	AstNode *nodes = NULL;
	size_t node_count = 0;

	Lexer lexer = { 0 };
	CodeGen codegen = { 0 };

	for(int i = 1; i < argc; i++) {
		int string_length = 0;
		char garbage;
		if(match_arg("--help", argv[i]) || match_arg("-h", argv[i])) {
			printf(
				"Usage: wyrt [OPTIONS] file\n"
				"\n"

				"Options:\n"
				"\t--token-dump=<path>\t\t\t\t"
				"Dump Lexed Tokens into file <path>\n"

				"\t--ast-dump=<path>\t\t\t\t"
				"Dump Parsed AST Nodes into file <path>\n"

				"\t--ir-dump=<path>\t\t\t\t"
				"Dump GCC IR into file <path>\n"

				"\t-S\t\t\t\t\t\t"
				"Compile only. Do not Assemble or Link."

				"\t-c\t\t\t\t\t\t"
				"Compile and Assemble only. Do not Link\n"

				"\t-o <path>\t\t\t\t\tOutput to <path>\n"

				"\t-g\t\t\t\t\t\tEmit Debug Symbols\n"
				"\t-O<0,1,2,3>\t\t\t\t\tOptimization Level (0 = lowest, 3 = highest)\n"
			);
			fflush(stdout);
			goto RET;
		} else if(match_arg("--token-dump=", argv[i])) {
			options.token_dump_file = match_arg(
				"--token-dump=",
				argv[i]
			) + argv[i];
		} else if(match_arg("--ast-dump=", argv[i])) {
			options.ast_dump_file = match_arg(
				"--ast-dump=",
				argv[i]
			) + argv[i];
		} else if(match_arg("--ir-dump=", argv[i])) {
			options.ir_dump_file = match_arg(
				"--ir-dump=",
				argv[i]
			) + argv[i];
		} else if(match_arg("-S", argv[i])) {
			options.do_not_assemble = true;
			options.do_not_link = true;
		} else if(match_arg("-c", argv[i])) {
			options.do_not_link = true;
		} else if(match_arg("-o", argv[i])) {
			if(i + 1 >= argc) {
				fprintf(stderr, "No output file provided to '-o'.\n");
				err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
			options.output_file = argv[i + 1];
			i += 1;
		} else if(match_arg("-g", argv[i])) {
			options.debug = true;
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

	if(options.do_not_assemble && options.ir_dump_file) {
		fprintf(
			stderr,
			"Cannot use -S and --ir-dump simultaneously. Use -o instead.\n"
		);
		err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

	lexer_init(&lexer, options.src_file, &err);
	if(err) goto RET;

	lexer_tokenize(
		&lexer,
		&tokens, &token_count,
		&identifiers, &identifier_count,
		&strings, &string_count,
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
				file,
				&tokens[i],
				identifiers,
				strings
			);
		}
		fclose(file);
	}

	parser_gen_ast(
		tokens, token_count, &nodes, &node_count, identifiers, strings, &err
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
			nodes,
			node_count,
			identifiers,
			strings
		);

		fclose(file);
	}

	if(!options.output_file) {
#ifdef _WIN32
		options.output_file = "a.exe";
#else
		options.output_file = "a.out";
#endif
	}

	codegen_init(
		&codegen,
		nodes,
		node_count,
		identifiers,
		strings,
		string_count
	);

	//TODO: Optimization and Debug
	codegen_gen(
		&codegen,
		options.do_not_link ? (options.do_not_assemble ? GEN_ASM : GEN_OBJ) : GEN_EXE,
		options.output_file,
		options.ir_dump_file,
		&err
	);
	if(err) goto RET;

RET:
	if(options.src_file) free(options.src_file);
	if(options.token_dump_file) free(options.src_file);
	if(options.ast_dump_file) free(options.src_file);
	if(options.ir_dump_file) free(options.src_file);
	lexer_clean(&lexer);
	lexer_clean_strings(identifiers, identifier_count);
	lexer_clean_strings(strings, string_count);
	if(nodes) parser_clean_ast(nodes, node_count);
	if(nodes) free(nodes);
	if(tokens) free(tokens);
	codegen_clean(&codegen);
	return err;
}
