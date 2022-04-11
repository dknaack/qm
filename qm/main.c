#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <qm/types.h>
#include <qm/tex.h>


static const char *token_name[QM_TOKEN_COUNT] = {
	[QM_TOKEN_INVALID]    = "INVALID",
	[QM_TOKEN_EOF]        = "EOF",
	[QM_TOKEN_LPAREN]     = "LPAREN",
	[QM_TOKEN_RPAREN]     = "RPAREN",
	[QM_TOKEN_LBRACKET]   = "LBRACKET",
	[QM_TOKEN_RBRACKET]   = "RBRACKET",
	[QM_TOKEN_LBRACE]     = "LBRACE",
	[QM_TOKEN_RBRACE]     = "RBRACE",
	[QM_TOKEN_COMMA]      = "COMMA",
	[QM_TOKEN_NEWLINE]    = "NEWLINE",
	[QM_TOKEN_IDENTIFIER] = "IDENTIFIER",
	[QM_TOKEN_NUMBER]     = "NUMBER",
	[QM_TOKEN_STRING]     = "STRING",
	[QM_TOKEN_RAW_STRING] = "RAW_STRING",
	[QM_TOKEN_OP]         = "OP",
	[QM_TOKEN_OPR]        = "OPR",
	[QM_TOKEN_OPP]        = "OPP",
};

static struct qm_memory_block *
memory_block_create(usize size)
{
	usize block_size = MAX(size, 8192);
	struct qm_memory_block *block = calloc(block_size + sizeof(*block), 1);
	if (!block) {
		perror("calloc");
		exit(EXIT_FAILURE);
	}

	block->size = block_size;
	block->data = block + 1;
	return block;
}

static void *
arena_alloc_(struct qm_memory_arena *arena, usize size)
{
	struct qm_memory_block *block = arena->block;
	if (!arena->block || block->used + size > block->size) {

		block = memory_block_create(size);
		block->prev = arena->block;
		arena->block = block;
	}

	assert(block->used + size <= block->size);
	void *ptr = (u8 *)block->data + block->used;
	block->used += size;

	return ptr;
}

static void
arena_finish(struct qm_memory_arena *arena)
{
	struct qm_memory_block *block = arena->block;

	while (block) {
		struct qm_memory_block *tmp = block->prev;
		free(block);
		block = tmp;
	}
}

static usize
string_length(const u8 *string)
{
	u32 length = 0;

	if (string) {
		while (*string++) {
			length++;
		}
	}

	return length;
}

static bool
string_equals(const u8 *a, const u8 *b)
{
	if (!a) {
		return false;
	} else if (!b) {
		return false;
	} else {
		while (*a && *b && *a == *b) {
			a++;
			b++;
		}

		return *a == *b;
	}
}

static u32
hash(const u8 *op)
{
	u32 h = 5381;

	if (op) {
		while (*op) {
			h = (h << 5) + h + *op++;
		}
	}

	return h;
}

#include "debug.c"
#include "tex.c"
#include "pandoc.c"

static bool
file_read(const char *filename, struct qm_memory_arena *arena,
		struct qm_buffer *buffer)
{
	FILE *f = stdin;
	if (filename && !(f = fopen(filename, "r"))) {
		return false;
	}

	u32 size = 2 * BUFSIZ;
	u8 *data = calloc(size, 1);
	if (!data) {
		return false;
	}

	u32 n, length = 0;
	while ((n = fread(data + length, 1, BUFSIZ, f))) {
		length += n;
		if (length + BUFSIZ + 1 >= size) {
			size *= 2;
			if (!(data = realloc(data, size))) {
				return false;
			}
		}
	}

	data[length] = '\0';
	buffer->data = data;
	buffer->size = length;
	buffer->start = 0;

	fclose(f);
	return true;
}

static bool
operator_define(struct qm_operator_table *operators,
		struct qm_memory_arena *arena, u8 *op, i32 lbp, i32 rbp)
{
	assert(op);

	if (!operators->keys) {
		operators->used = 0;
		operators->size = 1024;
		operators->keys = arena_alloc(arena, operators->size, u8 *);
		operators->lbp  = arena_alloc(arena, operators->size, i32);
		operators->rbp  = arena_alloc(arena, operators->size, i32);
	}

