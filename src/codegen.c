#include "codegen.h"

#include <string.h>
#include <inttypes.h>

#define INVALID_AST_NODE(err_msg, node_ptr) do {\
	fputs("Error: " err_msg " at ", stderr); \
	lexer_print_debug_to_file(stderr, &(node_ptr)->debug.debug_info); \
	fputc('\n', stderr); \
	*err = ERROR_UNEXPECTED_DATA; \
	goto RET; } while(0);

#define FPRINTF_OR_ERR(file, fmt, ...) \
	do { \
		if(fprintf(file, fmt, __VA_ARGS__) < 0) { \
			fprintf(stderr, "Error: Unable to Write to IR File\n"); \
			*err = ERROR_IO; \
			goto RET; \
		} \
	} while (0)

void codegen_init(CodeGen *cg)
{
	cg->unnamed_counter = 4;
}


const char *integral_type_to_string(size_t id)
{
	switch(id)
	{
	case 0:
		return "i8 ";
	case 1:
		return "void ";
	default:
		return NULL;
	}
}

void gen_decl(
	FILE *output,
	const AstNode *nodes,
	size_t index,
	char *const *identifiers,
	uintmax_t *unnamed_counter,
	Error *err
)
{
	const AstNode *decl = &nodes[index];
	const AstNode *access = &nodes[decl->decl.access];
	const AstNode *ident = &nodes[decl->decl.ident];
	const AstNode *data_type = &nodes[decl->decl.data_type];

	if(data_type->type != AST_FN_TYPE) {
		INVALID_AST_NODE("Expected Function Type", data_type);
	}

	const AstNode *ret_type = &nodes[data_type->fn_type.ret_type];

	if(ret_type->type != AST_IDENT) {
		INVALID_AST_NODE("Expected Type", ret_type);
	}
	
	const char *return_type = integral_type_to_string(ret_type->ident.id);
	if(!return_type) {
		INVALID_AST_NODE("Invalid Return Type", ret_type);
	}

	FPRINTF_OR_ERR(
			output,
			"define external ccc %s @%s(",
			return_type,
			identifiers[ident->ident.id]
	);

	for(size_t i = 0; i < data_type->fn_type.arg_count; i++) {
		const AstNode *arg_ident = &nodes[data_type->fn_type.args[2*i]];
		const AstNode *arg_type = &nodes[data_type->fn_type.args[2*i + 1]];

		if(arg_type->type != AST_IDENT) {
			INVALID_AST_NODE(
				"Illegal Type for Function Parameter", arg_type
			);
		}

		const char *type = integral_type_to_string(arg_type->ident.id);
		if(!type) {
			INVALID_AST_NODE("Expected Type", arg_type);
		}

		FPRINTF_OR_ERR(
			output,
			"%s %s",
			type, identifiers[arg_ident->ident.id]
		);

		if(i != data_type->fn_type.arg_count - 1) {
			FPRINTF_OR_ERR(output, ", ", NULL);
		}
	}
	

	FPRINTF_OR_ERR(
		output, ") !dbg !%ju {\n", *unnamed_counter + 1	
	);
	
	const AstNode *body = &nodes[decl->decl.initial]; // initial == 0 -> module

	if(body->type != AST_BLOCK) {
		INVALID_AST_NODE("Expected Function Body", body);
	}

	for(size_t i = 0; i < body->block.statement_count; i++) {
		const AstNode *statement = &nodes[body->block.statements[i]];

		switch(statement->type) {
		case AST_RET:
			do {} while (0);

			const AstNode *ret_val = &nodes[statement->ret.return_val];

			if(ret_val->type != AST_INT_LIT) {
				INVALID_AST_NODE("Expected Integer Literal", ret_val);
			}

			FPRINTF_OR_ERR(
				output,
				"\tret %s %" PRIdMAX
				", !dbg !DILocation(line: %" PRIu32 ", column: %" PRIu32
				", scope: !%ju)\n",
				return_type, ret_val->int_lit.val,
				ret_val->debug.debug_info.line,
				ret_val->debug.debug_info.col,
				*unnamed_counter + 1
			);

			break;

			
		default:
			INVALID_AST_NODE("Expected Statement", statement);
		}
	}

	FPRINTF_OR_ERR(
		output,
		"}\n !%ju = !DISubroutineType(types: !{"
		"!DIBasicType(name: \"%s\", size: %d, encoding: %s)%s",
		*unnamed_counter,
		identifiers[ret_type->ident.id], 8, "DW_ATE_unsigned", // size and encoding hardcoded
		data_type->fn_type.arg_count ? ", " : ""
	);

	for(size_t i = 0; i < data_type->fn_type.arg_count; i++) {
		const AstNode *arg_type = &nodes[data_type->fn_type.args[2*i + 1]];

		if(arg_type->type != AST_IDENT) {
			INVALID_AST_NODE("Expected Type", arg_type);
		}

		const char *type = integral_type_to_string(arg_type->ident.id);
		if(!type) {
			INVALID_AST_NODE("Expected Type", arg_type);
		}

		FPRINTF_OR_ERR(
			output,
			"!DIBasicType(name: \"%s\", size: %d, encoding: %s)",
			identifiers[arg_type->ident.id], 8, "DW_ATE_unsigned" //size and encoding hardcoded
		);

		if(i < data_type->fn_type.arg_count - 1) {
			FPRINTF_OR_ERR(output, ", ", NULL);
		}
	}

	FPRINTF_OR_ERR(
		output,
		"})\n"
		"!%ju = distinct !DISubprogram(\n"
		"\tname: \"%s\", scope: !1, file: !1, line: %" PRIu32 ",\n"
		"\ttype: !%ju, spFlags: DISPFlagDefinition, \n"
		"\tflags: DIFlagPrototyped, scopeLine: %" PRIu32 ", unit: !0\n)\n",
		*unnamed_counter + 1,
		identifiers[ident->ident.id], decl->debug.debug_info.line,
		*unnamed_counter,
		decl->debug.debug_info.line
	);

	*unnamed_counter += 1;

RET:
	return;	
}

