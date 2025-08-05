#include "parser.h"

static void parse_expr(
	const Token *tokens, size_t token_count, size_t *index,
	DynArr *nodes,
	char *const *identifiers,
	char *const *strings,
	Error *err
);

static void parse_fn_call(
	const Token *tokens, size_t token_count, size_t *index,
	DynArr *nodes,
	char *const *identifiers,
	char *const *strings,
	Error *err
)
{
	DynArr args;
	dynarr_init(&args, sizeof (size_t));

	// all callers guarentee TokenType
	size_t id = tokens[*index].ident.id;
	DebugInfo debug = tokens[*index].debug.debug_info;

	// skip LPAREN, all callers guarentee TokenType
	*index += 2;

	while(true) {
		parse_expr(tokens, token_count, index, nodes, identifiers, strings, err);
		if(*err) goto RET;

		*index += 1;

		dynarr_push(&args, &(size_t) {nodes->count - 1}, err);
		if(*err) goto RET;

		if(tokens[*index].type == TOKEN_RPAREN) break;

		if(tokens[*index].type != TOKEN_COMMA) {
			fprintf(stderr, "Expected ',', found ");
			lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
			goto RET;
		}

		*index += 1;
	}

	dynarr_push(
		nodes,
		&(AstNode) {
			.fn_call = {
				.type = AST_FN_CALL,
				.debug_info = debug,
				.fn_id = id,
				.arg_count = args.count,
				.args = args.data,
			},
		},
		err
	);
	if(*err) goto RET;

RET:
	if(*err) dynarr_clean(&args);
	return;
}

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