	u8 **keys = operators->keys;
	i32 *lbps = operators->lbp;
	i32 *rbps = operators->rbp;
	u32 size = operators->size;
	u32 mask = size - 1;

	u32 i = hash(op) & mask;
	while (size-- > 0) {
		if (!keys[i]) {
			keys[i] = op;
			lbps[i] = lbp;
			rbps[i] = rbp;
			return true;
		} else if (string_equals(keys[i], op)) {
			// NOTE: operator was redefined.
			return false;
		}

		i = (i + 1) & mask;
	}

	return false;
}

static bool
operator_find(struct qm_operator_table *operators, u8 *op,
		i32 *lbp, i32 *rbp)
{
	u8 **keys = operators->keys;
	i32 *lbps = operators->lbp;
	i32 *rbps = operators->rbp;
	u32 size = operators->size;
	u32 mask = size - 1;

	assert(op);
	u32 i = hash(op) & mask;
	while (size-- > 0) {
		if (string_equals(keys[i], op)) {
			*lbp = lbps[i];
			*rbp = rbps[i];
			return true;
		}

		i = (i + 1) & mask;
	}

	return false;
}

static bool
operator_find_prefix(struct qm_operator_table *operators, u8 *op, i32 *rbp)
{
	i32 tmp = 0;

	if (operator_find(operators, op, &tmp, rbp)) {
		return tmp == 0;
	} else {
		return false;
	}
}

static bool
operator_find_infix(struct qm_operator_table *operators, u8 *op,
		i32 *lbp, i32 *rbp)
{
	if (operator_find(operators, op, lbp, rbp)) {
		return *lbp != 0 && *rbp != 0;
	} else {
		return false;
	}
}

static bool
operator_find_postfix(struct qm_operator_table *operators, u8 *op, i32 *lbp)
{
	i32 tmp = 0;

	if (operator_find(operators, op, lbp, &tmp)) {
		return tmp == 0;
	} else {
		return false;
	}
}

static bool
is_digit(u8 c)
{
	return '0' <= c && c <= '9';
}

static bool
is_alpha(u8 c)
{
	return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z');
}

static bool
is_whitespace(u8 c)
{
	return c == ' ' || c == '\t';
}

static bool
is_separator(u8 c)
{
    switch (c) {
	case '(': case '[': case '{':
	case ')': case ']': case '}':
	case ',': case '\n': case '"': case '`':
    case ' ': case '\t':
        return true;
    default:
        return false;
    }
}

static u32
token_length(struct qm_token *token, struct qm_buffer *buffer)
{
	u8 *at = buffer->data + token->start;
	u32 length = 0;

	switch (token->type) {
	case QM_TOKEN_STRING:
		assert(*at == '"');

		do {
			length++;
			at++;
		} while (*at && *at != '"');
		length++;
		break;
	case QM_TOKEN_RAW_STRING:
		assert(*at == '`');

		do {
			length++;
			at++;
		} while (*at && *at != '`');
		length++;
		break;
	case QM_TOKEN_IDENTIFIER:
		if (is_alpha(*at) || *at == '_') {
			do {
				length++;
				at++;
			} while (*at && (is_digit(*at) || is_alpha(*at) || *at == '_'));
		} else {
			do {
				length++;
				at++;
			} while (*at && !is_digit(*at) && !is_alpha(*at) && !is_separator(*at));
		}
		break;
	case QM_TOKEN_NUMBER:
		do {
			at++;
			length++;
		} while (*at && is_digit(*at));
		break;
	case QM_TOKEN_LPAREN:
	case QM_TOKEN_RPAREN:
	case QM_TOKEN_LBRACKET:
	case QM_TOKEN_RBRACKET:
	case QM_TOKEN_LBRACE:
	case QM_TOKEN_RBRACE:
	case QM_TOKEN_COMMA:
	case QM_TOKEN_NEWLINE:
		length = 1;
		break;
	}

	return length;
}

