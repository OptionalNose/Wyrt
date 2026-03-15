#include "parser.h"

#include "ui.h"

#include <assert.h>

void nodelist_alloc(NodeList *list, size_t n, Error *err)
{
	list->len += n;
	if(list->len > list->cap) {
		do {
			if(!list->cap) list->cap = 1;
			list->cap *= 2;
		} while(list->len > list->cap);
		void *ptr = realloc(list->nodes, sizeof(AstNode) * list->cap);
		CHECK_MALLOC(ptr);
		list->nodes = ptr; 
	}

RET:
	return;
}

void nodelist_push(NodeList *list, AstNode node, Error *err)
{
	nodelist_alloc(list, 1, err);
	if(*err) goto RET;
	list->nodes[list->len - 1] = node;
RET:
	return;
}

AstNode nodelist_pop(NodeList *list)
{
	return list->nodes[--list->len];
}

void parsestack_alloc(ParseStack *ps, size_t n, Error *err)
{
	ps->len += n;
	if(ps->len > ps->cap) {
		do {
			if(!ps->cap) ps->cap = 1;
			ps->cap *= 2;
		} while(ps->len > ps->cap);
		void *ptr = realloc(ps->state, sizeof(AstNode) * ps->cap);
		CHECK_MALLOC(ptr);
		ps->state = ptr; 
	}

RET:
	return;
}

void parsestack_push(ParseStack *ps, ParseState state, Error *err)
{
	parsestack_alloc(ps, 1, err);
	if(*err) goto RET;
	ps->state[ps->len - 1] = state;
RET:
	return;
}

ParseState parsestack_pop(ParseStack *ps)
{
	return ps->state[--ps->len];
}

ParseState *parsestack_top(ParseStack *ps)
{
	return &ps->state[ps->len - 1];
}

ParseState *parsestack_from_top(ParseStack *ps, size_t i)
{
	return &ps->state[ps->len - 1 - i];
}

void parser_init(
	Parser *prs,
	Token const *tokens,
	char *const *identifiers,
	char *const *strings
) {
	*prs = (Parser) {
		.tokens = tokens,
		.identifiers = identifiers,
		.strings = strings,
		.ast = {0},
	};
}

void parser_clean(Parser *prs)
{
	nodelist_clean(&prs->ast);
	free(prs->parse_stack.state);
}

void nodelist_clean(NodeList *list)
{
	for(size_t i = 0; i < list->len; i++) {
		if(list->nodes[i].type > AST_IF) {
			fprintf(stderr, "Corrupted AST\n");
			continue;
		} 

		switch(list->nodes[i].type) {
		case AST_FN_TYPE:
			free(list->nodes[i].fn_type.args);
			break;

		case AST_BLOCK:
			free(list->nodes[i].block.statements);
			break;

		case AST_MODULE:
			free(list->nodes[i].module.statements);
			break;

		case AST_FN_CALL:
			free(list->nodes[i].fn_call.args);
			break;

		case AST_ARRAY_LIT:
			free(list->nodes[i].array_lit.elems);
			break;

		case AST_STRUCT_TYPE:
			free(list->nodes[i].struct_type.member_name_ids);
			free(list->nodes[i].struct_type.member_types);
			break;

		case AST_STRUCT_LIT:
			free(list->nodes[i].struct_lit.member_name_ids);
			free(list->nodes[i].struct_lit.member_values);
			break;

		case AST_NONE:
		case AST_FN_DEF:
		case AST_IDENT:
		case AST_RET:
		case AST_INT_LIT:
		case AST_MUL:
		case AST_DIV:
		case AST_ADD:
		case AST_SUB:
		case AST_VAR_DECL:
		case AST_ASSIGN:
		case AST_ADD_ASSIGN:
		case AST_SUB_ASSIGN:
		case AST_MUL_ASSIGN:
		case AST_DIV_ASSIGN:
		case AST_ADDR:
		case AST_POINTER_CONST:
		case AST_POINTER_VAR:
		case AST_POINTER_ABYSS:
		case AST_DEREF:
		case AST_ARRAY:
		case AST_SLICE_CONST:
		case AST_SLICE_VAR:
		case AST_SLICE_ABYSS:
		case AST_SUBSCRIPT:
		case AST_STRUCT_ACCESS:
		case AST_EXTERN:
		case AST_STRING_LIT:
		case AST_ZSTRING_LIT:
		case AST_CSTRING_LIT:
		case AST_DISCARD:
		case AST_ARROW:
		case AST_TYPEDEF:
		case AST_CHAR_LIT:
		case AST_COMP_EQ:
		case AST_COMP_GE:
		case AST_COMP_LE:
		case AST_COMP_NE:
		case AST_COMP_GT:
		case AST_COMP_LT:
		case AST_LOGIC_AND:
		case AST_LOGIC_OR:
		case AST_LOGIC_NOT:
		case AST_IF:
			break;
		}
	}
	free(list->nodes);
}

