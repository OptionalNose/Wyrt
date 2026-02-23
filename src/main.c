#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "lexer.h"
#include "parser.h"
#include "codegen.h"

#include "../config.h"

typedef struct {
	char *src_file;
	char *token_dump_file; // NULL == Do not dump
	char *ast_dump_file;
	char const *backend_path;
	char *output_file;
	bool do_not_link;
	bool do_not_assemble;
	bool debug;
	int opt_level;
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
	options.backend_path = backends[0].path;

	Error err = ERROR_OK;

	Token *tokens = NULL;
	char **identifiers = NULL;
	size_t identifier_count = 0;
	char **strings = NULL;
	size_t string_count = 0;
	size_t token_count = 0;

	Lexer lexer = { 0 };
	Parser parser = { 0 };
	CodeGen codegen = { 0 };

	for(int i = 1; i < argc; i++) {
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

				"\t-S\t\t\t\t\t\t"
				"Compile only. Do not Assemble or Link.\n"

				"\t-c\t\t\t\t\t\t"
				"Compile and Assemble only. Do not Link\n"

				"\t-o <path>\t\t\t\t\tOutput to <path>\n"

				"\t-g\t\t\t\t\t\tEmit Debug Symbols\n"
				"\t-O<0,1,2,3>\t\t\t\t\tOptimization Level (0 = lowest, 3 = highest)\n"

				"\t--backend-path=<path>\t\t\t\tUse the Backend Dynamic Library at <path>\n"
				"\t--backend=<name>\t\t\t\tUse a pre-configured backend\n"
				"\t\tPossible options are:\n"
				"\t\t(default) "
			);
			for(size_t j = 0; j < (sizeof backends) / sizeof backends[0]; j++) {
				if(j) {
					printf("%s\t\t\t\t\t%s\n\t\t", backends[j].name, backends[j].desc);
				} else {
					printf("%s\t\t\t\t%s\n\t\t", backends[j].name, backends[j].desc);
				}
			}
			fputc('\n', stdout);
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
		} else if(match_arg("-O", argv[i])) {
			if(!sscanf(argv[i], "-O%d", &options.opt_level)) {
				fprintf(stderr, "Expected integer after -O!\n");
				err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
			if(options.opt_level < 0 || options.opt_level > 3) {
				fprintf(
					stderr,
					"Invalid Optimization Level '%d', Optimization Levels are 0-3\n",
					options.opt_level
				);
				err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
		} else if(match_arg("--backend=", argv[i])) {
			bool found = false;
			const char *name = argv[i] + match_arg("--backend=", argv[i]);
			for(size_t j = 0; j < (sizeof backends) / sizeof backends[0]; j++) {
				if(!strcmp(backends[j].name, name)) {
					found = true;
					options.backend_path = backends[j].path;
					break;
				}
			}
			if(!found) {
				fprintf(stderr, "No backend named '%s'!\n", name);
			   	err = ERROR_UNEXPECTED_DATA;
				goto RET;	
			}
		} else if(match_arg("--backend-path=", argv[i])) {
			options.backend_path = argv[i] + match_arg("--backend-path=", argv[i]);
		} else if(sscanf(argv[i], "%*1[^-]%c", &garbage)) {
			options.src_file = argv[i];
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
			fputc('\n', file);
		}
		fclose(file);
	}

	parser_init(&parser, tokens, identifiers, strings);

	parser_parse(&parser, &err);
	if(err) goto RET;

	if(options.ast_dump_file) {
		FILE *file = fopen(options.ast_dump_file, "w");
		if(!file) {
			fprintf(stderr, "Unable to Open AST Dump File.\n");
			err = ERROR_NOT_FOUND;
			goto RET;
		}

		parser_print_ast(&parser, file);
		printf("INFO: %zi AST Nodes.\n", parser.ast.len);
		fclose(file);
	}

	if(!options.output_file) {
#ifdef _WIN32
		options.output_file = "a.exe";
#else
		options.output_file = "a.out";
#endif
	}

	if(options.backend_path) {
		codegen_init(
			&codegen,
			parser.ast.nodes,
			parser.ast.len,
			identifiers,
			strings,
			string_count,
			options.backend_path,
			&err
		);
		if(err) goto RET;

		GenOptions gen_options = 0;
		gen_options += options.do_not_link;
		gen_options += options.do_not_assemble;

		if(options.debug) gen_options += GEN_DBG;

		gen_options += GEN_OPT1 * options.opt_level;

		codegen_gen(
			&codegen,
			gen_options,
			options.output_file,
			&err
		);
		if(err) goto RET;
	}

RET:
	lexer_clean(&lexer);
	lexer_clean_strings(identifiers, identifier_count);
	lexer_clean_strings(strings, string_count);
	parser_clean(&parser);
	if(tokens) free(tokens);
	codegen_clean(&codegen);
	return err;
}