static bool
tokenize(struct qm_buffer *buffer, struct qm_token *token)
{
	u8 *at = buffer->data + buffer->start;
	while (is_whitespace(*at)) {
		at++;
	}

	buffer->start = token->start = at - buffer->data;
	if (token->start == buffer->size) {
		token->type = QM_TOKEN_EOF;
		return false;
	}

	token->type = QM_TOKEN_INVALID;
	switch (*at) {
	case '(':  token->type = QM_TOKEN_LPAREN;     break;
	case '[':  token->type = QM_TOKEN_LBRACKET;   break;
	case '{':  token->type = QM_TOKEN_LBRACE;     break;
	case ')':  token->type = QM_TOKEN_RPAREN;     break;
	case ']':  token->type = QM_TOKEN_RBRACKET;   break;
	case '}':  token->type = QM_TOKEN_RBRACE;     break;
	case ',':  token->type = QM_TOKEN_COMMA;      break;
	case '\n': token->type = QM_TOKEN_NEWLINE;    break;
	case '\0': token->type = QM_TOKEN_EOF;        break;
	case '"':  token->type = QM_TOKEN_STRING;     break;
	case '`':  token->type = QM_TOKEN_RAW_STRING; break;

	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		token->type = QM_TOKEN_NUMBER;
		break;
	default:
		token->type = QM_TOKEN_IDENTIFIER;
		break;
	}

	u32 length = token_length(token, buffer);
    if (length == 2) {
        if (memcmp(at, "op", 2) == 0) {
            token->type = QM_TOKEN_OP;
        } else if (memcmp(at, "fn", 2) == 0) {
            token->type = QM_TOKEN_FN;
        }
    } else if (length == 3) {
        if (memcmp(at, "var", 3) == 0) {
            token->type = QM_TOKEN_VAR;
        } else if (memcmp(at, "opr", 3) == 0) {
            token->type = QM_TOKEN_OPR;
        } else if (memcmp(at, "opp", 3) == 0) {
            token->type = QM_TOKEN_OPP;
        }
    }

	buffer->start += length;
	return true;
}

static void
parser_location(struct qm_parser *parser, u32 *out_line, u32 *out_column)
{
	u8 *at = parser->buffer.data;
	u32 count = parser->buffer.start;

	u32 line = 1;
	u32 column = 1;
	while (count-- > 0) {
		column++;

		if (*at == '\n') {
			line++;
			column = 1;
		}

		at++;
	}

	*out_line = line;
	*out_column = column;
}

static void
parser_error(struct qm_parser *parser, const char *fmt, ...)
{
	parser->result = QM_ERR_INVALID_TOKEN;

	u32 line, column;
	parser_location(parser, &line, &column);
	fprintf(stderr, "error:%d:%d: ", line, column);

	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	fputc('\n', stderr);
	fflush(stderr);
}

static bool
accept(struct qm_parser *parser, enum qm_token_type type)
{
	if (!parser->is_initialized) {
		tokenize(&parser->buffer, &parser->token);
		parser->is_initialized = true;
	}

	if (parser->result != 0 || parser->token.type == type) {
		tokenize(&parser->buffer, &parser->token);
		return true;
	} else {
		return false;
	}
}

static bool
accept_identifier(struct qm_parser *parser, u8 *expected)
{
	if (parser->token.type == QM_TOKEN_IDENTIFIER) {
		u32 expected_length = string_length(expected);
		u32 found_length = token_length(&parser->token, &parser->buffer);
		u8 *found = parser->buffer.data + parser->token.start;
		if (expected_length == found_length &&
				memcmp(expected, found, found_length) == 0) {
			 accept(parser, QM_TOKEN_IDENTIFIER);
			 return true;
		}
	}

	return false;
}

static void
expect(struct qm_parser *parser, enum qm_token_type type)
{
	if (!accept(parser, type)) {
		parser_error(parser, "Expected %s, but found %s",
			token_name[type], token_name[parser->token.type]);
	}
}

static void
expect_identifier(struct qm_parser *parser, u8 *expected)
{
	if (!accept_identifier(parser, expected)) {
		parser_error(parser, "Expected '%s'", expected);
	}
}