void parser_print_ast(Parser *prs, FILE *file)
{
	for(size_t i = 0; i < prs->ast.len; i++) {
		fprintf(file, "%zi:\t", i);
		switch(prs->ast.nodes[i].type) {
		case AST_NONE:
			fprintf(file, "NONE");
			break;
			
		case AST_FN_DEF:
			fprintf(
				file,
				"FN_DEF '%zi': %zi { %zi }",
				prs->ast.nodes[i].fn_def.ident,
				prs->ast.nodes[i].fn_def.fn_type,
				prs->ast.nodes[i].fn_def.block
			);
			break;

		case AST_FN_TYPE:
			fprintf(file, "(");
			for(size_t j = 0; j < prs->ast.nodes[i].fn_type.arg_count; j++) {
				fprintf(
					file,
					"%zi: %zi, ",
					prs->ast.nodes[i].fn_type.args[2 * j], prs->ast.nodes[i].fn_type.args[2 * j + 1]
				);
			}
			fprintf(file, ") -> %zi", prs->ast.nodes[i].fn_type.ret_type);
			break;
			
		case AST_BLOCK:
			fprintf(file, "{");
			for(size_t j = 0; j < prs->ast.nodes[i].block.statement_count; j++) {
				fprintf(
					file,
					"%zi, ",
					prs->ast.nodes[i].block.statements[j]
				);
			}
			fprintf(file, "}");
			break;
			
		case AST_RET:
			fprintf(file, "return %zi", prs->ast.nodes[i].ret.return_val);
			break;
			
		case AST_INT_LIT:
			fprintf(file, "%ji", prs->ast.nodes[i].int_lit.val);
			break;
			
		case AST_IDENT:
			fprintf(file, "'%s'", prs->identifiers[prs->ast.nodes[i].ident.id]);
			break;  
			
		case AST_MODULE:
			fprintf(file, "MODULE[");
			for(size_t j = 0; j < prs->ast.nodes[i].module.statement_count; j++) {
				fprintf(
					file,
					"%zi, ",
					prs->ast.nodes[i].module.statements[j]
				);
			}
			fprintf(file, "]");
			break;
		case AST_MUL:
			fprintf(file, "%zi * %zi", prs->ast.nodes[i].binop.lhs, prs->ast.nodes[i].binop.rhs);
			break;
		case AST_ADD:
			fprintf(file, "%zi + %zi", prs->ast.nodes[i].binop.lhs, prs->ast.nodes[i].binop.rhs);
			break;
		case AST_DIV:
			fprintf(file, "%zi / %zi", prs->ast.nodes[i].binop.lhs, prs->ast.nodes[i].binop.rhs);
			break;
		case AST_SUB:
			fprintf(file, "%zi - %zi", prs->ast.nodes[i].binop.lhs, prs->ast.nodes[i].binop.rhs);
			break;

		case AST_VAR_DECL:
			fprintf(
				file,
				"%s '%s': %ji [= %ji]", 
				prs->ast.nodes[i].var_decl.mut ? "var" : "const",
				prs->identifiers[prs->ast.nodes[i].var_decl.id],
				prs->ast.nodes[i].var_decl.data_type,
				prs->ast.nodes[i].var_decl.initial
			);
			break;

		case AST_FN_CALL:
			fprintf(file, "%s(", prs->identifiers[prs->ast.nodes[i].fn_call.fn_id]);
			for(size_t j = 0; j < prs->ast.nodes[i].fn_call.arg_count; j++) {
				fprintf(file, "%ji, ", prs->ast.nodes[i].fn_call.args[j]);
			}
			fprintf(file, ")");
			break;

		case AST_ASSIGN:
			fprintf(file, "%zi = %zi", prs->ast.nodes[i].assign.var, prs->ast.nodes[i].assign.expr);
			break;

		case AST_ADD_ASSIGN:
			fprintf(file, "%zi += %zi", prs->ast.nodes[i].assign.var, prs->ast.nodes[i].assign.expr);
			break;

		case AST_SUB_ASSIGN:
			fprintf(file, "%zi -= %zi", prs->ast.nodes[i].assign.var, prs->ast.nodes[i].assign.expr);
			break;

		case AST_MUL_ASSIGN:
			fprintf(file, "%zi *= %zi", prs->ast.nodes[i].assign.var, prs->ast.nodes[i].assign.expr);
			break;

		case AST_DIV_ASSIGN:
			fprintf(file, "%zi /= %zi", prs->ast.nodes[i].assign.var, prs->ast.nodes[i].assign.expr);
			break;

		case AST_ADDR:
			fprintf(file, "&%zi", prs->ast.nodes[i].unary_op.val);
			break;
		
		case AST_POINTER_CONST:
			fprintf(file, "&const %zi", prs->ast.nodes[i].pointer_type.base_type);
			break;

		case AST_POINTER_VAR:
			fprintf(file, "&var %zi", prs->ast.nodes[i].pointer_type.base_type);
			break;

		case AST_POINTER_ABYSS:
			fprintf(file, "&abyss %zi", prs->ast.nodes[i].pointer_type.base_type);
			break;

		case AST_DEREF:
			fprintf(file, "*%zi", prs->ast.nodes[i].unary_op.val);
			break;

		case AST_ARRAY:
			fprintf(file, "[%zi]%zi", prs->ast.nodes[i].array.len, prs->ast.nodes[i].array.elem_type);
			break;

		case AST_SLICE_CONST:
			fprintf(file, "[]const %zi", prs->ast.nodes[i].slice.elem_type);
			break;

		case AST_SLICE_VAR:
			fprintf(file, "[]var %zi", prs->ast.nodes[i].slice.elem_type);
			break;

		case AST_SLICE_ABYSS:
			fprintf(file, "[]abyss %zi", prs->ast.nodes[i].slice.elem_type);
			break;

		case AST_SUBSCRIPT:
			fprintf(file, "%zi[%zi]", prs->ast.nodes[i].subscript.arr, prs->ast.nodes[i].subscript.index);
			break;

		case AST_ARRAY_LIT:
			fprintf(file, "{");
			for(size_t j = 0; j < prs->ast.nodes[i].array_lit.elem_count; j++) {
				fprintf(file, "%zi, ", prs->ast.nodes[i].array_lit.elems[j]);
			}
			fprintf(file, "}");
			break;

		case AST_STRUCT_TYPE:
			fprintf(file, "struct {");
			for(size_t j = 0; j < prs->ast.nodes[i].struct_type.member_count; j++) {
				fprintf(
					file,
					"%s: %zi, ",
					prs->identifiers[prs->ast.nodes[i].struct_type.member_name_ids[j]],
					prs->ast.nodes[i].struct_type.member_types[j]
				);
			}
			fprintf(file, "}");
			break;

		case AST_STRUCT_LIT:
			fprintf(file, "(struct) {");
			for(size_t j = 0; j < prs->ast.nodes[i].struct_lit.member_count; j++) {
				fprintf(
					file,
					".%s = %zi, ",
					prs->identifiers[prs->ast.nodes[i].struct_lit.member_name_ids[j]],
					prs->ast.nodes[i].struct_lit.member_values[j]
				);
			}
			fprintf(file, "}");
			break;

		case AST_STRUCT_ACCESS:
			fprintf(
				file,
				"%zi.%s",
				prs->ast.nodes[i].struct_access.parent,
				prs->identifiers[prs->ast.nodes[i].struct_access.member_id]
			);
			break;

		case AST_STRING_LIT:
			fprintf(
				file,
				"String \"%s\"",
				prs->strings[prs->ast.nodes[i].string_lit.id]
			);
			break;
		case AST_ZSTRING_LIT:
			fprintf(
				file,
				"ZString \"%s\"",
				prs->strings[prs->ast.nodes[i].string_lit.id]
			);
			break;
		case AST_CSTRING_LIT:
			fprintf(
				file,
				"CString \"%s\"",
				prs->strings[prs->ast.nodes[i].string_lit.id]
			);
			break;
		case AST_EXTERN:
			fprintf(
				file,
				"#extern(%zi)",
				prs->ast.nodes[i].extrn.name
			);
			break;
		case AST_DISCARD:
			fprintf(
				file,
				"discard %zi",
				prs->ast.nodes[i].discard.value
			);
			break;
		case AST_ARROW:
			fprintf(
				file,
				"%zi -> %zi",
				prs->ast.nodes[i].arrow.parent,
				prs->ast.nodes[i].arrow.member
			);
			break;
		case AST_TYPEDEF:
			fprintf(
				file,
				"typedef \"%s\" = %zi",
				prs->identifiers[prs->ast.nodes[i].typdef.id],
				prs->ast.nodes[i].typdef.backing
			);
			break;
		case AST_CHAR_LIT:
			fprintf(
				file,
				"Char %02X",
				prs->ast.nodes[i].char_lit.val
			);
			break;
		case AST_COMP_EQ:
			fprintf(file, "%zi == %zi", prs->ast.nodes[i].binop.lhs, prs->ast.nodes[i].binop.rhs);
			break;
		case AST_COMP_GE:
			fprintf(file, "%zi >= %zi", prs->ast.nodes[i].binop.lhs, prs->ast.nodes[i].binop.rhs);
			break;
		case AST_COMP_LE:
			fprintf(file, "%zi <= %zi", prs->ast.nodes[i].binop.lhs, prs->ast.nodes[i].binop.rhs);
			break;
		case AST_COMP_NE:
			fprintf(file, "%zi != %zi", prs->ast.nodes[i].binop.lhs, prs->ast.nodes[i].binop.rhs);
			break;
		case AST_COMP_GT:
			fprintf(file, "%zi > %zi", prs->ast.nodes[i].binop.lhs, prs->ast.nodes[i].binop.rhs);
			break;
		case AST_COMP_LT:
			fprintf(file, "%zi < %zi", prs->ast.nodes[i].binop.lhs, prs->ast.nodes[i].binop.rhs);
			break;
		case AST_LOGIC_AND:
			fprintf(file, "%zi && %zi", prs->ast.nodes[i].binop.lhs, prs->ast.nodes[i].binop.rhs);
			break;
		case AST_LOGIC_OR:
			fprintf(file, "%zi || %zi", prs->ast.nodes[i].binop.lhs, prs->ast.nodes[i].binop.rhs);
			break;
		case AST_LOGIC_NOT:
			fprintf(file, "!%zi", prs->ast.nodes[i].unary_op.val);
			break;
		case AST_IF:
			fprintf(
				file,
				"if(%zi; %zi) {%zi} else {%zi}\n",
				prs->ast.nodes[i].if_statement.decl,
				prs->ast.nodes[i].if_statement.condition,
				prs->ast.nodes[i].if_statement.block,
				prs->ast.nodes[i].if_statement.else_block
			);
			break;
		}
		fprintf(file, "\n");
	}
}

static void handle_MODULE(Parser *prs, size_t *index, Error *err)
{
	switch(prs->tokens[*index].type) {
	case TOKEN_TYPEDEF: {
		const DebugInfo debug = prs->tokens[*index].debug.debug_info;

		*index += 1;

		if(prs->tokens[*index].type != TOKEN_IDENT) {
			wyrt_diag(
				stderr, prs->identifiers, prs->strings, NULL,
				"Expected identifier after typedef, found %T\n",
				&prs->tokens[*index]
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		const size_t id = prs->tokens[*index].ident.id;

		*index += 1;

		if(prs->tokens[*index].type != TOKEN_ASSIGN) {
			wyrt_diag(
				stderr, prs->identifiers, prs->strings, NULL,
				"Expected '=' in typedef, found %T\n",
				&prs->tokens[*index]
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		nodelist_alloc(&prs->ast, 2, err);
		if(*err) goto RET;

		AstNode *module = &prs->ast.nodes[parsestack_top(&prs->parse_stack)->ref];
		module->module.statements = realloc(
			module->module.statements,
			sizeof(size_t) * ++module->module.statement_count
		);
		CHECK_MALLOC(module->module.statements);
		module->module.statements[module->module.statement_count - 1] = prs->ast.len - 2;

		prs->ast.nodes[prs->ast.len - 2] = (AstNode) {
			.typdef = {
				.type = AST_TYPEDEF,
				.debug_info = debug,
				.id = id,
				.backing = prs->ast.len - 1,
			},
		};

		parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_SEMICOLON}, err);
		if(*err) goto RET;
		parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_TYPE, prs->ast.len - 1}, err);
		if(*err) goto RET;
	} break;

	case TOKEN_FN: {
		nodelist_push(
			&prs->ast,
			(AstNode) {
				.type = AST_FN_DEF,
			},
			err
		);
		if(*err) goto RET;

		AstNode *module = &prs->ast.nodes[parsestack_top(&prs->parse_stack)->ref];
		module->module.statements = realloc(
			module->module.statements, sizeof(size_t) * ++module->module.statement_count
		);
		CHECK_MALLOC(module->module.statements);
		module->module.statements[module->module.statement_count - 1] = prs->ast.len - 1;

		parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_FN_DEF, prs->ast.len - 1}, err);
		if(*err) goto RET;
	} break;

	case TOKEN_EOF:
		parsestack_pop(&prs->parse_stack);
		break;

	default:
		wyrt_diag(
			stderr, prs->identifiers, prs->strings, NULL,
			"Illegal Top-Level Statement %T\n",
			&prs->tokens[*index]
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}
	*index += 1;