static void op_push(Token op, DynArr *nodes, DynArr *free_terms, DynArr *operator_stack, Error *err)
{
	size_t *rhs = dynarr_pop(free_terms);
	size_t *lhs = NULL;
	if(op.type != TOKEN_AMPERSAND) lhs = dynarr_pop(free_terms);

	if((op.type != TOKEN_AMPERSAND && !lhs) || !rhs) {
		fprintf(stderr, "Malformed Expression (Not Enough Operands) at ");
		lexer_print_debug_to_file(stderr, &op.debug.debug_info);
		fprintf(stderr, "\n");
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

	if(op.type != TOKEN_AMPERSAND) {
		dynarr_push(
			nodes,
			&(AstNode) {
				.binop = {
					.type = binop_tok_to_ast(op.type),
					.debug_info = op.debug.debug_info,
					.lhs = *lhs,
					.rhs = *rhs,
				},
			},
			err
		);
		if(*err) goto RET;
	} else {
		dynarr_push(
			nodes,
			&(AstNode) {
				.addr = {
					.type = AST_ADDR,
					.debug_info = op.debug.debug_info,
					.base = *rhs,
				},
			},
			err
		);
		if(*err) goto RET;
	}

	dynarr_push(free_terms, &(size_t) {nodes->count - 1}, err);
	if(*err) goto RET;

RET:
	return;
}

static void parse_expr(
	const Token *tokens, size_t token_count, size_t *index,
	DynArr *nodes,
	char *const *identifiers,
	char *const *strings,
	Error *err
)
{
	DynArr free_terms;
	dynarr_init(&free_terms, sizeof(size_t));

	//SHUNTING YARD
	DynArr operator_stack;
	dynarr_init(&operator_stack, sizeof(Token));

	bool terminated = false;

	TokenType last_seen = TOKEN_NONE;

	Token *top;
	while(!terminated) {
		top = dynarr_from_back(&operator_stack, 0);
		switch(tokens[*index].type) {
		case TOKEN_LCURLY:
			do {} while(0);
			DynArr elems;
			dynarr_init(&elems, sizeof(size_t));

			DebugInfo array_lit_debug = tokens[*index].debug.debug_info;

			while(tokens[*index].type != TOKEN_RCURLY) {
				*index += 1;
				parse_expr(tokens, token_count, index, nodes, identifiers, strings, err);
				if(*err) goto RET;
				dynarr_push(&elems, &(size_t) { nodes->count - 1}, err);
				if(*err) goto RET;

				*index += 1;

				if(tokens[*index].type != TOKEN_COMMA && tokens[*index].type != TOKEN_RCURLY) {
					fprintf(stderr, "Error: Expected ',' found ");
					lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}
			}

			dynarr_push(
				nodes,
				&(AstNode) {
					.array_lit = {
						.type = AST_ARRAY_LIT,
						.debug_info = array_lit_debug,
						.elem_count = elems.count,
						.elems = elems.data,
					},
				},
				err
			);
			if(*err) goto RET;

			dynarr_push(&free_terms, &(size_t) { nodes->count - 1}, err);
			if(*err) goto RET;
			break;

		case TOKEN_LPAREN:
			dynarr_push(&operator_stack, &tokens[*index], err);
			if(*err) goto RET;
			break;

		case TOKEN_RPAREN:
			while(top && top->type != TOKEN_LPAREN) {
				operator_stack.count -= 1;
				op_push(*top, nodes, &free_terms, &operator_stack, err);
				if(*err) goto RET;

				top = dynarr_from_back(&operator_stack, 0);
			}

			if(!top) {
				terminated = true;
				*index -= 2;
				break;
			}

			operator_stack.count -= 1;
			break;

		case TOKEN_LSQUARE:
			do {} while(0);

			DebugInfo subscript_debug = tokens[*index].debug.debug_info;

			if(!dynarr_from_back(&free_terms, 0)) {
				fprintf(stderr, "Error: Expected Array or Slice for Subscript at ");
				lexer_print_debug_to_file(stderr, &subscript_debug);
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			size_t arr = *(size_t*)dynarr_pop(&free_terms);
			
			*index += 1;
			parse_expr(tokens, token_count, index, nodes, identifiers, strings, err);
			if(*err) goto RET;

			*index += 1;

			if(tokens[*index].type != TOKEN_RSQUARE) {
				fprintf(stderr, "Error: Expected ']' after '[', found ");
				lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			size_t arr_index = nodes->count - 1;

			dynarr_push(
				nodes,
				&(AstNode) {
					.subscript = {
						.type = AST_SUBSCRIPT,
						.debug_info = subscript_debug,
						.arr = arr,
						.index = arr_index,
					},
				},
				err
			);
			if(*err) goto RET;

			dynarr_push(&free_terms, &(size_t) {nodes->count - 1}, err);
			if(*err) goto RET;
			break;

		case TOKEN_PERIOD:
			do {} while(0);

			const DebugInfo struct_access_debug = tokens[*index].debug.debug_info;

			size_t *struct_parent = dynarr_pop(&free_terms);
			if(!struct_parent) {
				fprintf(stderr, "Expected Term before '.' at ");
				lexer_print_debug_to_file(stderr, &struct_access_debug);
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			*index += 1;

			if(tokens[*index].type != TOKEN_IDENT) {
				fprintf(stderr, "Expected Identifier after '.', found ");
				lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			size_t member_id = tokens[*index].ident.id;

			dynarr_push(
				nodes,
				&(AstNode) {
					.struct_access = {
						.type = AST_STRUCT_ACCESS,
						.debug_info = struct_access_debug,
						.parent = *struct_parent,
						.member_id = member_id
					},
				},
				err
			);
			if(*err) goto RET;

			dynarr_push(&free_terms, &(size_t){nodes->count - 1}, err);
			if(*err) goto RET;
			break;

		case TOKEN_AMPERSAND:
			while(
				top
				&& (top->type == TOKEN_PERIOD
					|| top->type == TOKEN_AMPERSAND
				)
			) {
					operator_stack.count -= 1;

					op_push(
						*top,
						nodes,
						&free_terms,
						&operator_stack,
						err
					);
					if(*err) goto RET;
					top = dynarr_from_back(&operator_stack, 0);
			}

			DebugInfo addr_debug = tokens[*index].debug.debug_info;

			if(tokens[*index + 1].type != TOKEN_IDENT) {
				fprintf(stderr, "Expected Identifier after '&', found ");
				lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			dynarr_push(&operator_stack, &tokens[*index], err);
			if(*err) goto RET;
			break;

		case TOKEN_STAR:
			if(
				last_seen == TOKEN_STAR
				|| last_seen == TOKEN_FSLASH
				|| last_seen == TOKEN_PLUS
				|| last_seen == TOKEN_MINUS
				|| last_seen == TOKEN_NONE
			) {
				DebugInfo debug = tokens[*index].debug.debug_info;

				*index += 1;

				switch(tokens[*index].type) {
				case TOKEN_IDENT:
					dynarr_push(
						nodes,
						&(AstNode) {
							.ident = {
								.type = AST_IDENT,
								.debug_info = tokens[*index].ident.debug_info,
								.id = tokens[*index].ident.id,
							},
						},
						err
					);
					if(*err) goto RET;

					size_t ident_target = nodes->count - 1;

					dynarr_push(
						nodes,
						&(AstNode) {
							.deref = {
								.type = AST_DEREF,
								.debug_info = debug,
								.ptr = ident_target,
							},
						},
						err
					);
					if(*err) goto RET;

					dynarr_push(&free_terms, &(size_t) {nodes->count - 1}, err);
					if(*err) goto RET;
					break;

				case TOKEN_LPAREN:
					*index += 1;

					parse_expr(tokens, token_count, index, nodes, identifiers, strings, err);
					if(*err) goto RET;

					*index += 1;

					if(tokens[*index].type != TOKEN_RPAREN) {
						fprintf(stderr, "Expected Closed Parentheses for Target of Dereference, found ");
						lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
						*err = ERROR_UNEXPECTED_DATA;
						goto RET;
					}

					size_t paren_target = nodes->count - 1;

					dynarr_push(
						nodes,
						&(AstNode) {
							.deref = {
								.type = AST_DEREF,
								.debug_info = debug,
								.ptr = paren_target,
							},
						},
						err
					);
					if(*err) goto RET;

					dynarr_push(&free_terms, &(size_t) { nodes->count - 1 }, err);
					if(*err) goto RET;
					break;

				default:
					fprintf(stderr, "Invalid Target for Dereference: ");
					lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}

				break;
			}

			while(
				top
				&& (top->type == TOKEN_AMPERSAND || top->type == TOKEN_STAR || top->type == TOKEN_FSLASH)
			) {
					operator_stack.count -= 1;

					op_push(*top, nodes, &free_terms, &operator_stack, err);
					if(*err) goto RET;
					top = dynarr_from_back(&operator_stack, 0);
			}

			dynarr_push(&operator_stack, &tokens[*index], err);
			if(*err) goto RET;

			break;

		case TOKEN_FSLASH:
			if(
				last_seen == TOKEN_STAR
				|| last_seen == TOKEN_FSLASH
				|| last_seen == TOKEN_PLUS
				|| last_seen == TOKEN_MINUS
			) {
				fprintf(stderr, "Malformed Expression. Unexpected ");
				lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			while(
				top
				&& (top->type == TOKEN_AMPERSAND || top->type == TOKEN_STAR || top->type == TOKEN_FSLASH)
			) {
					operator_stack.count -= 1;
					op_push(*top, nodes, &free_terms, &operator_stack, err);
					if(*err) goto RET;
					top = dynarr_from_back(&operator_stack, 0);
			}

			dynarr_push(&operator_stack, &tokens[*index], err);
			if(*err) goto RET;

			break;

		case TOKEN_PLUS:
			if(
				last_seen == TOKEN_STAR
				|| last_seen == TOKEN_FSLASH
				|| last_seen == TOKEN_PLUS
				|| last_seen == TOKEN_MINUS
			) {
				fprintf(stderr, "Malformed Expression. Unexpected ");
				lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			while(
				top &&
				(
					top->type == TOKEN_AMPERSAND
					|| top->type == TOKEN_STAR || top->type == TOKEN_FSLASH
					|| top->type == TOKEN_PLUS || top->type == TOKEN_MINUS
				)
			) {
					operator_stack.count -= 1;
					op_push(*top, nodes, &free_terms, &operator_stack, err);
					if(*err) goto RET;
					top = dynarr_from_back(&operator_stack, 0);
			}

			dynarr_push(&operator_stack, &tokens[*index], err);
			if(*err) goto RET;

			break;

		case TOKEN_MINUS:
			if(
				last_seen == TOKEN_STAR
				|| last_seen == TOKEN_FSLASH
				|| last_seen == TOKEN_PLUS
				|| last_seen == TOKEN_MINUS
			) {
				fprintf(stderr, "Malformed Expression. Unexpected ");
				lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			while(
				top &&
				(
					top->type == TOKEN_STAR || top->type == TOKEN_FSLASH
					|| top->type == TOKEN_PLUS || top->type == TOKEN_MINUS
				)
			) {
					operator_stack.count -= 1;
					op_push(*top, nodes, &free_terms, &operator_stack, err);
					if(*err) goto RET;
					top = dynarr_from_back(&operator_stack, 0);
			}

			dynarr_push(&operator_stack, &tokens[*index], err);
			if(*err) goto RET;

			break;

		case TOKEN_UNDERSCORE:
			do {} while(0);

			DebugInfo underscore_debug = tokens[*index].debug.debug_info;
			*index += 1;

			if(tokens[*index].type != TOKEN_LCURLY) {
				fprintf(stderr, "Expected '{' to start Compound Literal, found ");
				lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			DynArr names, vals;
			dynarr_init(&names, sizeof(size_t));
			dynarr_init(&vals, sizeof(size_t));

			while(true) {
				*index += 1;

				if(tokens[*index].type != TOKEN_PERIOD) {
					fprintf(stderr, "Expected '.' in Compound Literal, found ");
					lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
					fprintf(stderr, "\n");
					*err = ERROR_UNEXPECTED_DATA;
					goto UNDERSCORE_CLEAN;
				}

				*index += 1;

				if(tokens[*index].type != TOKEN_IDENT) {
					fprintf(stderr, "Expected Identfier in Compound Literal, found ");
					lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
					fprintf(stderr, "\n");
					*err = ERROR_UNEXPECTED_DATA;
					goto UNDERSCORE_CLEAN;
				}

				dynarr_push(&names, &tokens[*index].ident.id, err);
				if(*err) goto UNDERSCORE_CLEAN;

				*index += 1;

				if(tokens[*index].type != TOKEN_ASSIGN) {
					fprintf(stderr, "Expected '=' in Compound Literal, found ");
					lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
					fprintf(stderr, "\n");
					*err = ERROR_UNEXPECTED_DATA;
					goto UNDERSCORE_CLEAN;
				}

				*index += 1;

				parse_expr(tokens, token_count, index, nodes, identifiers, strings, err);
				if(*err) goto UNDERSCORE_CLEAN;

				dynarr_push(&vals, &(size_t) { nodes->count - 1 }, err);
				if(*err) goto UNDERSCORE_CLEAN;

				*index += 1;

				if(tokens[*index].type != TOKEN_COMMA) {
					if(tokens[*index].type == TOKEN_RCURLY) {
						break;
					}

					fprintf(stderr, "Expected ',' in Compound Literal, found ");
					lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
					fprintf(stderr, "\n");
					*err = ERROR_UNEXPECTED_DATA;
					goto UNDERSCORE_CLEAN;
				}
			}

			dynarr_push(
				nodes,
				&(AstNode) {
					.struct_lit = {
						.type = AST_STRUCT_LIT,
						.debug_info = underscore_debug,
						.member_count = names.count,
						.member_name_ids = names.data,
						.member_values = vals.data,
					},
				},
				err
			);
			if(*err) goto RET;

			dynarr_push(&free_terms, &(size_t) {nodes->count - 1}, err);
			if(*err) goto UNDERSCORE_CLEAN;
			break;


UNDERSCORE_CLEAN:
			dynarr_clean(&names);
			dynarr_clean(&vals);
			goto RET;

		case TOKEN_STRING_LIT:
		case TOKEN_ZSTRING_LIT:
		case TOKEN_CSTRING_LIT:
			do {} while(0);

			const AstNodeType strlit_types[] = {
				AST_STRING_LIT,
				AST_ZSTRING_LIT,
				AST_CSTRING_LIT,
			};
			
			const AstNodeType strlit_type = strlit_types[tokens[*index].type - TOKEN_STRING_LIT];

			dynarr_push(
				nodes,
				&(AstNode) {
					.string_lit = {
						.type = strlit_type,
						.debug_info = tokens[*index].debug.debug_info,
						.id = tokens[*index].string_lit.id,				
					},
				},
				err
			);
			if(*err) goto RET;
			dynarr_push(&free_terms, &(size_t) {nodes->count - 1}, err);
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
			if(tokens[*index + 1].type == TOKEN_LPAREN) {
				parse_fn_call(tokens, token_count, index, nodes, identifiers, strings, err);
				if(*err) goto RET;
			} else {
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
			}
			dynarr_push(&free_terms, &(size_t) {nodes->count - 1}, err);
			if(*err) goto RET;
			break;

		case TOKEN_SEMICOLON:
		case TOKEN_COMMA:
		case TOKEN_RSQUARE:
		case TOKEN_RCURLY:
			terminated = true;
			*index -= 2;
			break;

		default:
			fprintf(stderr, "Unexpected ");
			lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		last_seen = tokens[*index].type;

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

		op_push(*top, nodes, &free_terms, &operator_stack, err);

		if(*err) goto RET;

		top = dynarr_from_back(&operator_stack, 0);
	}

	if(free_terms.count != 1) {
		fprintf(stderr, "Malformed Expression (too many operands) at ");
		lexer_print_debug_to_file(stderr, &((AstNode*) dynarr_from_back(nodes, 0))->debug.debug_info);
		fprintf(stderr, "\n");
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}


RET:
	dynarr_clean(&free_terms);
	dynarr_clean(&operator_stack);
}

static void parse_type(
	const Token *tokens, size_t token_count, size_t *index,
	DynArr *nodes,
	char *const *identifiers,
	char *const *strings,
	Error *err
);

static void parse_block(
	const Token *tokens, size_t token_count, size_t *index,
	DynArr *nodes,
	char *const *identifiers,
	char *const *strings,
	Error *err
)
{
	DynArr statements;
	dynarr_init(&statements, sizeof(size_t));

	if(tokens[*index].type != TOKEN_LCURLY) {
		fprintf(stderr, "Expected '{', found ");
		lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
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
			do {} while(0);

			size_t return_val = 0;
			if(tokens[*index + 1].type != TOKEN_SEMICOLON) {
				*index += 1;
				parse_expr(tokens, token_count, index, nodes, identifiers, strings, err);
				if(*err) goto RET;

				return_val = nodes->count - 1;
			} 

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
		case TOKEN_VAR:
			do {} while(0);
			bool mut = (tokens[*index].type == TOKEN_VAR);
			*index += 1;

			if(tokens[*index].type != TOKEN_IDENT) {
				fprintf(stderr, "Expected Identifier, found ");
				lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			size_t id = tokens[*index].ident.id;
			
			*index += 1;

			if(tokens[*index].type != TOKEN_COLON) {
				fprintf(stderr, "Expected ':', found ");
				lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			*index += 1;

			parse_type(tokens, token_count, index, nodes, identifiers, strings, err);
			if(*err) goto RET;

			size_t data_type = nodes->count - 1; 

			size_t initial = 0;
			if(tokens[*index + 1].type == TOKEN_ASSIGN) {
				*index += 2;
				parse_expr(tokens, token_count, index, nodes, identifiers, strings, err);
				if(*err) goto RET;

				initial = nodes->count - 1;
			}

			dynarr_push(
				nodes,
				&(AstNode) {
					.var_decl = {
						.type = AST_VAR_DECL,
						.debug_info = debug,
						.mut = mut,
						.id = id,
						.data_type = data_type,
						.initial = initial,
					},
				},
				err
			);
			if(*err) goto RET;

			break;
			
		case TOKEN_STAR:
			*index += 1;

			switch(tokens[*index].type) {
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

				size_t ident_target = nodes->count - 1;

				dynarr_push(
					nodes,
					&(AstNode) {
						.deref = {
							.type = AST_DEREF,
							.debug_info = debug_info,
							.ptr = ident_target,
						},
					},
					err
				);
				if(*err) goto RET;
				break;

			case TOKEN_LPAREN:
				*index += 1;

				parse_expr(tokens, token_count, index, nodes, identifiers, strings, err);
				if(*err) goto RET;

				*index += 1;

				if(tokens[*index].type != TOKEN_RPAREN) {
					fprintf(stderr, "Expected ')', found ");
					lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}

				size_t paren_target = nodes->count - 1;

				dynarr_push(
					nodes,
					&(AstNode) {
						.deref = {
							.type = AST_DEREF,
							.debug_info = debug_info,
							.ptr = paren_target,
						},
					},
					err
				);
				if(*err) goto RET;
				break;

			default:
				fprintf(stderr, "Invalid Target for Dereference: ");
				lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			size_t lhs = nodes->count - 1;
			*index += 1;

			AstNodeType type;
			switch(tokens[*index].type) {
			case TOKEN_ASSIGN:
				type = AST_ASSIGN;
				break;
			case TOKEN_ADD_ASSIGN:
				type = AST_ADD_ASSIGN;
				break;
			case TOKEN_SUB_ASSIGN:
				type = AST_SUB_ASSIGN;
				break;
			case TOKEN_MUL_ASSIGN:
				type = AST_MUL_ASSIGN;
				break;
			case TOKEN_DIV_ASSIGN:
				type = AST_DIV_ASSIGN;
				break;
			default:
				fprintf(stderr, "Expected Assignment, found ");
				lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			DebugInfo assign_debug = tokens[*index].debug.debug_info;

			*index += 1;
			parse_expr(tokens, token_count, index, nodes, identifiers, strings, err);
			if(*err) goto RET;

			size_t expr = nodes->count - 1;

			dynarr_push(
				nodes,
				&(AstNode) {
					.assign = {
						.type = type,
						.debug_info = assign_debug,
						.var = lhs,
						.expr = expr,
					},
				},
				err
			);
			if(*err) goto RET;
			break;

		case TOKEN_IDENT:
			switch(tokens[*index + 1].type) {
			case TOKEN_LPAREN:
				parse_fn_call(tokens, token_count, index, nodes, identifiers, strings, err);
				if(*err) goto RET;
				break;

			case TOKEN_ASSIGN:
			case TOKEN_ADD_ASSIGN:
			case TOKEN_SUB_ASSIGN:
			case TOKEN_MUL_ASSIGN:
			case TOKEN_DIV_ASSIGN:
				do {} while(0);

				AstNodeType type;
				switch(tokens[*index + 1].type) {
				default: //silence compiler warnings
				case TOKEN_ASSIGN:
					type = AST_ASSIGN;
					break;
				case TOKEN_ADD_ASSIGN:
					type = AST_ADD_ASSIGN;
					break;
				case TOKEN_SUB_ASSIGN:
					type = AST_SUB_ASSIGN;
					break;
				case TOKEN_MUL_ASSIGN:
					type = AST_MUL_ASSIGN;
					break;
				case TOKEN_DIV_ASSIGN:
					type = AST_DIV_ASSIGN;
					break;
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

				*index += 2;

				size_t var = nodes->count - 1;

				parse_expr(tokens, token_count, index, nodes, identifiers, strings, err);
				if(*err) goto RET;

				size_t expr = nodes->count - 1;

				dynarr_push(
					nodes,
					&(AstNode) {
						.assign = {
							.type = type,
							.debug_info = debug,
							.var = var,
							.expr = expr,
						},
					},
					err
				);
				if(*err) goto RET;
				break;


			case TOKEN_PLUS:
			case TOKEN_MINUS:
			case TOKEN_FSLASH:
				fprintf(stderr, "Expected Statement, found Expression ");
				lexer_print_token_to_file(stderr, &tokens[*index + 1], identifiers, strings);
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;

			default:
				fprintf(stderr, "Unexpected ");
				lexer_print_token_to_file(stderr, &tokens[*index + 1], identifiers, strings);
				fprintf(stderr, "\n");
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}
			break;

		case TOKEN_DISCARD:
			*index += 1;

			parse_expr(tokens, token_count, index, nodes, identifiers, strings, err);
			if(*err) goto RET;

			size_t discarded_value = nodes->count - 1;

			dynarr_push(
				nodes,
				&(AstNode) {
					.discard = {
						.type = AST_DISCARD,
						.debug_info = debug,
						.value = discarded_value,
					},
				}, err
			);
			if(*err) goto RET;

			break;

		default:
			fprintf(stderr, "Unexpected ");
			lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
			fprintf(stderr, "\n");
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		if(tokens[++*index].type != TOKEN_SEMICOLON) {
			fprintf(stderr, "Expected ';', found ");
			lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
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
	char *const *strings,
	Error *err
)
{
	DynArr args;
	dynarr_init(&args, 2 * sizeof(size_t));
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

	case TOKEN_STRUCT:
		*index += 1;
		
		if(tokens[*index].type != TOKEN_LCURLY) {
			fprintf(stderr, "Expected '{' after 'struct', found ");
			lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
		
		DynArr names, types;
		dynarr_init(&names, sizeof(size_t));
		dynarr_init(&types, sizeof(size_t));

		while(true) {
			*index += 1;

			if(tokens[*index].type == TOKEN_RCURLY) break;

			if(tokens[*index].type != TOKEN_IDENT) {
				fprintf(stderr, "Expected Identifier for Struct Member Name, found ");
				lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
				*err = ERROR_UNEXPECTED_DATA;
				goto TOKEN_STRUCT_CLEAN;
			}

			dynarr_push(&names, &tokens[*index].ident.id, err);
			if(*err) goto TOKEN_STRUCT_CLEAN;

			*index += 1;

			if(tokens[*index].type != TOKEN_COLON) {
				fprintf(stderr, "Expected ':' in Struct Member Declaration, found ");
				*err = ERROR_UNEXPECTED_DATA;
		   		goto TOKEN_STRUCT_CLEAN;
		 	}

			*index += 1;

			parse_type(tokens, token_count, index, nodes, identifiers, strings, err);
			if(*err) goto TOKEN_STRUCT_CLEAN;

			dynarr_push(&types, &(size_t) {nodes->count - 1}, err);
			if(*err) goto TOKEN_STRUCT_CLEAN;

			*index += 1;

			if(tokens[*index].type == TOKEN_RCURLY) break;

			if(tokens[*index].type != TOKEN_COMMA) {
				fprintf(stderr, "Expected ',' after Struct Member Declaration, found ");
				*err = ERROR_UNEXPECTED_DATA;
				goto TOKEN_STRUCT_CLEAN;
			}
		}

TOKEN_STRUCT_CLEAN:
		if(*err) {
			dynarr_clean(&names);
			dynarr_clean(&types);
			goto RET;
		}

		dynarr_push(
			nodes,
			&(AstNode) {
				.struct_type = {
					.type = AST_STRUCT_TYPE,
					.debug_info = location,
					.member_count = names.count,
					.member_name_ids = names.data,
					.member_types = types.data,
				},
			},
			err
		);
		if(*err) goto RET;

		break;

	case TOKEN_AMPERSAND:
		do {} while(0);
		AstNodeType access_modifier;
		*index += 1;
		switch(tokens[*index].type) {
		case TOKEN_CONST:
			access_modifier = AST_POINTER_CONST;
			break;
		case TOKEN_VAR:
			access_modifier = AST_POINTER_VAR;
			break;
		case TOKEN_ABYSS:
			access_modifier = AST_POINTER_ABYSS;
			break;
		default:
			fprintf(stderr, "Expected Access Modifier for Pointer Type found ");
			lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}

		*index += 1;

		parse_type(tokens, token_count, index, nodes, identifiers, strings, err);
		if(*err) goto RET;

		size_t base_type = nodes->count - 1;

		dynarr_push(
			nodes,
			&(AstNode) {
				.pointer_type = {
					.type = access_modifier,
					.debug_info = location,
					.base_type = base_type,
				},
			},
			err
		);
		if(*err) goto RET;
		break;

	case TOKEN_LSQUARE:
		*index += 1;
		switch(tokens[*index].type) {
		case TOKEN_RSQUARE:
			*index += 1;

			AstNodeType access;
			switch(tokens[*index].type) {
			case TOKEN_CONST:
				access = AST_SLICE_CONST;
				break;
			case TOKEN_VAR:
				access = AST_SLICE_VAR;
				break;
			case TOKEN_ABYSS:
				access = AST_SLICE_ABYSS;
				break;
			default:
				fprintf(stderr, "Error: Expected Access Modifier for Slice Type, found ");
				lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			*index += 1;

			parse_type(tokens, token_count, index, nodes, identifiers, strings, err);
			if(*err) goto RET;

			size_t slice_base = nodes->count - 1;

			dynarr_push(
				nodes,
				&(AstNode) {
					.slice = {
						.type = access,
						.debug_info = location,
						.elem_type = slice_base,
					},
				},
				err
			);
			if(*err) goto RET;
			break;

		case TOKEN_INT_LIT:
			if(tokens[*index].int_lit.val == 0) {
				fprintf(stderr, "Error: Cannot have Zero-Length Array at ");
				lexer_print_debug_to_file(stderr, &tokens[*index].debug.debug_info);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			const intmax_t len = tokens[*index].int_lit.val;

			*index += 1;

			if(tokens[*index].type != TOKEN_RSQUARE) {
				fprintf(stderr, "Error: Expected ']', found ");
				lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			*index += 1;

			parse_type(tokens, token_count, index, nodes, identifiers, strings, err);
			if(*err) goto RET;

			size_t arr_type = nodes->count - 1;

			dynarr_push(
				nodes,
				&(AstNode) {
					.array = {
						.type = AST_ARRAY,
						.debug_info = location,
						.elem_type = arr_type,
						.len = len,
					},
				},
				err
			);
			if(*err) goto RET;
			break;

		case TOKEN_UNDERSCORE:
			*index += 1;

			if(tokens[*index].type != TOKEN_RSQUARE) {
				fprintf(stderr, "Error: Expected ']', found ");
				lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			*index += 1;

			parse_type(tokens, token_count, index, nodes, identifiers, strings, err);
			if(*err) goto RET;

			arr_type = nodes->count - 1;

			dynarr_push(
				nodes,
				&(AstNode) {
					.array = {
						.type = AST_ARRAY,
						.debug_info = location,
						.elem_type = arr_type,
						.len = 0,
					},
				},
				err
			);
			if(*err) goto RET;
			break;

		default:
			fprintf(stderr, "Error: Expected Array or Slice Type, found ");
			lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
			*err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
		break;

	case TOKEN_LPAREN:
		*index += 1;
		while(tokens[*index].type != TOKEN_RPAREN) {
			if(tokens[*index].type != TOKEN_IDENT) {
				fprintf(stderr, "Error: Expected Identifier found ");
				lexer_print_token_to_file(
					stderr,
					&tokens[*index],
					identifiers,
					strings
				);
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

			if(tokens[*index].type != TOKEN_COLON) {
				fprintf(stderr, "Expected ':', found ");
				lexer_print_token_to_file(stderr, &tokens[*index], identifiers, strings);
				*err = ERROR_UNEXPECTED_DATA;
				goto RET;
			}

			*index += 1;

			parse_type(
				tokens, token_count, index, nodes, identifiers, strings, err
			);
			if(*err) goto RET;

			size_t data_type = nodes->count - 1;

			dynarr_push(&args, &(size_t[2]) { name, data_type }, err);
			if(*err) goto RET;

			*index += 1;

			if(tokens[*index].type == TOKEN_RPAREN) break;

			*index += 1;
		}


		*index += 1;
		
		parse_type(
			tokens, token_count, index, nodes, identifiers, strings, err
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
			identifiers,
			strings
		);
		*err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}
RET:
	if(*err) dynarr_clean(&args);
	return;
}

void parser_gen_ast(
	Token const *tokens, size_t token_count,
	AstNode **nodes, size_t *node_count,
	char *const *identifiers,
	char *const *strings,
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
					stderr,
					&tokens[index],
					identifiers,
					strings
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
				tokens, token_count, &index, &node_list, identifiers, strings, err
			);
			if(*err) goto RET;
			size_t fn_type = node_list.count - 1;

			index += 1;

			if(tokens[index].type == TOKEN_HASH_EXTERN) {
				const DebugInfo extern_debug = tokens[index].debug.debug_info;
				index += 1;
				if(tokens[index].type != TOKEN_LPAREN) {
					fprintf(stderr, "Error: Expected '(' at ");
					lexer_print_debug_to_file(stderr, &tokens[index].debug.debug_info);
					fprintf(stderr, "\n");
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}

				index += 1;

				if(tokens[index].type != TOKEN_STRING_LIT) {
					fprintf(stderr, "Error: Expected String Literal at ");
					lexer_print_debug_to_file(stderr, &tokens[index].debug.debug_info);
					fprintf(stderr, "\n");
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}

				dynarr_push(
					&node_list,
					&(AstNode) {
						.string_lit = {
							.type = AST_STRING_LIT,
							.debug_info = tokens[index].debug.debug_info,
							.id = tokens[index].string_lit.id,
						},
					},
					err		
				);
				if(*err) goto RET;
				const size_t name = node_list.count - 1;

				index += 1;

				if(tokens[index].type != TOKEN_RPAREN) {
					fprintf(stderr, "Error: Expected ')' at ");
					lexer_print_debug_to_file(stderr, &tokens[index].debug.debug_info);
					fprintf(stderr, "\n");
					*err = ERROR_UNEXPECTED_DATA;
					goto RET;
				}

				dynarr_push(
					&node_list,
					&(AstNode) {
						.extrn = {
							.type = AST_EXTERN,
							.debug_info = extern_debug,
							.name = name,
						},
					},
					err		
				);
				if(*err) goto RET;
			} else {
				parse_block(
					tokens, token_count, &index, &node_list, identifiers, strings, err
				);
				if(*err) goto RET;
			}

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
				stderr, &tokens[index], identifiers, strings
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

		case AST_FN_CALL:
			free(nodes[i].fn_call.args);
			break;

		case AST_ARRAY_LIT:
			free(nodes[i].array_lit.elems);
			break;

		case AST_STRUCT_TYPE:
			free(nodes[i].struct_type.member_name_ids);
			free(nodes[i].struct_type.member_types);
			break;

		case AST_STRUCT_LIT:
			free(nodes[i].struct_lit.member_name_ids);
			free(nodes[i].struct_lit.member_values);
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
			break;
		}
	}
}

void parser_print_ast_to_file(
	FILE *file,
	AstNode *nodes,
	size_t node_count,
	char *const *identifiers,
	char *const *strings
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

		case AST_FN_CALL:
			fprintf(file, "%s(", identifiers[nodes[i].fn_call.fn_id]);
			for(size_t j = 0; j < nodes[i].fn_call.arg_count; j++) {
				fprintf(file, "%ji, ", nodes[i].fn_call.args[j]);
			}
			fprintf(file, ")");
			break;

		case AST_ASSIGN:
			fprintf(file, "%zi = %zi", nodes[i].assign.var, nodes[i].assign.expr);
			break;

		case AST_ADD_ASSIGN:
			fprintf(file, "%zi += %zi", nodes[i].assign.var, nodes[i].assign.expr);
			break;

		case AST_SUB_ASSIGN:
			fprintf(file, "%zi -= %zi", nodes[i].assign.var, nodes[i].assign.expr);
			break;

		case AST_MUL_ASSIGN:
			fprintf(file, "%zi *= %zi", nodes[i].assign.var, nodes[i].assign.expr);
			break;

		case AST_DIV_ASSIGN:
			fprintf(file, "%zi /= %zi", nodes[i].assign.var, nodes[i].assign.expr);
			break;

		case AST_ADDR:
			fprintf(file, "&%zi", nodes[i].addr.base);
			break;
		
		case AST_POINTER_CONST:
			fprintf(file, "&const %zi", nodes[i].pointer_type.base_type);
			break;

		case AST_POINTER_VAR:
			fprintf(file, "&var %zi", nodes[i].pointer_type.base_type);
			break;

		case AST_POINTER_ABYSS:
			fprintf(file, "&abyss %zi", nodes[i].pointer_type.base_type);
			break;

		case AST_DEREF:
			fprintf(file, "*%zi", nodes[i].deref.ptr);
			break;

		case AST_ARRAY:
			fprintf(file, "[%zi]%zi", nodes[i].array.len, nodes[i].array.elem_type);
			break;

		case AST_SLICE_CONST:
			fprintf(file, "[]const %zi", nodes[i].slice.elem_type);
			break;

		case AST_SLICE_VAR:
			fprintf(file, "[]var %zi", nodes[i].slice.elem_type);
			break;

		case AST_SLICE_ABYSS:
			fprintf(file, "[]abyss %zi", nodes[i].slice.elem_type);
			break;

		case AST_SUBSCRIPT:
			fprintf(file, "%zi[%zi]", nodes[i].subscript.arr, nodes[i].subscript.index);
			break;

		case AST_ARRAY_LIT:
			fprintf(file, "{");
			for(size_t j = 0; j < nodes[i].array_lit.elem_count; j++) {
				fprintf(file, "%zi, ", nodes[i].array_lit.elems[j]);
			}
			fprintf(file, "}");
			break;

		case AST_STRUCT_TYPE:
			fprintf(file, "struct {");
			for(size_t j = 0; j < nodes[i].struct_type.member_count; j++) {
				fprintf(
					file,
					"%s: %zi, ",
					identifiers[nodes[i].struct_type.member_name_ids[j]],
					nodes[i].struct_type.member_types[j]
				);
			}
			fprintf(file, "}");
			break;

		case AST_STRUCT_LIT:
			fprintf(file, "(struct) {");
			for(size_t j = 0; j < nodes[i].struct_lit.member_count; j++) {
				fprintf(
					file,
					".%s = %zi",
					identifiers[nodes[i].struct_lit.member_name_ids[j]],
					nodes[i].struct_lit.member_values[j]
				);
			}
			fprintf(file, "}");
			break;

		case AST_STRUCT_ACCESS:
			fprintf(
				file,
				"%zi.%zi",
				nodes[i].struct_access.parent,
				nodes[i].struct_access.member_id
			);
			break;

		case AST_STRING_LIT:
			fprintf(
				file,
				"String \"%s\"",
				strings[nodes[i].string_lit.id]
			);
			break;
		case AST_ZSTRING_LIT:
			fprintf(
				file,
				"ZString \"%s\"",
				strings[nodes[i].string_lit.id]
			);
			break;
		case AST_CSTRING_LIT:
			fprintf(
				file,
				"CString \"%s\"",
				strings[nodes[i].string_lit.id]
			);
			break;
		case AST_EXTERN:
			fprintf(
				file,
				"#extern(%zi)",
				nodes[i].extrn.name
			);
			break;
		case AST_DISCARD:
			fprintf(
				file,
				"discard %zi",
				nodes[i].discard.value
			);
			break;
		}
		fprintf(file, "\n");
	}
}