static bool
peek_identifier(struct qm_parser *parser, struct qm_memory_arena *arena,
		u8 **identifier)
{
	bool result = false;

	if (parser->token.type == QM_TOKEN_IDENTIFIER) {
		result = true;

		u32 length = token_length(&parser->token, &parser->buffer);
		u8 *at = parser->buffer.data + parser->token.start;
		u8 *tmp = arena_alloc(arena, length + 1, u8);
		*identifier = tmp;

		while (length-- > 0) {
			*tmp++ = *at++;
		}
	}

	return result;
}

static bool parse_expression_(struct qm_parser *parser,
	struct qm_memory_arena *arena, struct qm_expression *lhs, i32 bp);

static bool
parse_expression(struct qm_parser *parser, struct qm_memory_arena *arena,
		struct qm_expression *expression)
{
	return parse_expression_(parser, arena, expression, 0);
}

static bool
parse_matrix(struct qm_parser *parser, struct qm_memory_arena *arena,
		struct qm_matrix *matrix)
{
	bool result = false;

	enum qm_token_type closing_delimiter = 0;
	if (accept(parser, QM_TOKEN_LPAREN)) {
		result = true;
		matrix->delimiter = QM_TOKEN_LPAREN;
		closing_delimiter = QM_TOKEN_RPAREN;
	} else if (accept(parser, QM_TOKEN_LBRACKET)) {
		result = true;
		matrix->delimiter = QM_TOKEN_LBRACKET;
		closing_delimiter = QM_TOKEN_RBRACKET;
	} else if (accept(parser, QM_TOKEN_LBRACE)) {
		result = true;
		matrix->delimiter = QM_TOKEN_LBRACKET;
		closing_delimiter = QM_TOKEN_RBRACE;
	}

	if (result) {
		struct qm_memory_block *block = memory_block_create(0);
		struct qm_expression *expr = block->data;
		matrix->width = 0;
		matrix->height = 1;

		u32 i = 0;
		while (parser->result == 0) {
			if (block->used + sizeof(*expr) >= block->size) {
				block->size *= 2;
				assert(block->size);
				if (!(block = realloc(block, block->size + sizeof(*block)))) {
					perror("realloc");
					exit(EXIT_FAILURE);
				}

				block->data = block + 1;
				expr = block->data;
				usize remaining = block->size - block->used;
				memset((u8 *)block->data + block->used, 0, remaining);
			}

			if (!parse_expression(parser, arena, &expr[i++])) {
				parser_error(parser, "Expected expression inside matrix");
				break;
			}

			block->used += sizeof(*expr);

			matrix->width++;
			if (!accept(parser, QM_TOKEN_COMMA)) {
				break;
			}
		}

		expect(parser, closing_delimiter);

		matrix->expressions = block->data;
		block->prev = arena->block;
		arena->block = block;
	}

	return result;
}

static bool
parse_identifier(struct qm_parser *parser, struct qm_memory_arena *arena,
		u8 **identifier)
{
	bool result = peek_identifier(parser, arena, identifier);
	if (result) {
		accept(parser, QM_TOKEN_IDENTIFIER);
	}

	return result;
}

static bool
parse_number(struct qm_parser *parser, i32 *out_number)
{
	bool result = false;

	if (parser->token.type == QM_TOKEN_NUMBER) {
		result = true;

		u8 *at = parser->buffer.data + parser->token.start;
		i32 number = 0;
		while (is_digit(*at)) {
			number *= 10;
			number += *at++ - '0';
		}

		*out_number = number;
		accept(parser, QM_TOKEN_NUMBER);
	}

	return result;
}

static bool
parse_string(struct qm_parser *parser, struct qm_memory_arena *arena,
		u8 **string)
{
	bool result = false;

	if (parser->token.type == QM_TOKEN_STRING) {
		result = true;
		u32 length = token_length(&parser->token, &parser->buffer);
		u8 *at = parser->buffer.data + parser->token.start;
		u8 *tmp = arena_alloc(arena, length + 1, u8);
		*string = tmp;

		while (length-- > 0) {
			*tmp++ = *at++;
		}

		accept(parser, QM_TOKEN_STRING);
	}

	return result;
}