RET:
	return;
}

static void handle_FN_DEF(Parser *prs, size_t *index, Error *err)
{
	AstNode *fn = &prs->ast.nodes[parsestack_pop(&prs->parse_stack).ref];
	fn->fn_def.debug_info = prs->tokens[*index].debug.debug_info;
	fn->fn_def.ident = prs->ast.len + 2;
	fn->fn_def.fn_type = prs->ast.len + 1;
	fn->fn_def.block = prs->ast.len;

	parsestack_alloc(&prs->parse_stack, 3, err);
	if(*err) goto RET;
	nodelist_alloc(&prs->ast, 3, err);
	if(*err) goto RET;

	size_t ast_end = prs->ast.len - 1;
	size_t ps_end = prs->parse_stack.len - 1;
	
	prs->parse_stack.state[ps_end  ] = (ParseState) {PARSE_STATE_IDENT,		ast_end};
	prs->parse_stack.state[ps_end-1] = (ParseState) {PARSE_STATE_FN_TYPE,	ast_end-1};
	prs->parse_stack.state[ps_end-2] = (ParseState) {PARSE_STATE_FN_BODY,	ast_end-2};

	prs->ast.nodes[ast_end  ] = (AstNode) {.ident = {AST_IDENT}};
	prs->ast.nodes[ast_end-1] = (AstNode) {.fn_type = {AST_FN_TYPE}};
	prs->ast.nodes[ast_end-2] = (AstNode) {.block = {AST_BLOCK}};

RET:
	return;
}

static void handle_IDENT(Parser *prs, size_t *index, Error *err)
{
	if(prs->tokens[*index].type != TOKEN_IDENT) {
		wyrt_diag(
			stderr, prs->identifiers, prs->strings, NULL,
			"Expected Identifier, found %T\n",
			&prs->tokens[*index]
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

	prs->ast.nodes[parsestack_pop(&prs->parse_stack).ref] = (AstNode) {
		.ident = {
			.type = AST_IDENT,
			.debug_info = prs->tokens[*index].debug.debug_info,
			.id = prs->tokens[*index].ident.id,
		},
	};

	*index += 1;
RET:
	return;
}

static void handle_FN_TYPE(Parser *prs, size_t *index, Error *err)
{
	if(prs->tokens[*index].type != TOKEN_LPAREN) {
		wyrt_diag(
			stderr, prs->identifiers, prs->strings, NULL,
			"Expected '(' to start Function Type, found %T\n",
			&prs->tokens[*index]
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	} 

	*index += 1;

	size_t ref = parsestack_pop(&prs->parse_stack).ref;
	prs->ast.nodes[ref] = (AstNode) {
		.fn_type = {
			.type = AST_FN_TYPE,
			.args = NULL,
			.arg_count = 0,
			.ret_type = 0,
		},
	};
	
	parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_FN_TYPE_RET, ref}, err);
	if(*err) goto RET;
	parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_FN_TYPE_LIST, ref}, err);
	if(*err) goto RET;
	
RET:
	return;
}

static void handle_FN_TYPE_LIST(Parser *prs, size_t *index, Error *err)
{
	if(prs->tokens[*index].type == TOKEN_RPAREN) {
		parsestack_pop(&prs->parse_stack);
		*index += 1;
		goto RET;
	}

	AstNode *fn_type = &prs->ast.nodes[parsestack_top(&prs->parse_stack)->ref];
	
	if(fn_type->fn_type.args && prs->tokens[(*index)++].type != TOKEN_COMMA) {
		wyrt_diag(
			stderr, prs->identifiers, prs->strings, NULL,
			"Expected ',' after Function Argument, found %T\n",
			&prs->tokens[*index - 1]
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}
	
	size_t new_count = ++(fn_type->fn_type.arg_count);

	fn_type->fn_type.args = realloc(fn_type->fn_type.args, 2 * sizeof(size_t) * new_count);
	CHECK_MALLOC(fn_type->fn_type.args);

	nodelist_alloc(&prs->ast, 1, err);
	if(*err) goto RET;

	fn_type->fn_type.args[new_count * 2 - 2] = prs->ast.len - 1;

	parsestack_push(
		&prs->parse_stack,
		(ParseState) {PARSE_STATE_FN_TYPE_ARG, parsestack_top(&prs->parse_stack)->ref},
		err
	);
	if(*err) goto RET;

	parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_IDENT, prs->ast.len - 1}, err);
	if(*err) goto RET;

RET:
	return;
}

static void handle_FN_TYPE_ARG(Parser *prs, size_t *index, Error *err)
{
	if(prs->tokens[*index].type != TOKEN_COLON) {
		wyrt_diag(
			stderr, prs->identifiers, prs->strings, NULL,
			"Expected ':' in Function Argument, found %T\n",
			&prs->tokens[*index]
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

	*index += 1;

	nodelist_alloc(&prs->ast, 1, err);
	if(*err) goto RET;

	AstNode *fn_type = &prs->ast.nodes[parsestack_pop(&prs->parse_stack).ref];
	fn_type->fn_type.args[fn_type->fn_type.arg_count * 2 - 1] = prs->ast.len - 1;

	parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_TYPE, prs->ast.len - 1}, err);
	if(*err) goto RET;

RET:
	return;
}

static void handle_FN_TYPE_RET(Parser *prs, size_t *index, Error *err)
{
	nodelist_alloc(&prs->ast, 1, err);
	if(*err) goto RET;

	prs->ast.nodes[parsestack_pop(&prs->parse_stack).ref].fn_type.ret_type = prs->ast.len - 1;

	parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_TYPE, prs->ast.len - 1}, err);
	if(*err) goto RET;

RET:
	return;
}