void codegen_gen(
	CodeGen *cg,
	const AstNode *nodes,
	size_t node_count,
	char *const *identifiers,
	const char *dump_path,
	Error *err
)
{
	FILE *output = fopen(dump_path, "w");
	if(!output) goto RET;

	const AstNode *module = &nodes[0];

	FPRINTF_OR_ERR(
		output,
		"source_filename = \"%s\"\n"
		"!0 = distinct !DICompileUnit(\n"
		"\tlanguage: DW_LANG_C, file: !1, producer: \"wyrtc\",\n"
		"\tisOptimized: false, runtimeVersion: 0,\n"
		"\temissionKind: FullDebug, splitDebugInlining: false,\n"
		"\tnameTableKind: None\n"
		")\n"
		"!1 = !DIFile(filename: \"%s\", directory: \".\")\n"
		"!2 = !{i32 7, !\"Dwarf Version\", i32 5}\n"
		"!3 = !{i32 2, !\"Debug Info Version\", i32 3}\n"
		"!llvm.dbg.cu = !{!0}\n"
		"!llvm.module.flags = !{!2, !3}\n",
		module->debug.debug_info.file,
		module->debug.debug_info.file
	);

	for(size_t i = 0; i < module->module.statement_count; i++) {
		size_t index = module->module.statements[i];
		switch(nodes[index].type) {
		case AST_DECL:
			gen_decl(
				output, nodes, index, identifiers,
				&cg->unnamed_counter, err
			);
			if(*err) goto RET;
			break;
		default:
			INVALID_AST_NODE(
				"Illegal File Scope Statement", &nodes[index]
			);
		}
	}
	
RET:
	fclose(output);
	return;
}
