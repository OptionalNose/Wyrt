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
	bool do_not_link;
	bool do_not_assemble;
	PlatformType platform;
	char *linker;
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
#ifdef WINDOWS
	options.platform = PLATOFRM_WINDOWS;
	options.linker = "ldd";
#else
	options.platform = PLATFORM_LINUX;
	options.linker = "ld";
#endif

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
				"\t-S\t\t\t\t\t\tCompile only. Do not Assemble or Link. Incompatible with --asm-dump, use -o instead\n"
				"\t-c\t\t\t\t\t\tCompile and Assemble only. Do not Link\n"
				"\t-o <path>\t\t\t\t\tOutput to <path>\n"
				"\t--target=<linux, windows>\t\t\tSet Default Build Target. Defaults to Native.\n"
				"\t--linker=<path>\t\t\t\t\tOverride default Linker (GNU's ld)."
				"Note: Linker must accept GCC-style command-line flags.\n"
			);
			fflush(stdout);
			goto RET;
		} else if(match_arg("--token-dump=", argv[i])) {
			options.token_dump_file = match_arg("--token-dump=", argv[i]) + argv[i];
		} else if(match_arg("--ast-dump=", argv[i])) {
			options.ast_dump_file = match_arg("--ast-dump=", argv[i]) + argv[i];
		} else if(match_arg("--asm-dump=", argv[i])) {
			options.asm_dump_file = match_arg("--asm-dump=", argv[i]) + argv[i];
		} else if(match_arg("--linker=", argv[i])) {
			options.linker = match_arg("--linker=", argv[i]) + argv[i];
		} else if(match_arg("--target=", argv[i])) {
			if(match_arg("--target=linux", argv[i])) {
				options.platform = PLATFORM_LINUX;
			} else if(match_arg("--target=windows", argv[i])) {
				options.platform = PLATFORM_WINDOWS;
			} else {
				fprintf(stderr, "Invalid Target.\n");
				err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
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

	if(options.do_not_assemble && options.asm_dump_file) {
		fprintf(stderr, "Cannot use -S and --asm-dump simultaneously. Use -o instead.\n");
		err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

#ifndef WINDOWS
	if(options.platform == PLATFORM_WINDOWS) {
		if(strcmp(options.linker, "ld") == 0) {
			options.linker = "x86_64-w64-mingw32-ld";
		}
	}
#endif
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
	} else if(options.do_not_assemble) {
		asm_file_path = options.output_file ? options.output_file : "a.asm";
	} else {
		tmpnam(_asm_tmpnam);
		asm_file_path = _asm_tmpnam;
	}
	
	asm_file = fopen(asm_file_path, "wb");
	if(!asm_file) {
		fprintf(stderr, "Error Creating Assembly File!\n");
		err = ERROR_IO;
		goto RET;
	}

	codegen_init(&codegen, asm_file, options.platform, nodes, node_count, identifiers);

	codegen_gen(&codegen, !options.do_not_link, &err);
	if(err) goto RET;

	fclose(asm_file);
	asm_file = NULL;

	if(!options.do_not_assemble) {
		char *obj_file;
		char _obj_tmpnam[L_tmpnam];
		if(options.do_not_link) {
			obj_file = options.output_file ? options.output_file : "a.o";
		} else {
			tmpnam(_obj_tmpnam);
			obj_file = _obj_tmpnam;
		}

		string_builder_printf(
			&compile, &err, "nasm %s -f%s -o %s",
			asm_file_path,
			(options.platform == PLATFORM_WINDOWS) ? "win64" : "elf64",
			obj_file
		);
		if(err) goto RET;

		system(compile.str);

		if(!options.asm_dump_file) remove(asm_file_path);
		
		if(!options.do_not_link) {
			char *output_file;
			if(options.output_file) {
				output_file = options.output_file;
			} else {
				output_file = "a.out";
			}
		
			string_builder_printf(
				&link, &err, "%s -o %s %s %s",
				options.linker,
				output_file,
				obj_file,
				(options.platform == PLATFORM_WINDOWS) ? "-lkernel32" : ""
			);
			if(err) goto RET;

			system(link.str);

			remove(obj_file);
		}
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
	codegen_clean(&codegen);
	return err;
}