static void handle_TYPE(Parser *prs, size_t *index, Error *err)
{
	size_t ref = parsestack_top(&prs->parse_stack)->ref;

	switch(prs->tokens[*index].type) {
	case TOKEN_STRUCT:
		if(prs->tokens[++*index].type != TOKEN_LCURLY) {
			wyrt_diag(
				stderr, prs->identifiers, prs->strings, NULL,
				"Expected '{' after 'struct', found %T\n",
				&prs->tokens[*index]
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		prs->ast.nodes[ref] = (AstNode) {
			.struct_type = {
				.type = AST_STRUCT_TYPE,
				.debug_info = prs->tokens[*index - 1].debug.debug_info,
				.member_count = 0,
				.member_name_ids = NULL,
				.member_types = NULL,
			},
		};

		parsestack_pop(&prs->parse_stack);
		parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_STRUCT_TYPE, prs->ast.len - 1}, err);
		if(*err) goto RET;
		break;

	case TOKEN_AMPERSAND:
		*index += 1;
		if(prs->tokens[*index].type != TOKEN_CONST
			&& prs->tokens[*index].type != TOKEN_VAR
			&& prs->tokens[*index].type != TOKEN_ABYSS
		) {
			wyrt_diag(
				stderr, prs->identifiers, prs->strings, NULL,
				"Expected access specifier in pointer type, found %T\n",
				&prs->tokens[*index]
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		prs->ast.nodes[ref] = (AstNode) {
			.pointer_type = {
				.type = AST_POINTER_CONST + prs->tokens[*index].type - TOKEN_CONST,
				.debug_info = prs->tokens[*index].debug.debug_info,
				.base_type = prs->ast.len,
			},
		};

		nodelist_alloc(&prs->ast, 1, err);
		if(*err) goto RET;

		*parsestack_top(&prs->parse_stack) = (ParseState) {PARSE_STATE_TYPE, prs->ast.len - 1};
		*index += 1;
		break;
	
	case TOKEN_LSQUARE:
		*index += 1;
		switch(prs->tokens[*index].type) {
		case TOKEN_RSQUARE:
			*index += 1;
			
			if(prs->tokens[*index].type < TOKEN_CONST
				|| prs->tokens[*index].type > TOKEN_ABYSS
			) {
				wyrt_diag(
					stderr, prs->identifiers, prs->strings, NULL,
					"Expected Access Specifier in Slice Type, found %T\n",
					&prs->tokens[*index]
				);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			prs->ast.nodes[parsestack_pop(&prs->parse_stack).ref] = (AstNode) {
				.slice = {
					.type = AST_SLICE_CONST + (prs->tokens[*index].type - TOKEN_CONST),
					.debug_info = prs->tokens[*index].debug.debug_info,
					.elem_type = prs->ast.len,
				},
			};

			*index += 1;

			nodelist_alloc(&prs->ast, 1, err);
			if(*err) goto RET;

			parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_TYPE, prs->ast.len - 1}, err);
			if(*err) goto RET;
			break;

		case TOKEN_UNDERSCORE:
		case TOKEN_INT_LIT: {
			intmax_t len = prs->tokens[*index].type == TOKEN_INT_LIT ? prs->tokens[*index].int_lit.val : 0;

			*index += 1;

			if(prs->tokens[*index].type != TOKEN_RSQUARE) {
				wyrt_diag(
					stderr, prs->identifiers, prs->strings, NULL,
					"Expected ']' in Array Type, found %T\n",
					&prs->tokens[*index]
				);

				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			prs->ast.nodes[parsestack_pop(&prs->parse_stack).ref] = (AstNode) {
				.array = {
					.type = AST_ARRAY,
					.debug_info = prs->tokens[*index].debug.debug_info,
					.elem_type = prs->ast.len,
					.len = len,
				},
			};

			nodelist_alloc(&prs->ast, 1, err);
			if(*err) goto RET;

			parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_TYPE, prs->ast.len - 1}, err);
			if(*err) goto RET;

			*index += 1;
		} break;
		
		default:
			wyrt_diag(
				stderr, prs->identifiers, prs->strings, NULL,
				"Expected Count or ']' for Array/Slice Type, found %T\n",
				&prs->tokens[*index]
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
		break;

	case TOKEN_IDENT:
		parsestack_top(&prs->parse_stack)->type = PARSE_STATE_IDENT;
		break;

	default:
		wyrt_diag(
			stderr, prs->identifiers, prs->strings, NULL,
			"Expected Type, found %T\n",
			&prs->tokens[*index]
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}
	
RET:
	return;
}

static void handle_BLOCK(Parser *prs, size_t *index, Error *err)
{
	if(prs->tokens[*index].type != TOKEN_LCURLY) {
		wyrt_diag(
			stderr, prs->identifiers, prs->strings, NULL,
			"Expected '{', found %T\n",
			&prs->tokens[*index]
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

	prs->ast.nodes[parsestack_top(&prs->parse_stack)->ref] = (AstNode) {
		.block = {
			.type = AST_BLOCK,
			.debug_info = prs->tokens[*index].debug.debug_info,
		},
	};

	*index += 1;

	parsestack_top(&prs->parse_stack)->type = PARSE_STATE_BLOCK_LIST;

RET:
	return;
}

static void handle_BLOCK_LIST(Parser *prs, size_t *index, Error *err)
{
	AstNode *block = &prs->ast.nodes[parsestack_top(&prs->parse_stack)->ref];
	if(block->block.statements) {
		if(prs->tokens[*index].type != TOKEN_SEMICOLON) {
			if(prs->tokens[*index - 1].type != TOKEN_RCURLY) {
				wyrt_diag(
					stderr, prs->identifiers, prs->strings, NULL,
					"Expected ';' after Statement, found %T\n",
					&prs->tokens[*index]
				);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
		} else {
			*index += 1;
		}
	}

	if(prs->tokens[*index].type == TOKEN_RCURLY) {
		parsestack_pop(&prs->parse_stack);
		*index += 1;
		goto RET;
	}

	block->block.statements = realloc(
		block->block.statements, sizeof(size_t) * ++block->block.statement_count
	);

	switch(prs->tokens[*index].type) {
	case TOKEN_IF: {
		block->block.statements[block->block.statement_count - 1] = prs->ast.len;
		nodelist_alloc(&prs->ast, 1, err);
		if(*err) goto RET;
		parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_IF, prs->ast.len - 1}, err);
		if(*err) goto RET;
	} break;

	case TOKEN_DISCARD: {
		size_t ref = prs->ast.len + 1;
		block->block.statements[block->block.statement_count - 1] = ref - 1;

		nodelist_alloc(&prs->ast, 2, err);
		if(*err) goto RET;

		prs->ast.nodes[ref - 1] = (AstNode) {
			.discard = {
				.type = AST_DISCARD,
				.debug_info = prs->tokens[*index].debug.debug_info,
				.value = ref,	
			},
		};

		parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_EXPR, ref}, err);
		if(*err) goto RET;

		*index += 1;
	} break;
	case TOKEN_RETURN: {
		size_t ref = prs->ast.len + 1;
		block->block.statements[block->block.statement_count - 1] = ref - 1;

		if(prs->tokens[*index + 1].type == TOKEN_SEMICOLON) {
			nodelist_push(
				&prs->ast,
				(AstNode) {
					.ret = {
						.type = AST_RET,
						.debug_info = prs->tokens[*index].debug.debug_info,
						.return_val = 0,
					},
				},
				err
			);
			if(*err) goto RET;
			*index += 1;
			break;
		}

		nodelist_alloc(&prs->ast, 2, err);
		if(*err) goto RET;

		prs->ast.nodes[ref-1] = (AstNode) {
			.ret = {
				.type = AST_RET,
				.debug_info = prs->tokens[*index].debug.debug_info,
				.return_val = ref,
			},
		};
		parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_EXPR, ref}, err);
		if(*err) goto RET;

		*index += 1;
	} break;

	case TOKEN_CONST:
	case TOKEN_VAR: {
		block->block.statements[block->block.statement_count - 1] = prs->ast.len;
		nodelist_alloc(&prs->ast, 1, err);
		if(*err) goto RET;
		parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_VAR_DECL, prs->ast.len - 1}, err);
		if(*err) goto RET;
	} break;

	default:
		nodelist_push(
			&prs->ast,
			(AstNode) {
				.assign = {
					.type = AST_ASSIGN,
					.var = prs->ast.len - 1,
				},
			},	
			err
		);
		if(*err) goto RET;

		parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_ASSIGNMENT, prs->ast.len - 1}, err);
		if(*err) goto RET;
		parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_EXPR, prs->ast.len - 1}, err);
		if(*err) goto RET;

		block->block.statements[block->block.statement_count - 1] = prs->ast.len - 1;
	}

RET:
	return;
}

static void handle_IF(Parser *prs, size_t *index, Error *err)
{
	size_t ref = parsestack_pop(&prs->parse_stack).ref;

	nodelist_alloc(&prs->ast, 1, err);
	if(*err) goto RET;

	prs->ast.nodes[prs->ast.len - 1] = (AstNode) {
		.block = {
			.type = AST_BLOCK,
			.statements = NULL,
			.statement_count = 0,
		}
	};

	prs->ast.nodes[ref] = (AstNode) {
		.if_statement = {
			.type = AST_IF,
			.debug_info = prs->tokens[*index].debug.debug_info,
			.block = prs->ast.len - 1,
			.else_block = 0,
		},
	};

	*index += 1;
	
	if(prs->tokens[*index].type != TOKEN_LPAREN) {
		wyrt_diag(
			stderr, prs->identifiers, prs->strings, NULL,
			"Expected '(' after 'if', found %T\n",
			&prs->tokens[*index]
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

	*index += 1;

	parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_ELSE, ref}, err);
	if(*err) goto RET;
	parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_BLOCK, prs->ast.len - 1}, err);
	if(*err) goto RET;
	parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_RPAREN, ref}, err);
	if(*err) goto RET;

	if(prs->tokens[*index].type == TOKEN_CONST
		|| prs->tokens[*index].type == TOKEN_VAR
	) {
		prs->ast.nodes[ref].if_statement.decl = prs->ast.len;
		parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_CONDITION, ref}, err);
		if(*err) goto RET;
		parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_SEMICOLON}, err);
		if(*err) goto RET;
		parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_VAR_DECL, prs->ast.len}, err);
		if(*err) goto RET;
		nodelist_alloc(&prs->ast, 1, err);
		if(*err) goto RET;
	} else {
		nodelist_alloc(&prs->ast, 1, err);
		if(*err) goto RET;
		prs->ast.nodes[ref].if_statement.condition = prs->ast.len - 1;
		parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_EXPR, prs->ast.len - 1}, err);
		if(*err) goto RET;
	}

RET:
	return;
}

