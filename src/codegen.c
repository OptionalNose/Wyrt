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



typedef union {
	enum {
		TYPE_INVALID,
		TYPE_PRIMITIVE_U8,
		TYPE_PRIMITIVE_VOID,
	} type;
} Type;



void codegen_init(
		CodeGen *cg,
		FILE *output,
		const AstNode *nodes,
		size_t node_count,
		char *const *identifiers
)
{
	*cg = (CodeGen) {
		.output = output,
		.nodes = nodes,
		.node_count = node_count,
		.identifiers = identifiers,
		.md_counter = 4,
		.ssa_counter = 0,
	};
}

Type identifier_to_type(size_t ident_id)
{
	switch(ident_id) {
	case 0:
		return (Type) {.type = TYPE_PRIMITIVE_U8};
	case 1:
		return (Type) {.type = TYPE_PRIMITIVE_VOID};
	default:
		return (Type) {.type = TYPE_INVALID};
	} 
}


const char *type_to_string(Type type)
{
	switch(type.type)
	{
	case TYPE_INVALID:
		return NULL;
	case TYPE_PRIMITIVE_U8:
		return "i8";
	case TYPE_PRIMITIVE_VOID:
		return "void";
	}
}

void gen_expr(CodeGen *cg, size_t index, size_t dbg_current_scope, Type expected, Error *err)
{
	const AstNode *expr = &cg->nodes[index];

	switch(expr->type) {
	case AST_INT_LIT:
		FPRINTF_OR_ERR(
			cg->output, 
			"%%%ju = add %s 0, %ji"
			", !dbg !DILocation(line: %" PRIu32 ", column: %" PRIu32
			", scope: !%ju)\n",
			++cg->ssa_counter, type_to_string(expected), expr->int_lit.val,
			expr->debug.debug_info.line,
			expr->debug.debug_info.col,
			dbg_current_scope
		);
		break;

	case AST_MUL:
		do {
			gen_expr(cg, expr->binop.lhs, dbg_current_scope, expected, err);
			if(*err) goto RET;

			const size_t lhs = cg->ssa_counter;

			gen_expr(cg, expr->binop.rhs, dbg_current_scope, expected, err);
			if(*err) goto RET;

			const size_t rhs = cg->ssa_counter;

			FPRINTF_OR_ERR(
				cg->output, 
				"%%%ju = mul %s %%%ju, %%%ju"
				", !dbg !DILocation(line: %" PRIu32 ", column: %" PRIu32
				", scope: !%ju)\n",
				++cg->ssa_counter, type_to_string(expected), lhs, rhs,
				expr->debug.debug_info.line,
				expr->debug.debug_info.col,
				dbg_current_scope
			);
		} while(0);
		break;

	case AST_DIV:
		do {
			gen_expr(cg, expr->binop.lhs, dbg_current_scope, expected, err);
			if(*err) goto RET;

			const size_t lhs = cg->ssa_counter;

			gen_expr(cg, expr->binop.rhs, dbg_current_scope, expected, err);
			if(*err) goto RET;

			const size_t rhs = cg->ssa_counter;

			FPRINTF_OR_ERR( //TODO: sdiv for signed types
				cg->output, 
				"%%%ju = udiv %s %%%ju, %%%ju"
				", !dbg !DILocation(line: %" PRIu32 ", column: %" PRIu32
				", scope: !%ju)\n",
				++cg->ssa_counter, type_to_string(expected), lhs, rhs,
				expr->debug.debug_info.line,
				expr->debug.debug_info.col,
				dbg_current_scope
			);
		} while(0);
		break;

	case AST_ADD:
		do {
			gen_expr(cg, expr->binop.lhs, dbg_current_scope, expected, err);
			if(*err) goto RET;

			const size_t lhs = cg->ssa_counter;

			gen_expr(cg, expr->binop.rhs, dbg_current_scope, expected, err);
			if(*err) goto RET;

			const size_t rhs = cg->ssa_counter;

			FPRINTF_OR_ERR( 
				cg->output, 
				"%%%ju = add %s %%%ju, %%%ju"
				", !dbg !DILocation(line: %" PRIu32 ", column: %" PRIu32
				", scope: !%ju)\n",
				++cg->ssa_counter, type_to_string(expected), lhs, rhs,
				expr->debug.debug_info.line,
				expr->debug.debug_info.col,
				dbg_current_scope
			);
		} while(0);
		break;

	case AST_SUB:
		do {
			gen_expr(cg, expr->binop.lhs, dbg_current_scope, expected, err);
			if(*err) goto RET;

			const size_t lhs = cg->ssa_counter;

			gen_expr(cg, expr->binop.rhs, dbg_current_scope, expected, err);
			if(*err) goto RET;

			const size_t rhs = cg->ssa_counter;

			FPRINTF_OR_ERR( 
				cg->output, 
				"%%%ju = sub %s %%%ju, %%%ju"
				", !dbg !DILocation(line: %" PRIu32 ", column: %" PRIu32
				", scope: !%ju)\n",
				++cg->ssa_counter, type_to_string(expected), lhs, rhs,
				expr->debug.debug_info.line,
				expr->debug.debug_info.col,
				dbg_current_scope
			);
		} while(0);
		break;

	default:
		INVALID_AST_NODE("Expected Expression", expr);
	}

RET:
	return;
}

