#include "parser.h"

static AstNodeType binop_tok_to_ast(TokenType type)
{
	switch(type) {
	case TOKEN_PLUS:
		return AST_ADD;
	case TOKEN_MINUS:
		return AST_SUB;
	case TOKEN_STAR:
		return AST_MUL;
	case TOKEN_FSLASH:
		return AST_DIV;
	default:
		return AST_NONE;
	}
}

static void binop_push(Token binop, DynArr *nodes, DynArr *free_terms, DynArr *operator_stack, Error *err)
{
	size_t *rhs = dynarr_pop(free_terms);
	size_t *lhs = dynarr_pop(free_terms);

	if(!lhs || !rhs) {
		fprintf(stderr, "Malformed Expression at ");
		lexer_print_debug_to_file(stderr, &binop.debug.debug_info);
		fprintf(stderr, "\n");
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

	dynarr_push(
		nodes,
		&(AstNode) {
			.binop = {
				.type = binop_tok_to_ast(binop.type),
				.debug_info = binop.debug.debug_info,
				.lhs = *lhs,
				.rhs = *rhs,
			},
		},
		err
	);
	if(*err) goto RET;

	dynarr_push(free_terms, &(size_t) {nodes->count - 1}, err);
	if(*err) goto RET;

RET:
	return;
}

static void parse_expr(
	const Token *tokens, size_t token_count, size_t *index,
	DynArr *nodes,
	char *const *identifiers,
	Error *err
)
{
	DynArr free_terms;
	dynarr_init(&free_terms, sizeof(size_t));

	//SHUNTING YARD
	DynArr operator_stack;
	dynarr_init(&operator_stack, sizeof(Token));

	bool terminated = false;


	Token *top;
	while(!terminated) {
		top = dynarr_from_back(&operator_stack, 0);
		switch(tokens[*index].type) {
		case TOKEN_LPAREN:
			dynarr_push(&operator_stack, &tokens[*index], err);
			if(*err) goto RET;
			break;

		case TOKEN_RPAREN:
			while(top && top->type != TOKEN_LPAREN) {
				operator_stack.count -= 1;
				binop_push(*top, nodes, &free_terms, &operator_stack, err);
				if(*err) goto RET;

				top = dynarr_from_back(&operator_stack, 0);
			}

			if(!top) {
				fprintf(stderr, "Too Many Close Parentheses in expression at ");
				lexer_print_debug_to_file(stderr, &tokens[*index].debug.debug_info);
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			operator_stack.count -= 1;
			break;

		case TOKEN_STAR:
			do {} while(0);

			while(top && (top->type == TOKEN_STAR || top->type == TOKEN_FSLASH)) {
					operator_stack.count -= 1;

					binop_push(*top, nodes, &free_terms, &operator_stack, err);
					if(*err) goto RET;
					top = dynarr_from_back(&operator_stack, 0);
			}

			dynarr_push(&operator_stack, &tokens[*index], err);
			if(*err) goto RET;

			break;

		case TOKEN_FSLASH:
			do {} while(0);

			while(top && (top->type == TOKEN_STAR || top->type == TOKEN_FSLASH)) {
					operator_stack.count -= 1;
					binop_push(*top, nodes, &free_terms, &operator_stack, err);
					if(*err) goto RET;
					top = dynarr_from_back(&operator_stack, 0);
			}

			dynarr_push(&operator_stack, &tokens[*index], err);
			if(*err) goto RET;

			break;

		case TOKEN_PLUS:
			do {} while(0);

			while(
				top &&
				(
					top->type == TOKEN_STAR || top->type == TOKEN_FSLASH
					|| top->type == TOKEN_PLUS || top->type == TOKEN_MINUS
				)
			) {
					operator_stack.count -= 1;
					binop_push(*top, nodes, &free_terms, &operator_stack, err);
					if(*err) goto RET;
					top = dynarr_from_back(&operator_stack, 0);
			}

			dynarr_push(&operator_stack, &tokens[*index], err);
			if(*err) goto RET;

			break;

		case TOKEN_MINUS:
			do {} while(0);

			while(
				top &&
				(
					top->type == TOKEN_STAR || top->type == TOKEN_FSLASH
					|| top->type == TOKEN_PLUS || top->type == TOKEN_MINUS
				)
			) {
					operator_stack.count -= 1;
					binop_push(*top, nodes, &free_terms, &operator_stack, err);
					if(*err) goto RET;
					top = dynarr_from_back(&operator_stack, 0);
			}

			dynarr_push(&operator_stack, &tokens[*index], err);
			if(*err) goto RET;

			break;

		case TOKEN_INT_LIT:
			dynarr_push(
				nodes,
				&(AstNode) {
					.int_lit = {
						.type = AST_INT_LIT,
						.debug_info = tokens[*index].debug.debug_info,
						.val = tokens[*index].int_lit.val,
					},
				},
				err
			);
			if(*err) goto RET;
			dynarr_push(&free_terms, &(size_t) {nodes->count - 1}, err);
			if(*err) goto RET;
			break;

		case TOKEN_IDENT:
			dynarr_push(
				nodes,
				&(AstNode) {
					.ident = {
						.type = AST_IDENT,
						.debug_info = tokens[*index].debug.debug_info,
						.id = tokens[*index].ident.id,
					},
				},
				err
			);
			if(*err) goto RET;
			dynarr_push(&free_terms, &(size_t) {nodes->count - 1}, err);
			if(*err) goto RET;
			break;

		case TOKEN_SEMICOLON:
			terminated = true;
			*index -= 2;
			break;

		default:
			fprintf(stderr, "Unexpected ");
			lexer_print_token_to_file(stderr, &tokens[*index], identifiers);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		*index += 1;
	}

	top = dynarr_from_back(&operator_stack, 0);
	while(top) {
		if(top->type == TOKEN_LPAREN) {
			fprintf(stderr, "Too Many Open Parentheses in Expression at ");
			lexer_print_debug_to_file(stderr, &top->debug.debug_info);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
		operator_stack.count -= 1;

		binop_push(*top, nodes, &free_terms, &operator_stack, err);

		if(*err) goto RET;

		top = dynarr_from_back(&operator_stack, 0);
	}

	if(free_terms.count != 1) {
		fprintf(stderr, "Malformed Expression at ");
		lexer_print_debug_to_file(stderr, &((AstNode*) dynarr_from_back(nodes, 0))->debug.debug_info);
		fprintf(stderr, "\n");
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}


RET:
	dynarr_clean(&free_terms);
	dynarr_clean(&operator_stack);
}


static void parse_block(
	const Token *tokens, size_t token_count, size_t *index,
	DynArr *nodes,
	char *const *identifiers,
	Error *err
)
{
	DynArr statements;
	dynarr_init(&statements, sizeof(size_t));

	if(tokens[*index].type != TOKEN_LCURLY) {
		fprintf(stderr, "Expected '{', found");
		lexer_print_token_to_file(stderr, &tokens[*index], identifiers);
		fprintf(stderr, " instead\n");
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

	DebugInfo debug_info = tokens[*index].debug.debug_info;

	*index += 1;

	while(tokens[*index].type != TOKEN_RCURLY) {
		DebugInfo debug = tokens[*index].debug.debug_info;

		switch(tokens[*index].type) {
		case TOKEN_RETURN:
			*index += 1;
			parse_expr(tokens, token_count, index, nodes, identifiers, err);
			if(*err) goto RET;

			size_t return_val = nodes->count - 1;

			dynarr_push(
				nodes,
				&(AstNode) {
					.ret = {
						.type = AST_RET,
						.debug_info = debug,
						.return_val = return_val,
					},
				},
				err
			);
			if(*err) goto RET;

			break;

		case TOKEN_CONST:
			*index += 1;

			if(tokens[*index].type != TOKEN_IDENT) {
				fprintf(stderr, "Expected Identifier, found ");
				lexer_print_token_to_file(stderr, &tokens[*index], identifiers);
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			size_t id = tokens[*index].ident.id;
			
			*index += 1;

			if(tokens[*index].type != TOKEN_COLON) {
				fprintf(stderr, "Expected ':', found ");
				lexer_print_token_to_file(stderr, &tokens[*index], identifiers);
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			*index += 1;

			if(tokens[*index].type != TOKEN_IDENT) {
				fprintf(stderr, "Expected Identifier, found ");
				lexer_print_token_to_file(stderr, &tokens[*index], identifiers);
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			dynarr_push(
				nodes,
				&(AstNode) {
					.ident = {
						.type = AST_IDENT,
						.debug_info = tokens[*index].debug.debug_info,
						.id = tokens[*index].ident.id,
					},
				},
				err
			);
			if(*err) goto RET;

			size_t data_type = nodes->count - 1; 

			size_t initial = 0;
			if(tokens[*index + 1].type == TOKEN_ASSIGN) {
				*index += 2;
				parse_expr(tokens, token_count, index, nodes, identifiers, err);
				if(*err) goto RET;

				initial = nodes->count - 1;
			}

			dynarr_push(
				nodes,
				&(AstNode) {
					.var_decl = {
						.type = AST_VAR_DECL,
						.debug_info = debug,
						.mut = false,
						.id = id,
						.data_type = data_type,
						.initial = initial,
					},
				},
				err
			);
			if(*err) goto RET;

			break;

		default:
			fprintf(stderr, "Unexpected ");
			lexer_print_token_to_file(stderr, &tokens[*index], identifiers);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		if(tokens[++*index].type != TOKEN_SEMICOLON) {
			fprintf(stderr, "Expected ';', found ");
			lexer_print_token_to_file(stderr, &tokens[*index], identifiers);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		dynarr_push(&statements, &(size_t) { nodes->count - 1 }, err);
		if(*err) goto RET;

		*index += 1;
	}

	dynarr_push(
		nodes,
		&(AstNode) {
			.block = {
				.type = AST_BLOCK,
				.debug_info = debug_info,
				.statements = statements.data,
				.statement_count = statements.count,
			},
		},
		err
	);
	if(*err) goto RET;

RET:
	if(*err && statements.data) free(statements.data);
	return;
}

static void parse_type(
	const Token *tokens, size_t token_count, size_t *index,
	DynArr *nodes,
	char *const *identifiers,
	Error *err
)
{
	DebugInfo location = tokens[*index].debug.debug_info;

	switch(tokens[*index].type) {
	case TOKEN_IDENT:
		dynarr_push(
			nodes,
			&(AstNode) {
				.ident = {
					.type = AST_IDENT,
					.debug_info = location,
					.id = tokens[*index].ident.id,
				},
			},
			err
		);
		if(*err) goto RET;
		break;
		
	case TOKEN_LPAREN:
		do {} while (0);

		DynArr args;
		dynarr_init(&args, 2 * sizeof(size_t));

		while(tokens[++*index].type != TOKEN_RPAREN) {
			if(tokens[*index].type != TOKEN_IDENT) {
				fprintf(stderr, "Error: Expected Identifier found ");
				lexer_print_token_to_file(
					stderr,
					&tokens[*index],
					identifiers
				);
				fprintf(stderr, " instead.\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			dynarr_push(
				nodes,
				&(AstNode) {
					.ident = {
						.type = AST_IDENT,
						.debug_info = tokens[*index].debug.debug_info,
						.id = tokens[*index].ident.id
					},
				}, err
			);
			if(*err) goto RET;

			size_t name = nodes->count - 1;

			*index += 1;

			parse_type(
				tokens, token_count, index, nodes, identifiers, err
			);
			if(*err) goto RET;

			size_t data_type = nodes->count - 1;

			dynarr_push(&args, &(size_t[2]) { name, data_type }, err);
			if(*err) goto RET;
		}


		*index += 1;
		
		parse_type(
			tokens, token_count, index, nodes, identifiers, err
		);
		if(*err) goto RET;
		size_t ret_type = nodes->count - 1;

		dynarr_push(
			nodes,
			&(AstNode) {
				.fn_type = {
					.type = AST_FN_TYPE,
					.debug_info = location,
					.args = (size_t *) args.data,
					.arg_count = args.count,
					.ret_type = ret_type,
				},
			},
			err
		);
		if(*err) goto RET;
		break;
		
	default:
		fprintf(stderr, "Error: Expected Type, found ");
		lexer_print_token_to_file(
			stderr,
			&tokens[*index],
			identifiers
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}
RET:
	return;
}

void parser_gen_ast(
	Token const *tokens, size_t token_count,
	AstNode **nodes, size_t *node_count,
	char *const *identifiers,
	Error *err
)
{
	DynArr node_list = { 0 };
	dynarr_init(&node_list, sizeof(AstNode));

	dynarr_alloc(&node_list, 1, err); // Allocate for module
	if(*err) goto RET;

	DynArr module_statements = { 0 };
	dynarr_init(&module_statements, sizeof(size_t));

	size_t index = 0;
	while(index < token_count - 1) {
		DebugInfo debug_info = tokens[index].debug.debug_info;

		switch(tokens[index].type) {
		case TOKEN_FN:
			if(tokens[++index].type != TOKEN_IDENT) {
				fprintf(stderr, "Error: Expected Identifier, found ");
				lexer_print_token_to_file(
					stderr, &tokens[index], identifiers
				);
				fprintf(stderr, " instead.\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
			dynarr_push(
				&node_list,
				&(AstNode) {
					.ident = {
						.type = AST_IDENT,
						.debug_info = tokens[index].debug.debug_info,
						.id = tokens[index].ident.id
					}
				},
				err
			);
			size_t ident = node_list.count - 1;
			if(*err) goto RET;

			index += 1;
			parse_type(
				tokens, token_count, &index, &node_list, identifiers, err      
			);
			if(*err) goto RET;
			size_t fn_type = node_list.count - 1;

			index += 1;
			parse_block(
				tokens, token_count, &index, &node_list, identifiers, err
			);
			if(*err) goto RET;
			size_t block = node_list.count - 1;

			dynarr_push(
				&node_list,
				&(AstNode) {
					.fn_def = {
						.type = AST_FN_DEF,
						.debug_info = debug_info,
						.ident = ident,
						.fn_type = fn_type,
						.block = block,
					}
				},
				err
			);
			if(*err) goto RET;

			dynarr_push(
				&module_statements,
				&(size_t) { node_list.count - 1 },
				err
			);
			if(*err) goto RET;

			break;
		default:
			fprintf(stderr, "Error: Expected Declaration, found ");
			lexer_print_token_to_file(
				stderr, &tokens[index], identifiers
			);
			fprintf(stderr, "instead.\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		index += 1;
	}

RET:
	*nodes = (AstNode *) node_list.data;
	*node_count = node_list.count;
	(*nodes)[0] = (AstNode) {
		.module = {
			.type = AST_MODULE,
			.debug_info = tokens[0].debug.debug_info,
			.statements = module_statements.data,
			.statement_count = module_statements.count,
		},
	};
	return;
}

void parser_clean_ast(AstNode *nodes, size_t node_count)
{
	for(size_t i = 0; i < node_count; i++) {
		switch(nodes[i].type) {
		case AST_FN_TYPE:
			free(nodes[i].fn_type.args);
			break;

		case AST_BLOCK:
			free(nodes[i].block.statements);
			break;

		case AST_MODULE:
			free(nodes[i].module.statements);
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
			break;
		}
	}
}

void parser_print_ast_to_file(
	FILE *file,
	AstNode *nodes, size_t node_count, char *const *identifiers
)
{
	for(size_t i = 0; i < node_count; i++) {
		fprintf(file, "%zi:\t", i);
		switch(nodes[i].type) {
		case AST_NONE:
			fprintf(file, "NONE");
			break;
			
		case AST_FN_DEF:
			fprintf(
				file,
				"FN_DEF '%zi': %zi { %zi }",
				nodes[i].fn_def.ident,
				nodes[i].fn_def.fn_type,
				nodes[i].fn_def.block
			);
			break;

		case AST_FN_TYPE:
			fprintf(file, "(");
			for(size_t j = 0; j < nodes[i].fn_type.arg_count; j++) {
				fprintf(
					file,
					"%zi: %zi, ",
					nodes[i].fn_type.args[j], nodes[i].fn_type.args[j + 1]
				);
			}
			fprintf(file, ") -> %zi", nodes[i].fn_type.ret_type);
			break;
			
		case AST_BLOCK:
			fprintf(file, "{");
			for(size_t j = 0; j < nodes[i].block.statement_count; j++) {
				fprintf(
					file,
					"%zi, ",
					nodes[i].block.statements[j]
				);
			}
			fprintf(file, "}");
			break;
			
		case AST_RET:
			fprintf(file, "return %zi", nodes[i].ret.return_val);
			break;
			
		case AST_INT_LIT:
			fprintf(file, "%ji", nodes[i].int_lit.val);
			break;
			
		case AST_IDENT:
			fprintf(file, "'%s'", identifiers[nodes[i].ident.id]);
			break;  
			
		case AST_MODULE:
			fprintf(file, "MODULE[");
			for(size_t j = 0; j < nodes[i].module.statement_count; j++) {
				fprintf(
					file,
					"%zi, ",
					nodes[i].module.statements[j]
				);
			}
			fprintf(file, "]");
			break;
		case AST_MUL:
			fprintf(file, "%zi * %zi", nodes[i].binop.lhs, nodes[i].binop.rhs);
			break;
		case AST_ADD:
			fprintf(file, "%zi + %zi", nodes[i].binop.lhs, nodes[i].binop.rhs);
			break;
		case AST_DIV:
			fprintf(file, "%zi / %zi", nodes[i].binop.lhs, nodes[i].binop.rhs);
			break;
		case AST_SUB:
			fprintf(file, "%zi - %zi", nodes[i].binop.lhs, nodes[i].binop.rhs);
			break;

		case AST_VAR_DECL:
			fprintf(
				file,
				"%s '%s': %ji [= %ji]", 
				nodes[i].var_decl.mut ? "var" : "const",
				identifiers[nodes[i].var_decl.id],
				nodes[i].var_decl.data_type,
				nodes[i].var_decl.initial
			);
			break;
		}
		fprintf(file, "\n");
	}
}