static bool
parse_raw_string(struct qm_parser *parser, struct qm_memory_arena *arena,
		u8 **string)
{
	bool result = false;

	if (parser->token.type == QM_TOKEN_RAW_STRING) {
		result = true;

		u32 length = token_length(&parser->token, &parser->buffer);
		u8 *at = parser->buffer.data + parser->token.start;
		u8 *tmp = arena_alloc(arena, length + 1, u8);
		*string = tmp;

		while (length-- > 0) {
			*tmp++ = *at++;
		}

		accept(parser, QM_TOKEN_RAW_STRING);
	}

	return result;
}

static bool
parse_unary_expression(struct qm_parser *parser, struct qm_memory_arena *arena,
		struct qm_expression *expression)
{
	bool result = true;
	u8 *variable = 0;

	if (parse_matrix(parser, arena, &expression->matrix)) {
		expression->type = QM_EXPR_MATRIX;
	} else if (parse_identifier(parser, arena, &variable)) {
		expression->type = QM_EXPR_VARIABLE;
		expression->variable = variable;

		i32 rbp = 0;
		i32 lbp = 0;
		bool is_operator = operator_find(&parser->operators, variable, &lbp, &rbp);
		bool is_prefix_operator = lbp == 0 && rbp != 0;
		if (is_operator && is_prefix_operator) {
			expression->type = QM_EXPR_CALL;

			struct qm_expression *arg =
				arena_alloc(arena, 1, struct qm_expression);
			if (!parse_expression_(parser, arena, arg, rbp)) {
				parser_error(parser, "Expected expression");
			}

			struct qm_expression *callee =
				arena_alloc(arena, 1, struct qm_expression);
			callee->type = QM_EXPR_VARIABLE;
			callee->variable = variable;

			expression->call.callee = callee;
			expression->call.arg = arg;
        } else if (is_operator) {
			result = false;
		}

	} else if (parse_number(parser, &expression->number)) {
		expression->type = QM_EXPR_NUMBER;
	} else if (parse_string(parser, arena, &expression->string)) {
		expression->type = QM_EXPR_STRING;
	} else if (parse_raw_string(parser, arena, &expression->string)) {
		expression->type = QM_EXPR_RAW_STRING;
	} else {
		result = false;
	}

	return result;
}

static struct qm_expression *
variable_create(struct qm_memory_arena *arena, u8 *identifier)
{
	struct qm_expression *expr = arena_alloc(arena, 1,
		struct qm_expression);
	expr->type = QM_EXPR_VARIABLE;
	expr->variable = identifier;

	return expr;
}

static struct qm_expression *
matrix_create(struct qm_memory_arena *arena, u32 width, u32 height, u8 delim,
		struct qm_expression *expressions)
{
	struct qm_expression *expr = arena_alloc(arena, 1,
		struct qm_expression);
	expr->type = QM_EXPR_MATRIX;
	expr->matrix.width = width;
	expr->matrix.height = height;
	expr->matrix.delimiter = delim;
	expr->matrix.expressions = expressions;

	return expr;
}

static bool
parse_expression_(struct qm_parser *parser, struct qm_memory_arena *arena,
		struct qm_expression *lhs, i32 bp)
{
	struct qm_operator_table *operators = &parser->operators;
	bool result = false;

	if (parse_unary_expression(parser, arena, lhs)) {
		result = true;

		while (parser->result == 0) {
			u8 *op = 0;
			bool is_identifier = peek_identifier(parser, arena, &op);

			i32 lbp, rbp;
			struct qm_expression rhs = {0};
			if (is_identifier && operator_find_postfix(operators, op, &lbp)) {
				if (lbp < bp) {
					break;
				}

				accept(parser, QM_TOKEN_IDENTIFIER);

				struct qm_expression *callee = variable_create(arena, op);
				struct qm_expression *arg = arena_alloc(arena, 1,
					struct qm_expression);
				memcpy(arg, lhs, sizeof(*arg));

				lhs->type = QM_EXPR_CALL;
				lhs->call.callee = callee;
				lhs->call.arg = arg;
			} else if (is_identifier && operator_find_infix(operators, op, &lbp, &rbp)) {
				if (lbp < bp) {
					break;
				}

				accept(parser, QM_TOKEN_IDENTIFIER);

				struct qm_expression *callee = variable_create(arena, op);
				struct qm_expression *args = arena_alloc(arena, 2,
					struct qm_expression);
				memcpy(&args[0], lhs, sizeof(*args));
				if (!parse_expression_(parser, arena, &args[1], rbp)) {
					parser_error(parser, "Expected expression after operator");
				}

				lhs->type = QM_EXPR_CALL;
				lhs->call.callee = callee;
				lhs->call.arg = matrix_create(arena, 2, 1, '\0', args);
			} else if (parse_unary_expression(parser, arena, &rhs)) {
				struct qm_expression *callee = variable_create(arena, op);
				struct qm_expression *arg = arena_alloc(arena, 1,
					struct qm_expression);
				memcpy(callee, lhs, sizeof(*callee));
				memcpy(arg, &rhs, sizeof(*arg));

				lhs->type = QM_EXPR_CALL;
				lhs->call.callee = callee;
				lhs->call.arg = arg;
			} else {
				break;
			}
		}
	}