static void handle_VAR_DECL(Parser *prs, size_t *index, Error *err)
{
	size_t ref = parsestack_pop(&prs->parse_stack).ref;

	*index += 1;

	if(prs->tokens[*index].type != TOKEN_IDENT) {
		wyrt_diag(
			stderr, prs->identifiers, prs->strings, NULL,
			"Expected Identifier after variable declaration, found %T\n",
			&prs->tokens[*index]
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

	prs->ast.nodes[ref] = (AstNode) {
		.var_decl = {
			.type = AST_VAR_DECL,
			.debug_info = prs->tokens[*index].debug.debug_info,
			.mut = prs->tokens[*index - 1].type == TOKEN_VAR,
			.id =  prs->tokens[*index].ident.id,
			.data_type = ref + 1,
		},
	};

	*index += 1;

	if(prs->tokens[*index].type != TOKEN_COLON) {
		wyrt_diag(
			stderr, prs->identifiers, prs->strings, NULL,
			"Expected ':' in Variable Declaration, found %T\n",
			&prs->tokens[*index]
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

	nodelist_alloc(&prs->ast, 1, err);
	if(*err) goto RET;


	parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_VAR_DECL_INIT, ref}, err);
	if(*err) goto RET;	
	parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_TYPE, prs->ast.len - 1}, err);
	if(*err) goto RET;

	*index += 1;

RET:
	return;
}

typedef enum {
	EXPR_NONE,
	EXPR_ADD,
	EXPR_SUB,
	EXPR_MUL,
	EXPR_DIV,
	EXPR_ADDR,
	EXPR_DEREF,
	EXPR_LPAREN,
	EXPR_FN_CALL,
	EXPR_ARRAY_LIT,
	EXPR_SUBSCRIPT,
	EXPR_STRUCT_LIT,
	EXPR_STRUCT_ACCESS,
	EXPR_ARROW,
	EXPR_COMP_EQ,
	EXPR_COMP_GE,
	EXPR_COMP_LE,
	EXPR_COMP_NE,
	EXPR_COMP_GT,
	EXPR_COMP_LT,
	EXPR_LOGIC_AND,
	EXPR_LOGIC_OR,
	EXPR_LOGIC_NOT
} ExprOpType;

typedef struct {
	ExprOpType type;
	DebugInfo debug;
	size_t extra;
	size_t id;
} ExprOp;

static ExprOpType token_to_op(TokenType type)
{
	switch(type) {
	case TOKEN_PLUS: return EXPR_ADD;
	case TOKEN_MINUS: return EXPR_SUB;
	case TOKEN_STAR: return EXPR_MUL;
	case TOKEN_FSLASH: return EXPR_DIV;
	case TOKEN_AMPERSAND: return EXPR_ADDR;
	case TOKEN_PERIOD: return EXPR_STRUCT_ACCESS;
	case TOKEN_ARROW: return EXPR_ARROW;
	case TOKEN_COMP_EQ: return EXPR_COMP_EQ;
	case TOKEN_COMP_GE: return EXPR_COMP_GE;
	case TOKEN_COMP_LE: return EXPR_COMP_LE;
	case TOKEN_COMP_NE: return EXPR_COMP_NE;
	case TOKEN_COMP_GT: return EXPR_COMP_GT;
	case TOKEN_COMP_LT: return EXPR_COMP_LT;
	case TOKEN_LOGIC_AND: return EXPR_LOGIC_AND;
	case TOKEN_LOGIC_OR: return EXPR_LOGIC_OR;
	case TOKEN_LOGIC_NOT: return EXPR_LOGIC_NOT;
	default: assert(0);
	}
}

static int op_precedence(ExprOpType type)
{
	switch(type) {
	case EXPR_FN_CALL:
	case EXPR_LPAREN:
	case EXPR_ARRAY_LIT:
	case EXPR_STRUCT_LIT:
		return 0;
	case EXPR_LOGIC_OR:
		return 1;
	case EXPR_LOGIC_AND:
		return 2;
	case EXPR_COMP_EQ:
	case EXPR_COMP_GE:
	case EXPR_COMP_LE:
	case EXPR_COMP_NE:
	case EXPR_COMP_GT:
	case EXPR_COMP_LT:
		return 3;
	case EXPR_ADD:
	case EXPR_SUB:
		return 4;
	case EXPR_MUL:
	case EXPR_DIV:
		return 5;
	case EXPR_LOGIC_NOT:
	case EXPR_ADDR:
	case EXPR_DEREF:
		return 6;
	case EXPR_STRUCT_ACCESS:
	case EXPR_ARROW:
		return 7;
	default: assert(0);
	}
}

static AstNodeType op_to_ast(ExprOpType type)
{
	switch(type) {
	case EXPR_COMP_EQ: return AST_COMP_EQ;
	case EXPR_COMP_GE: return AST_COMP_GE;
	case EXPR_COMP_LE: return AST_COMP_LE;
	case EXPR_COMP_NE: return AST_COMP_NE;
	case EXPR_COMP_GT: return AST_COMP_GT;
	case EXPR_COMP_LT: return AST_COMP_LT;
	case EXPR_LOGIC_AND: return AST_LOGIC_AND;
	case EXPR_LOGIC_OR: return AST_LOGIC_OR;
	case EXPR_LOGIC_NOT: return AST_LOGIC_NOT;
	case EXPR_ADD: return AST_ADD;
	case EXPR_SUB: return AST_SUB;
	case EXPR_MUL: return AST_MUL;
	case EXPR_DIV: return AST_DIV;
	case EXPR_ADDR: return AST_ADDR;
	case EXPR_DEREF: return AST_DEREF;
	case EXPR_STRUCT_ACCESS: return AST_STRUCT_ACCESS;
	case EXPR_ARROW: return AST_ARROW;
	default: assert(0);
	}
}

