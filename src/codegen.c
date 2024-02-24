#include "codegen.h"

#include <string.h>
#include <inttypes.h>

#define INVALID_AST_NODE(err_msg, node_ptr) do {\
	fputs("Error: " err_msg " at ", stderr); \
	lexer_print_debug_to_file(stderr, &(node_ptr)->debug.debug_info); \
	fputc('\n', stderr); \
	*err = ERROR_UNEXPECTED_DATA; \
	goto RET; } while(0);

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
	StringBuilder *ir_builder,
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

	string_builder_printf(
		ir_builder,
		err,
		"define external ccc %s @%s(",
		return_type,
		identifiers[ident->ident.id]
	);
	if(*err) goto RET;

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

		string_builder_append_va(
			ir_builder,
			err,
			type,
			identifiers[arg_ident->ident.id],
			NULL
		);
		if(*err) goto RET;

		if(i != data_type->fn_type.arg_count - 1) {
			string_builder_append(ir_builder, ", ", err);
			if(*err) goto RET;	
		}
	}
	
	
	string_builder_printf(
		ir_builder, err, ") !dbg !%d {\n", *unnamed_counter + 1
	);
	if(*err) goto RET;
	
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

			string_builder_printf(
				ir_builder,
				err,
				"\tret %s %" PRIdMAX
				", !dbg !DILocation(line: %" PRIu32 ", column: %" PRIu32
				", scope: !%d)\n",
				return_type, ret_val->int_lit.val,
				ret_val->debug.debug_info.line,
				ret_val->debug.debug_info.col,
				*unnamed_counter + 1
			);
			if(*err) goto RET;
			break;

			
		default:
			INVALID_AST_NODE("Expected Statement", statement);
		}
	}

	string_builder_append(
		ir_builder, "}\n", err
	);
	if(*err) goto RET;

	string_builder_printf(
		ir_builder, err,
		"!%" PRIuMAX " = !DISubroutineType(types: !{",
		*unnamed_counter
	);
	if(*err) goto RET;

	string_builder_printf(
		ir_builder, err,
		"!DIBasicType(name: \"%s\", size: %d, encoding: %s)%s",
		identifiers[ret_type->ident.id], 8, "DW_ATE_unsigned", // size and encoding hardcoded
		data_type->fn_type.arg_count ? ", " : ""
	);
	if(*err) goto RET;

	for(size_t i = 0; i < data_type->fn_type.arg_count; i++) {
		const AstNode *arg_type = &nodes[data_type->fn_type.args[2*i + 1]];

		if(arg_type->type != AST_IDENT) {
			INVALID_AST_NODE("Expected Type", arg_type);
		}

		const char *type = integral_type_to_string(arg_type->ident.id);
		if(!type) {
			INVALID_AST_NODE("Expected Type", arg_type);
		}

		string_builder_printf(
			ir_builder, err,
			"!DIBasicType(name: \"%s\", size: %d, encoding: %s)",
			identifiers[arg_type->ident.id], 8, "DW_ATE_unsigned" //size and encoding hardcoded
		);
		if(*err) goto RET;

		if(i < data_type->fn_type.arg_count - 1) {
			string_builder_append(ir_builder, ", ", err);
			if(*err) goto RET;
		}
	}

	string_builder_append(ir_builder, "})\n", err);
	if(*err) goto RET;

	*unnamed_counter += 1;
	
	string_builder_printf(
		ir_builder, err,
		"!%d = distinct !DISubprogram(\n"
		"\tname: \"%s\", scope: !1, file: !1, line: %" PRIu32 ",\n"
		"\ttype: !%" PRIuMAX ", spFlags: DISPFlagDefinition, \n"
		"\tflags: DIFlagPrototyped, scopeLine: %" PRIu32 ", unit: !0\n)\n",
		*unnamed_counter,
		identifiers[ident->ident.id], decl->debug.debug_info.line,
		*unnamed_counter - 1,
		decl->debug.debug_info.line
	);
	if(*err) goto RET;

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
	StringBuilder ir_builder = { 0 };

	const AstNode *module = &nodes[0];

	string_builder_append_va(
		&ir_builder,
		err,
		"source_filename = \"",
		module->debug.debug_info.file, "\"\n"
		"!0 = distinct !DICompileUnit(\n"
		"\tlanguage: DW_LANG_C, file: !1, producer: \"wyrtc\",\n"
		"\tisOptimized: false, runtimeVersion: 0,\n"
		"\temissionKind: FullDebug, splitDebugInlining: false,\n"
		"\tnameTableKind: None\n"
		")\n"
		"!1 = !DIFile(filename: \"",
		module->debug.debug_info.file, "\""
		", directory: \".\")\n"
		"!2 = !{i32 7, !\"Dwarf Version\", i32 5}\n"
		"!3 = !{i32 2, !\"Debug Info Version\", i32 3}\n"
		"!llvm.dbg.cu = !{!0}\n"
		"!llvm.module.flags = !{!2, !3}\n",
		NULL
	);
	if(*err) goto RET;

	for(size_t i = 0; i < module->module.statement_count; i++) {
		size_t index = module->module.statements[i];
		switch(nodes[index].type) {
		case AST_DECL:
			gen_decl(
				&ir_builder, nodes, index, identifiers,
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
	
	FILE *dump = fopen(dump_path, "w");
	if(!dump) {
		*err = ERROR_IO;
		goto RET;
	}
	fwrite(ir_builder.str, 1, ir_builder.count - 1, dump);
	fclose(dump);
	
RET:
	free(ir_builder.str);
	return;
}