void gen_fn_def(CodeGen *cg, size_t index, Error *err)
{
	const AstNode *fn_def = &cg->nodes[index];
	const AstNode *ident = &cg->nodes[fn_def->fn_def.ident];
	const AstNode *fn_type = &cg->nodes[fn_def->fn_def.fn_type];

	if(fn_type->type != AST_FN_TYPE) {
		INVALID_AST_NODE("Expected Function Type", fn_type);
	}

	const AstNode *ret_type = &cg->nodes[fn_type->fn_type.ret_type];

	if(ret_type->type != AST_IDENT) {
		INVALID_AST_NODE("Expected Type", ret_type);
	}
	

	const Type return_type = identifier_to_type(ret_type->ident.id);

	if(return_type.type == TYPE_INVALID) {
		INVALID_AST_NODE("Invalid Return Type", ret_type);
	}

	const char *return_type_str = type_to_string(return_type);

	FPRINTF_OR_ERR(
			cg->output,
			"define external ccc %s @%s(",
			return_type_str,
			cg->identifiers[ident->ident.id]
	);

	for(size_t i = 0; i < fn_type->fn_type.arg_count; i++) {
		const AstNode *arg_ident = &cg->nodes[fn_type->fn_type.args[2*i]];
		const AstNode *arg_type = &cg->nodes[fn_type->fn_type.args[2*i + 1]];

		if(arg_type->type != AST_IDENT) {
			INVALID_AST_NODE(
				"Illegal Type for Function Parameter", arg_type
			);
		}

		const char *type = type_to_string(identifier_to_type(arg_type->ident.id));
		if(!type) {
			INVALID_AST_NODE("Expected Type", arg_type);
		}

		FPRINTF_OR_ERR(
			cg->output,
			"%s %s",
			type, cg->identifiers[arg_ident->ident.id]
		);

		if(i != fn_type->fn_type.arg_count - 1) {
			FPRINTF_OR_ERR(cg->output, ", ", NULL);
		}
	}
	
	const size_t scope_dbg = ++cg->md_counter;

	FPRINTF_OR_ERR(
		cg->output, ") !dbg !%ju {\n", scope_dbg	
	);

	const AstNode *block = &cg->nodes[fn_def->fn_def.block];

	if(block->type != AST_BLOCK) {
		INVALID_AST_NODE("Expected Function Body", block);
	}

	for(size_t i = 0; i < block->block.statement_count; i++) {
		const AstNode *statement = &cg->nodes[block->block.statements[i]];

		switch(statement->type) {
		case AST_RET:
			gen_expr(cg, statement->ret.return_val, scope_dbg, return_type, err);
			if(*err) goto RET;

			FPRINTF_OR_ERR(
				cg->output,
				"\tret %s %%%ju"
				", !dbg !DILocation(line: %" PRIu32 ", column: %" PRIu32
				", scope: !%ju)\n",
				return_type_str, cg->ssa_counter,
				statement->debug.debug_info.line,
				statement->debug.debug_info.col,
				scope_dbg
			);
			break;
		default:
			INVALID_AST_NODE("Expected Statement", statement);
		}
	}

	const size_t fn_type_dbg = ++cg->md_counter;
	FPRINTF_OR_ERR(
		cg->output,
		"}\n !%ju = !DISubroutineType(types: !{"
		"!DIBasicType(name: \"%s\", size: %d, encoding: %s)%s",
		fn_type_dbg,
		cg->identifiers[ret_type->ident.id], 8, "DW_ATE_unsigned", // size and encoding hardcoded
		fn_type->fn_type.arg_count ? ", " : ""
	);

	for(size_t i = 0; i < fn_type->fn_type.arg_count; i++) {
		const AstNode *arg_type = &cg->nodes[fn_type->fn_type.args[2*i + 1]];

		if(arg_type->type != AST_IDENT) {
			INVALID_AST_NODE("Expected Type", arg_type);
		}

		const char *type = type_to_string(identifier_to_type(arg_type->ident.id));
		if(!type) {
			INVALID_AST_NODE("Expected Type", arg_type);
		}

		FPRINTF_OR_ERR(
			cg->output,
			"!DIBasicType(name: \"%s\", size: %d, encoding: %s)",
			cg->identifiers[arg_type->ident.id], 8, "DW_ATE_unsigned" //size and encoding hardcoded
		);

		if(i < fn_type->fn_type.arg_count - 1) {
			FPRINTF_OR_ERR(cg->output, ", ", NULL);
		}
	}

	FPRINTF_OR_ERR(
		cg->output,
		"})\n"
		"!%ju = distinct !DISubprogram(\n"
		"\tname: \"%s\", scope: !1, file: !1, line: %" PRIu32 ",\n"
		"\ttype: !%ju, spFlags: DISPFlagDefinition, \n"
		"\tflags: DIFlagPrototyped, scopeLine: %" PRIu32 ", unit: !0\n)\n",
		scope_dbg,
		cg->identifiers[ident->ident.id], fn_def->debug.debug_info.line,
		fn_type_dbg,
		fn_def->debug.debug_info.line
	);

RET:
	return;	
}

void codegen_gen(CodeGen *cg, Error *err)
{
	const AstNode *module = &cg->nodes[0];

	FPRINTF_OR_ERR(
		cg->output,
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
		switch(cg->nodes[index].type) {
		case AST_FN_DEF:
			gen_fn_def(cg, index, err);
			if(*err) goto RET;
			break;
		default:
			INVALID_AST_NODE(
				"Illegal File Scope Statement", &cg->nodes[index]
			);
		}
	}
	
RET:
	return;
}