static void pop_op(Parser *prs, ExprOp *op, DynArr *free_list, size_t index, Error *err)
{
	size_t *rhs;
	size_t *lhs;
	switch(op->type) {
	case EXPR_LOGIC_NOT:
	case EXPR_ADDR:
	case EXPR_DEREF:
		rhs = dynarr_pop(free_list);
		if(!rhs) {
			wyrt_diag(
				stderr, prs->identifiers, prs->strings, NULL,
				"Expected operand for unary operator at %l\n",
				&op->debug
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		nodelist_push(
			&prs->ast,
			(AstNode) {
				.unary_op = {
					.type = op_to_ast(op->type),
					.debug_info = op->debug,
					.val = *rhs,
				},
			},
			err
		);
		if(*err) goto RET;
		break;

	case EXPR_COMP_EQ:
	case EXPR_COMP_GE:
	case EXPR_COMP_LE:
	case EXPR_COMP_NE:
	case EXPR_COMP_GT:
	case EXPR_COMP_LT:
	case EXPR_LOGIC_AND:
	case EXPR_LOGIC_OR:
	case EXPR_ADD:
	case EXPR_SUB:
	case EXPR_MUL:
	case EXPR_DIV:
		rhs = dynarr_pop(free_list);
		if(!rhs) {
			wyrt_diag(
				stderr, prs->identifiers, prs->strings, NULL,
				"Expected operands for binary operator at %l\n",
				&op->debug
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		lhs = dynarr_pop(free_list);
		if(!lhs) {
			wyrt_diag(
				stderr, prs->identifiers, prs->strings, NULL,
				"Expected second operand for binary operator at %l\n",
				&op->debug
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		nodelist_push(
			&prs->ast,
			(AstNode) {
				.binop = {
					.type = op_to_ast(op->type),
					.debug_info = op->debug,
					.lhs = *lhs,
					.rhs = *rhs,
				},
			},
			err
		);
		if(*err) goto RET;
		break;
	
	case EXPR_STRUCT_ACCESS:
	case EXPR_ARROW:
		rhs = dynarr_pop(free_list);
		if(!rhs) {
			wyrt_diag(
				stderr, prs->identifiers, prs->strings, NULL,
				"Expected operands for binary operator at %l\n",
				&op->debug
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		lhs = dynarr_pop(free_list);
		if(!lhs) {
			wyrt_diag(
				stderr, prs->identifiers, prs->strings, NULL,
				"Expected second operand for binary operator at %l\n",
				&op->debug
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		if(prs->ast.nodes[*rhs].type != AST_IDENT) {
			wyrt_diag(
				stderr, prs->identifiers, prs->strings, NULL,
				"Expected Identifier for struct access at %l\n",
				&prs->ast.nodes[*rhs].debug.debug_info
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		nodelist_push(
			&prs->ast,
			(AstNode) {
				.struct_access = {
					.type = op_to_ast(op->type),
					.debug_info = op->debug,
					.parent = *lhs,
					.member_id = prs->ast.nodes[*rhs].ident.id,
				},
			},
			err
		);
		if(*err) goto RET;
		break;


	case EXPR_NONE:
	case EXPR_LPAREN:
	case EXPR_FN_CALL:
	case EXPR_ARRAY_LIT:
	case EXPR_SUBSCRIPT:
	case EXPR_STRUCT_LIT:
		wyrt_diag(
			stderr, prs->identifiers, prs->strings, NULL,
			"Malformed Expression at %l\n",
			&prs->tokens[index].debug.debug_info
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

	dynarr_push(free_list, &(size_t) {prs->ast.len - 1}, err);
	if(*err) goto RET;

RET:
	return;
}

static void handle_EXPR(Parser *prs, size_t *index, Error *err)
{
	size_t ref = parsestack_top(&prs->parse_stack)->ref;
	DynArr free_list;
	DynArr op_stack;
	dynarr_init(&free_list, sizeof(size_t));
	dynarr_init(&op_stack, sizeof(ExprOp));

	bool ended = false;

	bool has_prev_op = true;
	while(!ended) {
		switch(prs->tokens[*index].type) {
		case TOKEN_COMMA: {
			bool found = false;
			for(size_t i = 0; i < op_stack.count; i++) {
				ExprOp *op = dynarr_from_back(&op_stack, i);

				if(op->type == EXPR_FN_CALL || op->type == EXPR_ARRAY_LIT || op->type == EXPR_STRUCT_LIT) {
					op->extra += 1;
					found = true;
					break;
				}
			}
			
			if(!found) {
				wyrt_diag(
					stderr, prs->identifiers, prs->strings, NULL,
					"Unexpected %T\n",
					&prs->tokens[*index]
				);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
			
			*index += 1;
			has_prev_op = true;
		} break;

		case TOKEN_ASSIGN:
		case TOKEN_SEMICOLON:
		case TOKEN_ADD_ASSIGN:
		case TOKEN_SUB_ASSIGN:
		case TOKEN_MUL_ASSIGN:
		case TOKEN_DIV_ASSIGN:
			ended = true;
			break;

		case TOKEN_LPAREN:
			dynarr_push(&op_stack, &(ExprOp) {EXPR_LPAREN}, err);
			if(*err) goto RET;
			*index += 1;
			has_prev_op = true;
			break;
		case TOKEN_RPAREN: {
			ExprOp *op;
			do {
				op = (ExprOp*)dynarr_pop(&op_stack);
				if(!op) {
					ended = true;
					*index -= 1;
					break;
				}

				if(op->type == EXPR_LPAREN) break;
				if(op->type == EXPR_FN_CALL) {
					AstNode fn_call = {
						.fn_call = {
							.type = AST_FN_CALL,
							.debug_info = op->debug,
							.fn_id = op->id,
						},
					};

					if(!op->extra && has_prev_op) {
						fn_call.fn_call.args = NULL;
						fn_call.fn_call.arg_count = 0;
					} else {
						fn_call.fn_call.arg_count = op->extra + 1;
						fn_call.fn_call.args = malloc((1 + op->extra) * sizeof(size_t));

						CHECK_MALLOC(fn_call.fn_call.args);

						for(size_t i = 0; i < op->extra + 1; i++) {
							fn_call.fn_call.args[op->extra - i] = *(size_t*)dynarr_from_back(&free_list, i);
						}
						free_list.count -= op->extra + 1;
					}

					nodelist_push(&prs->ast, fn_call, err);
					if(*err) goto RET;
					dynarr_push(&free_list, &(size_t) {prs->ast.len - 1}, err);
					if(*err) goto RET;
					break;
				}

				pop_op(prs, op, &free_list, *index, err);
				if(*err) goto RET;
			} while(true);
			has_prev_op = false;
			*index += 1;
		} break;

		case TOKEN_RCURLY: {
			ExprOp *op;
			do {
				op = (ExprOp*)dynarr_pop(&op_stack);
				if(!op) {
					wyrt_diag(
						stderr, prs->identifiers, prs->strings, NULL,
						"Extra %T\n",
						&prs->tokens[*index]
					);
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}

				if(op->type == EXPR_ARRAY_LIT) {
					AstNode array_lit = {
						.array_lit = {
							.type = AST_ARRAY_LIT,
							.debug_info = op->debug,
							.elem_count = op->extra + 1,
							.elems = malloc((1 + op->extra) * sizeof(size_t)),
						},
					};

					CHECK_MALLOC(array_lit.array_lit.elems);

					if(free_list.count < op->extra + 1) {
						wyrt_diag(
							stderr, prs->identifiers, prs->strings, NULL,
							"Malformed array literal at %l\n",
							&prs->tokens[*index].debug.debug_info
						);
						*err = ERROR_UNEXPECTED_DATA;
						goto RET;
					}

					for(size_t i = 0; i < op->extra + 1; i++) {
						array_lit.array_lit.elems[op->extra - i] = *(size_t*)dynarr_from_back(&free_list, i);
					}
					free_list.count -= op->extra + 1;

					nodelist_push(&prs->ast, array_lit, err);
					if(*err) goto RET;
					dynarr_push(&free_list, &(size_t) {prs->ast.len - 1}, err);
					if(*err) goto RET;
					break;
				} else if(op->type == EXPR_STRUCT_LIT) {
					AstNode struct_lit = {
						.struct_lit = {
							.type = AST_STRUCT_LIT,
							.debug_info = op->debug,
							.member_count = (op->extra + 1) / 2,
							.member_name_ids = malloc((1 + op->extra) / 2 * sizeof(size_t)),
							.member_values = malloc((1 + op->extra) / 2* sizeof(size_t)),
						},
					};
					CHECK_MALLOC(struct_lit.struct_lit.member_name_ids);
					CHECK_MALLOC(struct_lit.struct_lit.member_values);

					if(free_list.count < op->extra + 1) {
						wyrt_diag(
							stderr, prs->identifiers, prs->strings, NULL,
							"Malformed struct literal at %l\n",
							&prs->tokens[*index].debug.debug_info
						);
						*err = ERROR_UNEXPECTED_DATA;
						goto RET;
					}

					for(size_t i = 0; i < op->extra + 1; i += 2) {
						size_t member = *(size_t*)dynarr_from_back(&free_list, i + 1);
						size_t value = *(size_t*)dynarr_from_back(&free_list, i);

						struct_lit.struct_lit.member_name_ids[(1 + op->extra)/2 - i/2 - 1] = member;
						struct_lit.struct_lit.member_values[(1 + op->extra)/2 - i/2 - 1] = value;
					}

					free_list.count -= op->extra + 1;

					nodelist_push(&prs->ast, struct_lit, err);
					if(*err) goto RET;
					dynarr_push(&free_list, &(size_t) {prs->ast.len - 1}, err);
					if(*err) goto RET;
					break;
				} else {
					pop_op(prs, op, &free_list, *index, err);
					if(*err) goto RET;
				}
			} while(true);
			has_prev_op = false;
			*index += 1;
		} break;

		case TOKEN_LSQUARE: {
			ExprOp *top = dynarr_from_back(&op_stack, 0);
			while(top && (top->type == EXPR_STRUCT_ACCESS || top->type == EXPR_ARROW)) {
				pop_op(prs, top, &free_list, *index, err);
				op_stack.count -= 1;
				top = dynarr_from_back(&op_stack, 0);
				if(*err) goto RET;
			}

			if(!free_list.count) {
				wyrt_diag(
					stderr, prs->identifiers, prs->strings, NULL,
					"Expected Parent of Subscript, found %T\n",
					&prs->tokens[*index].debug.debug_info
				);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			size_t arr = *(size_t*)dynarr_pop(&free_list);
			dynarr_push(
				&op_stack,
				&(ExprOp) {
					.type = EXPR_SUBSCRIPT,
					.debug = prs->tokens[*index].debug.debug_info,
					.extra = arr,
				},
				err
			);
			if(*err) goto RET;

			*index += 1;
			has_prev_op = true;
		} break;

		case TOKEN_RSQUARE: {
			do {
				ExprOp *op = dynarr_pop(&op_stack);
				if(!op) {
					wyrt_diag(
						stderr, prs->identifiers, prs->strings, NULL,
						"Extra ']' at %l\n",
						&prs->tokens[*index].debug.debug_info
					);
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}

				if(op->type == EXPR_SUBSCRIPT) {
					size_t *idx = dynarr_pop(&free_list);

					nodelist_push(
						&prs->ast,
						(AstNode) {
							.subscript = {
								.type = AST_SUBSCRIPT,
								.debug_info = op->debug,
								.arr = op->extra,
								.index = *idx,
							},
						},
						err
					);
					if(*err) goto RET;

					dynarr_push(&free_list, &(size_t){prs->ast.len - 1}, err);
					if(*err) goto RET;
					break;
				}

				pop_op(prs, op, &free_list, *index, err);
				if(*err) goto RET;
			} while(true);
			*index += 1;

		} break;

		case TOKEN_STRING_LIT:
		case TOKEN_ZSTRING_LIT:
		case TOKEN_CSTRING_LIT: {
			nodelist_push(
				&prs->ast,
				(AstNode) {
					.string_lit = {
						.type = AST_STRING_LIT + (prs->tokens[*index].type - TOKEN_STRING_LIT),
						.debug_info = prs->tokens[*index].debug.debug_info,
						.id = prs->tokens[*index].string_lit.id,
					},
				},
				err
			);
			if(*err) goto RET;

			dynarr_push(&free_list, &(size_t) {prs->ast.len - 1}, err);
			if(*err) goto RET;

			*index += 1;
			has_prev_op = false;
		} break;

		case TOKEN_INT_LIT:
			if(!has_prev_op) {
				wyrt_diag(
					stderr, prs->identifiers, prs->strings, NULL,
					"Malformed Expression: extra %T\n",
					&prs->tokens[*index]
				);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			nodelist_push(
				&prs->ast,
				(AstNode) {
					.int_lit = {
						.type = AST_INT_LIT,
						.debug_info = prs->tokens[*index].debug.debug_info,
						.val = prs->tokens[*index].int_lit.val,
					},
				},
				err
			);
			if(*err) goto RET;

			dynarr_push(&free_list, &(size_t) {prs->ast.len - 1}, err);
			if(*err) goto RET;

			*index += 1;
			has_prev_op = false;
			break;

		case TOKEN_CHAR_LIT:
			if(!has_prev_op) {
				wyrt_diag(
					stderr, prs->identifiers, prs->strings, NULL,
					"Malformed Expression: extra %T\n",
					&prs->tokens[*index]
				);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			nodelist_push(
				&prs->ast,
				(AstNode) {
					.int_lit = {
						.type = AST_CHAR_LIT,
						.debug_info = prs->tokens[*index].debug.debug_info,
						.val = prs->tokens[*index].char_lit.val,
					},
				},
				err
			);
			if(*err) goto RET;

			dynarr_push(&free_list, &(size_t) {prs->ast.len - 1}, err);
			if(*err) goto RET;

			*index += 1;
			has_prev_op = false;
			break;

		case TOKEN_UNDERSCORE:
			*index += 1;

			if(prs->tokens[*index].type != TOKEN_LCURLY) {
				wyrt_diag(
					stderr, prs->identifiers, prs->strings, NULL,
					"Expected struct literal, found %T\n",
					&prs->tokens[*index]
				);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			*index += 1;

			dynarr_push(
				&op_stack,
				&(ExprOp) {
					.type = EXPR_STRUCT_LIT,
					.debug = prs->tokens[*index].debug.debug_info,
					.extra = 0,
				},
				err
			);
			if(*err) goto RET;
			break;

		case TOKEN_IDENT:
			if(!has_prev_op) {
				wyrt_diag(
					stderr, prs->identifiers, prs->strings, NULL,
					"Malformed Expression: extra %T\n",
					&prs->tokens[*index]
				);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			if(prs->tokens[*index + 1].type == TOKEN_LPAREN) {
				*index += 1;

				dynarr_push(
					&op_stack,
					&(ExprOp) {
						.type = EXPR_FN_CALL,
						.debug = prs->tokens[*index].debug.debug_info,
						.extra = 0,
						.id = prs->tokens[*index - 1].ident.id,
					},
					err
				);
				if(*err) goto RET;
			} else {
				nodelist_push(
					&prs->ast,
					(AstNode) {
						.ident = {
							.type = AST_IDENT,
							.debug_info = prs->tokens[*index].debug.debug_info,
							.id = prs->tokens[*index].ident.id,
						},
					},
					err
				);
				if(*err) goto RET;

				dynarr_push(&free_list, &(size_t) {prs->ast.len - 1}, err);
				if(*err) goto RET;

				has_prev_op = false;
			}
			*index += 1;
			break;

		case TOKEN_LCURLY:
			dynarr_push(
				&op_stack,
				&(ExprOp) {
					.type = EXPR_ARRAY_LIT,
					.debug = prs->tokens[*index].debug.debug_info,
					.extra = 0,
				},
				err
			);
			if(*err) goto RET;
			*index += 1;
			break;

		case TOKEN_PERIOD:
			if(prs->tokens[*index - 1].type == TOKEN_COMMA || prs->tokens[*index - 1].type == TOKEN_LCURLY) {
				*index += 1;
				if(prs->tokens[*index].type != TOKEN_IDENT) {
					wyrt_diag(
						stderr, prs->identifiers, prs->strings, NULL,
						"Expected identifier in struct literal, found %T\n",
						&prs->tokens[*index]
					);
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}

				size_t id = prs->tokens[*index].ident.id;

				*index += 1;

				if(prs->tokens[*index].type != TOKEN_ASSIGN) {
					wyrt_diag(
						stderr, prs->identifiers, prs->strings, NULL,
						"Expected '=' in struct literal, found %T\n",
						&prs->tokens[*index]
					);
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}

				*index += 1;

				dynarr_push(&free_list, &id, err);
				if(*err) goto RET;

				ExprOp *op = dynarr_from_back(&op_stack, 0);
				if(!op || op->type != EXPR_STRUCT_LIT) {
					wyrt_diag(
						stderr, prs->identifiers, prs->strings, NULL,
						"Expected to be inside a struct literal at %l\n",
						&prs->tokens[*index].debug.debug_info
					);
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}

				op->extra += 1;
				break;
			}
			//fallthrough
		case TOKEN_COMP_EQ:
		case TOKEN_COMP_GE:
		case TOKEN_COMP_LE:
		case TOKEN_COMP_NE:
		case TOKEN_COMP_GT:
		case TOKEN_COMP_LT:
		case TOKEN_LOGIC_AND:
		case TOKEN_LOGIC_OR:
		case TOKEN_LOGIC_NOT:
		case TOKEN_PLUS:
		case TOKEN_MINUS:
		case TOKEN_STAR:
		case TOKEN_FSLASH:
		case TOKEN_AMPERSAND:
		case TOKEN_ARROW: {
			ExprOpType this = token_to_op(prs->tokens[*index].type);
			if(this == EXPR_MUL && has_prev_op) this = EXPR_DEREF;

			ExprOp *top;
			bool higher_prec;
			do {
				top = dynarr_from_back(&op_stack, 0);
				if(!top) break;
				higher_prec = op_precedence(top->type) >= op_precedence(this);
				
				if(higher_prec) {
					ExprOp *op = dynarr_pop(&op_stack);

					pop_op(prs, op, &free_list, *index, err);
					if(*err) goto RET;
				}				
			} while(higher_prec);

			dynarr_push(&op_stack, &(ExprOp) {this, prs->tokens[*index].debug.debug_info}, err);
			if(*err) goto RET;
			has_prev_op = true;
			*index += 1;
		} break;

		default:
			wyrt_diag(
				stderr, prs->identifiers, prs->strings, NULL,
				"Expected Expression, found %T\n",
				&prs->tokens[*index]
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
	}

	while(op_stack.count) {
		ExprOp *op = dynarr_pop(&op_stack);
		
		pop_op(prs, op, &free_list, *index, err);
		if(*err) goto RET;
	}

	if(free_list.count != 1) {
		wyrt_diag(
			stderr, prs->identifiers, prs->strings, NULL,
			"Malformed Expression at %l\n",
			prs->tokens[*index].debug.debug_info
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

	prs->ast.nodes[ref] = prs->ast.nodes[prs->ast.len - 1];
	prs->ast.len -= 1;

	parsestack_pop(&prs->parse_stack);

RET:
	dynarr_clean(&free_list);
	dynarr_clean(&op_stack);
	return;
}

static void handle_VAR_DECL_INIT(Parser *prs, size_t *index, Error *err)
{
	if(prs->tokens[*index].type == TOKEN_ASSIGN) {
		nodelist_alloc(&prs->ast, 1, err);
		if(*err) goto RET;

		*index += 1;

		prs->ast.nodes[parsestack_pop(&prs->parse_stack).ref].var_decl.initial = prs->ast.len - 1;	
		parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_EXPR, prs->ast.len - 1}, err);
		if(*err) goto RET;
	} else {
		parsestack_pop(&prs->parse_stack);
	}

RET:
	return;
}

static void handle_ASSIGNMENT(Parser *prs, size_t *index, Error *err)
{
	size_t ref = parsestack_top(&prs->parse_stack)->ref;

	switch(prs->tokens[*index].type) {
	case TOKEN_ASSIGN:
	case TOKEN_ADD_ASSIGN:
	case TOKEN_SUB_ASSIGN:
	case TOKEN_MUL_ASSIGN:
	case TOKEN_DIV_ASSIGN:
		nodelist_push(&prs->ast, prs->ast.nodes[ref], err);
		if(*err) goto RET;
		prs->ast.nodes[ref].type = AST_ASSIGN + prs->tokens[*index].type - TOKEN_ASSIGN;
		prs->ast.nodes[ref].debug.debug_info = prs->tokens[*index].debug.debug_info;
		prs->ast.nodes[ref].assign.var = prs->ast.len - 1;
		prs->ast.nodes[ref].assign.expr = prs->ast.len;

		nodelist_alloc(&prs->ast, 1, err);
		if(*err) goto RET;

		*parsestack_top(&prs->parse_stack) = (ParseState) {PARSE_STATE_EXPR, prs->ast.len - 1};

		*index += 1;
		break;

	default:
		if(prs->ast.nodes[ref].type == AST_FN_CALL) {
			parsestack_pop(&prs->parse_stack);
			break;
		}

		wyrt_diag(
			stderr, prs->identifiers, prs->strings, NULL,
			"Expected assignment, found %T\n",
			&prs->tokens[*index]
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

RET:
	return;
}

static void handle_STRUCT_TYPE(Parser *prs, size_t *index, Error *err)
{
	size_t ref = parsestack_top(&prs->parse_stack)->ref;

	if(prs->tokens[*index].type == TOKEN_RCURLY) {
		parsestack_pop(&prs->parse_stack);
		*index += 1;
		goto RET;
	}

	if(prs->ast.nodes[ref].struct_type.member_count) {
		if(prs->tokens[*index].type != TOKEN_COMMA) {
			wyrt_diag(
				stderr, prs->identifiers, prs->strings, NULL,
				"Expected ',' after struct member, found %T\n",
				&prs->tokens[*index]
			);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		if(prs->tokens[*index + 1].type == TOKEN_RCURLY) {
			parsestack_pop(&prs->parse_stack);
			*index += 2;
			goto RET;
		}
	}

	*index += 1;

	if(prs->tokens[*index].type != TOKEN_IDENT) {
		wyrt_diag(
			stderr, prs->identifiers, prs->strings, NULL,
			"Expected Identifier for struct member, found %T\n",
			&prs->tokens[*index]
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

	size_t id = prs->tokens[*index].ident.id;

	*index += 1;

	if(prs->tokens[*index].type != TOKEN_COLON) {
		wyrt_diag(
			stderr, prs->identifiers, prs->strings, NULL,
			"Expected ':' after struct member, found %T\n",
			&prs->tokens[*index]
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

	*index += 1;

	prs->ast.nodes[ref].struct_type.member_count += 1;
	prs->ast.nodes[ref].struct_type.member_name_ids = realloc(
		prs->ast.nodes[ref].struct_type.member_name_ids,
		prs->ast.nodes[ref].struct_type.member_count * sizeof(size_t)
	);
	CHECK_MALLOC(prs->ast.nodes[ref].struct_type.member_name_ids);
	prs->ast.nodes[ref].struct_type.member_types = realloc(
		prs->ast.nodes[ref].struct_type.member_types,
		prs->ast.nodes[ref].struct_type.member_count * sizeof(size_t)
	);
	CHECK_MALLOC(prs->ast.nodes[ref].struct_type.member_types);

	prs->ast.nodes[ref].struct_type.member_name_ids[
		prs->ast.nodes[ref].struct_type.member_count - 1
	] = id;

	prs->ast.nodes[ref].struct_type.member_types[
		prs->ast.nodes[ref].struct_type.member_count - 1
	] = prs->ast.len;
	
	nodelist_alloc(&prs->ast, 1, err);
	if(*err) goto RET;

	parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_TYPE, prs->ast.len - 1}, err);
	if(*err) goto RET;

RET:
	return;
}

static void handle_EXTERN(Parser *prs, size_t *index, Error *err)
{
	const DebugInfo debug = prs->tokens[*index].debug.debug_info;

	*index += 1;

	if(prs->tokens[*index].type != TOKEN_LPAREN) {
		wyrt_diag(
			stderr, prs->identifiers, prs->strings, NULL,
			"Expected '(' after #extern, found %T\n",
			&prs->tokens[*index]
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

	*index += 1;

	if(prs->tokens[*index].type != TOKEN_STRING_LIT) {
		wyrt_diag(
			stderr, prs->identifiers, prs->strings, NULL,
			"Expected string literal in #extern, found %T\n",
			&prs->tokens[*index]
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

	nodelist_push(
		&prs->ast,
		(AstNode) {
			.string_lit = {
				.type = AST_STRING_LIT,
				.debug_info = prs->tokens[*index].debug.debug_info,
				.id = prs->tokens[*index].string_lit.id,
			},
		},
		err
	);
	if(*err) goto RET;

	*index += 1;

	if(prs->tokens[*index].type != TOKEN_RPAREN) {
		wyrt_diag(
			stderr, prs->identifiers, prs->strings, NULL,
			"Expected ')' to end #extern, found %T\n",
			&prs->tokens[*index]
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

	*index += 1;

	prs->ast.nodes[parsestack_pop(&prs->parse_stack).ref] = (AstNode) {
		.extrn = {
			.type = AST_EXTERN,
			.debug_info = debug,
			.name = prs->ast.len - 1,
		},
	};

RET:
	return;
}

static void handle_FN_BODY(Parser *prs, size_t *index, Error *err)
{
	if(prs->tokens[*index].type == TOKEN_HASH_EXTERN) {
		parsestack_top(&prs->parse_stack)->type = PARSE_STATE_EXTERN;
	} else {
		parsestack_top(&prs->parse_stack)->type = PARSE_STATE_BLOCK;
	}
}

static void handle_SEMICOLON(Parser *prs, size_t *index, Error *err)
{
	if(prs->tokens[*index].type != TOKEN_SEMICOLON) {
		wyrt_diag(
			stderr, prs->identifiers, prs->strings, NULL,
			"Expected ';', found %T\n",
			&prs->tokens[*index]
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}
	*index += 1;

	prs->parse_stack.len -= 1;
RET:
	return;
}

static void handle_RPAREN(Parser *prs, size_t *index, Error *err)
{
	if(prs->tokens[*index].type != TOKEN_RPAREN) {
		wyrt_diag(
			stderr, prs->identifiers, prs->strings, NULL,
			"Expected ')', found %T\n",
			&prs->tokens[*index]
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}
	*index += 1;
	prs->parse_stack.len -= 1;
RET:
	return;
}

static void handle_ELSE(Parser *prs, size_t *index, Error *err)
{
	if(prs->tokens[*index].type == TOKEN_ELSE) {
		size_t ref = parsestack_pop(&prs->parse_stack).ref;

		*index += 1;

		prs->ast.nodes[ref].if_statement.else_block = prs->ast.len;
		nodelist_alloc(&prs->ast, 1, err);
		if(*err) goto RET;

		if(prs->tokens[*index].type == TOKEN_IF) {
			prs->ast.nodes[prs->ast.len - 1] = (AstNode) {
				.block = {
					.type = AST_BLOCK,
					.debug_info = prs->tokens[*index].debug.debug_info,
					.statements = malloc(sizeof(size_t)),
					.statement_count = 1,
				},
			};
			CHECK_MALLOC(prs->ast.nodes[prs->ast.len - 1].block.statements);

			prs->ast.nodes[prs->ast.len - 1].block.statements[0] = prs->ast.len;
			nodelist_alloc(&prs->ast, 1, err);
			if(*err) goto RET;

			parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_IF, prs->ast.len - 1}, err);
			if(*err) goto RET;
		} else {
			if(prs->tokens[*index].type != TOKEN_LCURLY) {
				wyrt_diag(
					stderr, prs->identifiers, prs->strings, NULL,
					"Expected '{' after 'else', found %T\n",
					&prs->tokens[*index]
				);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_BLOCK, prs->ast.len - 1}, err);
			if(*err) goto RET;
		}
	}
RET:
	return;
}

static void handle_CONDITION(Parser *prs, size_t *index, Error *err)
{
	if(prs->tokens[*index].type != TOKEN_RPAREN) {
		prs->ast.nodes[parsestack_top(&prs->parse_stack)->ref].if_statement.condition = prs->ast.len;
		nodelist_alloc(&prs->ast, 1, err);
		if(*err) goto RET;
		*parsestack_top(&prs->parse_stack) = (ParseState) {PARSE_STATE_EXPR, prs->ast.len - 1};
	} else {
		prs->parse_stack.len -= 1;
	}

RET:
	return;
}

void parser_parse(Parser *prs, Error *err)
{
	nodelist_push(&prs->ast, (AstNode) {.module = {AST_MODULE, prs->tokens[0].debug.debug_info}}, err);
	if(*err) goto RET;
	parsestack_push(&prs->parse_stack, (ParseState) {PARSE_STATE_MODULE, 0}, err);
	if(*err) goto RET;

	size_t i = 0;
	while(prs->parse_stack.len) {
		ParseState *top = parsestack_top(&prs->parse_stack);
#define X(n) case PARSE_STATE_ ##n: \
		handle_ ##n (prs, &i, err); \
		if(*err) goto RET; \
		break;

		switch(top->type) {
			PARSE_STATE_LIST
		}
#undef X
	}

RET:
	return;
}