	return result;
}

static bool
parse_definition(struct qm_parser *parser, struct qm_memory_arena *arena,
		struct qm_definition *definition)
{
	bool result = false;

    if (accept(parser, QM_TOKEN_VAR)) {
		result = true;

        if (!parse_identifier(parser, arena, &definition->variable)) {
            parser_error(parser, "Expected identifier, but found %s",
				token_name[parser->token.type]);
        }

        definition->parameter_count = 0;
        definition->parameters = 0;
        expect_identifier(parser, (u8 *)"=");

        if (!parse_expression(parser, arena, &definition->expression)) {
            parser_error(parser, "Expected expression");
        }

        expect(parser, QM_TOKEN_NEWLINE);
    } else if (accept(parser, QM_TOKEN_FN)) {
		result = true;

		if (!parse_identifier(parser, arena, &definition->variable)) {
			parser_error(parser, "Expected identifier, but found %s",
				token_name[parser->token.type]);
		}

		expect(parser, QM_TOKEN_LPAREN);

		definition->parameters = arena_alloc(arena, 128, u8 *);
		u8 **parameter = definition->parameters;
		u32 parameter_count = 0;
		while (parser->result == 0 && !accept(parser, QM_TOKEN_RPAREN)) {
			if (!parse_identifier(parser, arena, parameter)) {
				parser_error(parser, "Expected identifier, but found %s",
					token_name[parser->token.type]);
			}

			parameter_count++;
			parameter++;

			if (!accept(parser, QM_TOKEN_COMMA)) {
				expect(parser, QM_TOKEN_RPAREN);
				break;
			}
		}

		definition->parameter_count = parameter_count;
		expect_identifier(parser, (u8 *)"=");

		if (!parse_expression(parser, arena, &definition->expression)) {
			parser_error(parser, "Expected expression");
		}

		expect(parser, QM_TOKEN_NEWLINE);
	} else if (accept(parser, QM_TOKEN_OPP)) {
		result = true;
		i32 rbp = ++parser->bp;

		if (!parse_identifier(parser, arena, &definition->variable)) {
			parser_error(parser, "Expected identifier");
		}

		definition->parameter_count = 1;
		definition->parameters = arena_alloc(arena, 1, u8 *);

		if (!parse_identifier(parser, arena, &definition->parameters[0])) {
			parser_error(parser, "Expected one parameter for the operator");
		}

		expect_identifier(parser, (u8 *)"=");

		if (!parse_expression(parser, arena, &definition->expression)) {
			parser_error(parser, "Expected expression for definition");
		}

		operator_define(&parser->operators, arena, definition->variable, 0, rbp);
		expect(parser, QM_TOKEN_NEWLINE);
    } else {
		u8 *operator = 0;
        i32 lbp, rbp;
        i32 bp = ++parser->bp;

        if (accept(parser, QM_TOKEN_OP)) {
            result = true;
            lbp = bp;
            rbp = bp + 1;
        } else if (accept(parser, QM_TOKEN_OPR)) {
            result = true;
            lbp = bp + 1;
            rbp = bp;
        }

        if (result) {
            if (accept(parser, QM_TOKEN_LBRACKET)) {
                u8 *target_operator = 0;
                if (parse_identifier(parser, arena, &target_operator)) {
                    if (!operator_find(&parser->operators, target_operator,
                            &lbp, &rbp)) {
                        parser_error(parser, "Operator not found: %s",
                            target_operator);
                    }

                    // TODO: check if operator actually is right associative
                    // and produce error if it's not the same.
                } else if (parse_number(parser, &lbp)){
					expect(parser, QM_TOKEN_COMMA);
					if (!parse_number(parser, &rbp)) {
						parser_error(parser, "Expected number");
					}
				} else {
                    parser_error(parser, "Expected identifier");
                }

                expect(parser, QM_TOKEN_RBRACKET);
            }

            definition->parameters = arena_alloc(arena, 2, u8 *);
            definition->parameter_count = 2;
            if (!parse_identifier(parser, arena, &definition->parameters[0])) {
                parser_error(parser, "Expected identifier for first parameter");
            }

            if (!parse_identifier(parser, arena, &operator)) {
                parser_error(parser, "Expected identifier for the operator");
            }

            if (!parse_identifier(parser, arena, &definition->parameters[1])) {
                parser_error(parser, "Expected identifier for second parameter");
            }

            expect_identifier(parser, (u8 *)"=");
            if (!parse_expression(parser, arena, &definition->expression)) {
                parser_error(parser,
                    "Expected expression for the definition of %s",
                    (char *)definition->variable);
            }
            expect(parser, QM_TOKEN_NEWLINE);

            operator_define(&parser->operators, arena, operator, lbp, rbp);
            definition->variable = operator;
        }
	}

