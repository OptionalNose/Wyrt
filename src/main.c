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
	char *asm_dump_file;
	char *output_file;
	bool compile_only;
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
	size_t token_count = 0;

	AstNode *nodes = NULL;
	size_t node_count = 0;

	Lexer lexer = { 0 };
	CodeGen codegen = { 0 };
	
	StringBuilder compile = { 0 };
	StringBuilder link = { 0 };

	FILE *asm_file = NULL;

	for(int i = 1; i < argc; i++) {
		int string_length = 0;
		char garbage;
		if(match_arg("--help", argv[i]) || match_arg("-h", argv[i])) {
			printf(
				"Usage: wyrt [OPTIONS] file\n"
				"\n"
				"Options:\n"
				"\t--token-dump=<path>\t\t\t\tDump Lexed Tokens into file <path>\n"
				"\t--ast-dump=<path>\t\t\t\tDump Parsed AST Nodes into file <path>\n"
				"\t--asm-dump=<path>\t\t\t\tDump Assembly into file <path>\n"
				"\t-c\t\t\t\t\t\tCompile Only, do not link\n"
				"\t-o <path>\t\t\t\t\tOutput to <path>\n"
			);
			fflush(stdout);
			goto RET;
		} else if(match_arg("--token-dump=", argv[i])) {
			options.token_dump_file = match_arg("--token-dump=", argv[i]) + argv[i];
		} else if(match_arg("--ast-dump=", argv[i])) {
			options.ast_dump_file = match_arg("--ast-dump=", argv[i]) + argv[i];
		} else if(match_arg("--asm-dump=", argv[i])) {
			options.asm_dump_file = match_arg("--asm-dump=", argv[i]) + argv[i];
		} else if(match_arg("-c", argv[i])) {
			options.compile_only = true;
		} else if(match_arg("-o", argv[i])) {
			if(i + 1 >= argc) {
				fprintf(stderr, "No output file provided to '-o'.\n");
				err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
			options.output_file = argv[i + 1];
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

		fclose(file);
	}

	char *asm_file_path;
	char _asm_tmpnam[L_tmpnam];
	if(options.asm_dump_file) {
		asm_file_path = options.asm_dump_file;
	} else {
		tmpnam(_asm_tmpnam);
		asm_file_path = _asm_tmpnam;
	}
	
	asm_file = fopen(asm_file_path, "wb");
	if(!asm_file) {
		fprintf(stderr, "Error Creating File for LLVM IR!\n");
		err = ERROR_IO;
		goto RET;
	}

	codegen_init(&codegen, asm_file, nodes, node_count, identifiers);

	codegen_gen(&codegen, !options.compile_only, &err);
	if(err) goto RET;

	fclose(asm_file);
	asm_file = NULL;

	char *obj_file;
	char _obj_tmpnam[L_tmpnam];
	if(options.compile_only) {
		obj_file = options.output_file ? options.output_file : "a.o";
	} else {
		tmpnam(_obj_tmpnam);
		obj_file = _obj_tmpnam;
	}

	string_builder_printf(
		&compile, &err, "nasm %s -felf64 -o %s",
		asm_file_path, obj_file
	);
	if(err) goto RET;

	system(compile.str);

	if(!options.asm_dump_file) remove(asm_file_path);
	
	if(!options.compile_only) {
		char *output_file;
		if(options.output_file) {
			output_file = options.output_file;
		} else {
			output_file = "a.out";
		}
	
		string_builder_printf(
			&link, &err, "ld -o %s %s",
			output_file, obj_file
		);
		if(err) goto RET;

		system(link.str);

		remove(obj_file);
	}
	
RET:
	if(options.src_file) free(options.src_file);
	if(asm_file) fclose(asm_file);
	if(link.str) free(link.str);
	if(compile.str) free(compile.str);
	lexer_clean(&lexer);
	lexer_clean_identifiers(identifiers, identifier_count);
	if(nodes) parser_clean_ast(nodes, node_count);
	if(nodes) free(nodes);
	if(tokens) free(tokens);
	return err;
}
