#include "parser.h"

void parse_expr(
	const Token *tokens, size_t token_count, size_t *index,
	AstNode **nodes, size_t *node_count,
	char *const *identifiers,
	Error *err
)
{
	DynArr node_list = {
		.data = (char *) *nodes,
		.elem_size = sizeof(AstNode),
		.count = *node_count,
		.capacity = *node_count,
	};

	switch(tokens[*index].type) {
	case TOKEN_LCURLY:
		do {} while(0);

		DynArr statements;
		dynarr_init(&statements, sizeof(size_t));

		DebugInfo block_debug = tokens[*index].debug.debug_info;
		
		while(tokens[++*index].type != TOKEN_RCURLY) {
			switch(tokens[*index].type) {
			case TOKEN_RETURN:
				do {} while(0);

				DebugInfo ret_debug = tokens[*index].debug.debug_info;

				*index += 1;
				parse_expr(
					tokens, token_count, index,
					(AstNode **) &node_list.data, &node_list.count,
					identifiers, err
				);
				if(*err) goto RET;

				size_t ret_val = node_list.count - 1;

				dynarr_push(
					&node_list,
					&(AstNode) {
						.ret = {
							.type = AST_RET,
							.debug_info = ret_debug,
							.return_val = ret_val,
						},
					}, 
					err
				);
				if(*err) goto RET;

				break;
				
			default:
				fprintf(stderr, "Error: Illegal Statement in Block at ");
				lexer_print_debug_to_file(
					stderr,
					&tokens[*index].debug.debug_info
				);
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			if(tokens[++*index].type != TOKEN_SEMICOLON) {
				fprintf(stderr, "Error: Expected ';' found ");
				lexer_print_token_to_file(
					stderr,
					&tokens[*index],
					identifiers
				);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			dynarr_push(&statements, &(size_t) {node_list.count - 1}, err);
			if(*err) goto RET;
		}

		dynarr_push(
			&node_list,
			&(AstNode) {
				.block = {
					.type = AST_BLOCK,
					.debug_info = block_debug,
					.statements = statements.data,
					.statement_count = statements.count,
				},
			},
			err
		);
		break;
		
	case TOKEN_INT_LIT:
		dynarr_push(
			&node_list,
			&(AstNode) {
				.int_lit = {
					.type = AST_INT_LIT,
					.debug_info = tokens[*index].debug.debug_info,
					.val = tokens[*index].int_lit.val,
				},
			}, err
		);
		if(*err) goto RET;
		break;
		
	default:
		fprintf(stderr, "Error: Expected Expression at ");
		lexer_print_debug_to_file(
			stderr,
			&tokens[*index].debug.debug_info
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

RET:
	*nodes = (AstNode *) node_list.data;
	*node_count = node_list.count;
	return;
}

void parse_type(
	const Token *tokens, size_t token_count, size_t *index,
	AstNode **nodes, size_t *node_count,
	char *const *identifiers,
	Error *err
)
{
	DynArr node_list = {
		.data = (char *) *nodes,
		.elem_size = sizeof(AstNode),
		.count = *node_count,
		.capacity = *node_count,
	};

	switch(tokens[*index].type) {
	case TOKEN_IDENT:
		dynarr_push(
			&node_list,
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
				&node_list,
				&(AstNode) {
					.ident = {
						.type = AST_IDENT,
						.debug_info = tokens[*index].debug.debug_info,
						.id = tokens[*index].ident.id
					},
				}, err
			);
			if(*err) goto RET;

			size_t name = node_list.count - 1;

			*index += 1;

			parse_type(
				tokens, token_count, index, (AstNode **)&node_list.data,
				&node_list.count, identifiers, err
			);
			if(*err) goto RET;

			size_t data_type = node_list.count - 1;

			dynarr_push(&args, &(size_t[2]) { name, data_type }, err);
			if(*err) goto RET;
		}

		if(tokens[++*index].type != TOKEN_ARROW) {
			fprintf(stderr, "Error: Expected '->' found ");
			lexer_print_token_to_file(
				stderr,
				&tokens[*index],
				identifiers
			);
			fprintf(stderr, " instead.\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		DebugInfo location = tokens[*index].debug.debug_info;

		*index += 1;
		
		parse_type(
			tokens, token_count, index,
			(AstNode **) &node_list.data, &node_list.count,
			identifiers, err
		);
		if(*err) goto RET;
		size_t ret_type = node_list.count - 1;

		dynarr_push(
			&node_list,
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
	*nodes = (AstNode *) node_list.data;
	*node_count = node_list.count;
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
		switch(tokens[index].type) {
		case TOKEN_CONST:
			dynarr_push(&node_list,
				&(AstNode) {
					.debug = {
						.type = AST_CONST,
						.debug_info = tokens[index].debug.debug_info,
					},
				},
				err
			);
			if(*err) goto RET;
			size_t access = node_list.count - 1;

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

			if(tokens[++index].type != TOKEN_COLON) {
				fprintf(stderr, "Error: Expected ':', found ");
				lexer_print_token_to_file(
					stderr, &tokens[index], identifiers
				);
				fprintf(stderr, " instead.\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
			DebugInfo debug_info = tokens[index].debug.debug_info;

			index += 1;
			parse_type(
				tokens, token_count, &index, (AstNode **)&node_list.data,
				&node_list.count, identifiers, err	
			);
			if(*err) goto RET;
			size_t data_type = node_list.count - 1;

			size_t initial = 0;

			if(tokens[index + 1].type == TOKEN_ASSIGN) {
				index += 2;
				parse_expr(
					tokens, token_count, &index,
					(AstNode **) &node_list.data, &node_list.count,
					identifiers, err
				);
				if(*err) goto RET;
				initial = node_list.count - 1;
			}

			dynarr_push(
				&node_list,
				&(AstNode) {
					.decl = {
						.type = AST_DECL,
						.debug_info = debug_info,
						.access = access,
						.ident = ident,
						.data_type = data_type,
						.initial = initial,
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
			fprintf(stderr, "Error: Unexpected Token ");
			lexer_print_token_to_file(stderr, &tokens[index], identifiers);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		if(tokens[++index].type != TOKEN_SEMICOLON) {
			fprintf(stderr, "Error: Expected ';', found ");
			lexer_print_token_to_file(stderr, &tokens[index], identifiers);
			fprintf(stderr, " instead.\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
		index += 1;
	}
	
	*nodes = node_list.data;
	*node_count = node_list.count;

	(*nodes)[0] = (AstNode) {
		.module = {
			.type = AST_MODULE,
			.debug_info = {
				.file = (*nodes)[1].debug.debug_info.file,
				.line = 1,
				.col = 1
			},
			.statements = module_statements.data,
			.statement_count = module_statements.count
		}
	};

RET:
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
		case AST_DECL:
		case AST_CONST:
		case AST_IDENT:
		case AST_RET:
		case AST_INT_LIT:
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
			
		case AST_DECL:
			fprintf(
				file,
				"%zi %zi: %zi = %zi",
				nodes[i].decl.access, nodes[i].decl.ident,
				nodes[i].decl.data_type, nodes[i].decl.initial
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
			
		case AST_CONST:
			fprintf(file, "CONST");
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
		}
		fprintf(file, "\n");
	}
}