	return result;
}

static bool
parse_statement(struct qm_parser *parser, struct qm_memory_arena *arena,
		struct qm_statement *stmt)
{
	bool result = true;

	if (accept(parser, QM_TOKEN_NEWLINE)) {
		stmt->type = QM_STMT_NONE;
	} else if (parse_expression(parser, arena, &stmt->expression)) {
		accept(parser, QM_TOKEN_NEWLINE);
		stmt->type = QM_STMT_EXPRESSION;
	} else if (parse_definition(parser, arena, &stmt->definition)) {
		stmt->type = QM_STMT_DEFINITION;
	} else {
		stmt->type = QM_STMT_NONE;
		result = false;
	}

	return result;
}

int
main(int argc, char **argv)
{
	struct qm_buffer json = {0};
	struct qm_parser parser = {0};
	struct qm_memory_arena arena = {0};
	struct tex_environment env = {0};

	if (argc < 2) {
		fprintf(stderr, "Not enough arguments\n");
		return 1;
	}

	if (!file_read(argv[1], &arena, &parser.buffer)) {
		fprintf(stderr, "Failed to read stdin: %s\n", strerror(errno));
		return 1;
	}

	if (!file_read(0, &arena, &json)) {
		fprintf(stderr, "Failed to read file '%s': %s\n", argv[1], strerror(errno));
		return 1;
	}

	operator_define(&parser.operators, &arena, (u8 *)"__unwrap__", 0, 100);

	struct qm_statement statement = {0};
	while (parse_statement(&parser, &arena, &statement)) {
		tex_eval(&statement, 0, &arena, &env);
	}

	parser.buffer.start = 0;
	parser.buffer.data  = 0;
	parser.buffer.size  = 0;

	while (pandoc_next_math_block(&json, &arena, &parser.buffer)) {
		parser.buffer.start = 0;
		assert(parser.buffer.size != 0);
		assert(parser.buffer.start == 0);
		assert(parser.buffer.start == 0);
		tokenize(&parser.buffer, &parser.token);

		putchar('"');
		struct qm_statement statement = {0};
		while (parse_statement(&parser, &arena, &statement)) {
			struct qm_buffer output = {0};
			output.size = tex_eval(&statement, 0, &arena, &env);
			output.data = arena_alloc(&arena, output.size + 1, u8);

			tex_eval(&statement, &output, &arena, &env);
			pandoc_print_string(output.data, output.size);
		}
		putchar('"');

		parser.buffer.size = 0;
		parser.buffer.data = 0;
	}

	arena_finish(&arena);
	return 0;
}
