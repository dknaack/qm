static const u8 *open_delimiters[2][QM_TOKEN_COUNT] = {
	[false] = {
		[QM_TOKEN_LPAREN]   = (u8 *)"(",
		[QM_TOKEN_LBRACKET] = (u8 *)"_{",
		[QM_TOKEN_LBRACE]   = (u8 *)"\\{",
	},
	[true] = {
		[QM_TOKEN_LPAREN]   = (u8 *)"\\begin{pmatrix}",
		[QM_TOKEN_LBRACKET] = (u8 *)"\\begin{bmatrix}",
		[QM_TOKEN_LBRACE]   = (u8 *)"\\begin{matrix}",
	},
};

static const u8 *closing_delimiters[2][QM_TOKEN_COUNT] = {
	[false] = {
		[QM_TOKEN_LPAREN]   = (u8 *)")",
		[QM_TOKEN_LBRACKET] = (u8 *)"}",
		[QM_TOKEN_LBRACE]   = (u8 *)"\\}",
	},
	[true] = {
		[QM_TOKEN_LPAREN]   = (u8 *)"\\end{pmatrix}",
		[QM_TOKEN_LBRACKET] = (u8 *)"\\end{bmatrix}",
		[QM_TOKEN_LBRACE]   = (u8 *)"\\end{matrix}",
	},
};

static usize
buffer_write(struct qm_buffer *buffer, const u8 *string)
{
	assert(string);

	if (buffer) {
		u8 *at = buffer->data + buffer->start;
		u32 size = buffer->size;
		u32 used = buffer->start;
		u32 count = 0;
		while (*string && used + count < size) {
			*at++ = *string++;
			count++;
		}

		buffer->start += count;
		return count;
	} else {
		return string_length(string);
	}
}

static usize
buffer_writen(struct qm_buffer *buffer, const u8 *string, u32 length)
{
	if (buffer) {
		u8 *at = buffer->data + buffer->start;
		u32 size = buffer->size;
		u32 used = buffer->start;
		u32 count = 0;
		while (length-- > 0 && used + count < size) {
			*at++ = *string++;
			count++;
		}

		buffer->start += count;
		return count;
	} else {
		return length;
	}
}

static bool
tex_env_find(struct tex_environment *env, const u8 *name, struct tex_value *value)
{
	if (!name) {
		return false;
	}

	u32 size = env->size;
	u32 mask = size - 1;
	u32 i = hash(name) & mask;
	while (size-- > 0) {
		if (string_equals(name, env->keys[i])) {
			memcpy(value, &env->values[i], sizeof(*value));
			assert(0 <= value->type && value->type < TEX_VALUE_COUNT);
			return true;
		}

		i = (i + 1) & mask;
	}

	return env->parent ? tex_env_find(env->parent, name, value) : false;
}

static bool
tex_env_define(struct tex_environment *env, struct qm_memory_arena *arena,
		u8 *name, struct tex_value *value)
{
	assert(0 <= value->type && value->type < TEX_VALUE_COUNT);

	if (env->size == 0) {
		env->size = 1024;
		env->keys = arena_alloc(arena, env->size, u8 *);
		env->values = arena_alloc(arena, env->size, struct tex_value);
		memset(env->keys, 0, env->size * sizeof(*env->keys));
		memset(env->values, 0, env->size * sizeof(*env->values));
	}

	// TODO: reallocate the environment map
	if (env->used == env->size) {
		return false;
	}

	u32 size = env->size;
	u32 mask = size - 1;
	u32 i = hash(name) & mask;
	while (size-- > 0) {
		if (!env->keys[i] || string_equals(name, env->keys[i])) {
			env->keys[i] = name;
			memcpy(&env->values[i], value, sizeof(*value));
			env->used++;
			return true;
		}

		i = (i + 1) & mask;
	}

	return false;
}

static struct tex_value *
tex_builtin_unwrap(struct tex_value *value)
{
	u32 is_1x1_matrix = value->type == TEX_VALUE_MATRIX &&
		value->matrix.width == 1 && value->matrix.height == 1;

	return is_1x1_matrix ? tex_builtin_unwrap(value->matrix.values) : value;
}

static usize
tex_value_write(struct tex_value *value, struct qm_buffer *output)
{
	char number_str[64] = {0};
	u32 total = 0;

	switch (value->type) {
	case TEX_VALUE_FUNCTION:
		total += buffer_write(output, (u8 *)"<fn>");
		break;
	case TEX_VALUE_STRING:
		total += buffer_write(output, (u8 *)"\\text{");
		total += buffer_writen(output, value->string.data, value->string.size);
		total += buffer_write(output, (u8 *)"}");
		break;
	case TEX_VALUE_RAW_STRING:
		total += buffer_writen(output, value->string.data, value->string.size);
		break;
	case TEX_VALUE_NUMBER:
		snprintf(number_str, sizeof(number_str), "%d", value->number);
		total += buffer_write(output, (u8 *)number_str);
		break;
	case TEX_VALUE_MATRIX:
		{
			bool is_matrix = value->matrix.height > 1;
			u32 delimiter = value->matrix.delimiter;
			assert(delimiter < QM_TOKEN_COUNT);

			const u8 *open_delim = open_delimiters[is_matrix][delimiter];
			const u8 *closing_delim = closing_delimiters[is_matrix][delimiter];
			total += buffer_write(output, open_delim);

			u32 width = value->matrix.width;
			u32 height = value->matrix.height;
			const u8 *cell_delimiter = (u8 *)(height > 1 ? " & " : ", ");
			struct tex_value *values = value->matrix.values;
			for (u32 i = 0; i < height; i++) {
				if (i != 0) {
					total += buffer_write(output, (u8 *)"\\\n");
				}

				for (u32 j = 0; j < width; j++) {
					if (j != 0 && height == 1) {
						total += buffer_write(output, cell_delimiter);
					}

					total += tex_value_write(values++, output);
				}
			}

			total += buffer_write(output, closing_delim);
		}
		break;
	default:
		fprintf(stderr, "value->type = %d\n", value->type);
		fflush(stderr);
		fflush(stdout);
		assert(!"Not implemented");
	}

	return total;
}

static bool tex_eval_expression(struct qm_expression *expression,
	struct tex_value *value, struct qm_memory_arena *arena,
	struct tex_environment *env);

static bool
tex_eval_call(struct qm_call *call, struct tex_value *value,
		struct qm_memory_arena *arena, struct tex_environment *env)
{
	struct tex_value callee;
	struct tex_value arg;

	bool is_variable = call->callee->type == QM_EXPR_VARIABLE;
	if (is_variable && string_equals(call->callee->variable, (u8 *)"__unwrap__")) {
		assert(tex_eval_expression(call->arg, &arg, arena, env));

		memcpy(value, tex_builtin_unwrap(&arg), sizeof(*value));
	} else {
		assert(tex_eval_expression(call->callee, &callee, arena, env));
		assert(tex_eval_expression(call->arg, &arg, arena, env));

		if (callee.type == TEX_VALUE_FUNCTION) {
			u8 **parameters = callee.function.parameters;
			u32 parameter_count = callee.function.parameter_count;

			bool is_matrix = arg.type == TEX_VALUE_MATRIX;
			u32 width = arg.matrix.width;
			u32 height = arg.matrix.height;
			assert(parameter_count == 1 || (is_matrix && width == parameter_count && height == 1));

			struct tex_environment subenv = {0};
			subenv.parent = env;

			struct tex_value *values = parameter_count != 1 ? arg.matrix.values : &arg;
			for (u32 i = 0; i < parameter_count; i++) {
				tex_env_define(&subenv, arena, *parameters++, values++);
			}

			struct qm_expression *expression = &callee.function.expression;
			tex_eval_expression(expression, value, arena, &subenv);
		} else {
			struct qm_buffer buffer = {0};
			buffer.size += tex_value_write(&callee, 0);
			buffer.size += tex_value_write(&arg, 0);
			buffer.data = arena_alloc(arena, buffer.size, u8);
			tex_value_write(&callee, &buffer);
			tex_value_write(&arg, &buffer);

			value->type = TEX_VALUE_RAW_STRING;
			value->string.data = buffer.data;
			value->string.size = buffer.size;
		}
	}

	assert(0 <= value->type && value->type < TEX_VALUE_COUNT);
	return true;
}

static bool
tex_eval_expression(struct qm_expression *expression, struct tex_value *value,
		struct qm_memory_arena *arena, struct tex_environment *env)
{
	switch (expression->type) {
	case QM_EXPR_MATRIX:
		{
			struct qm_expression *expressions = expression->matrix.expressions;
			u32 height = expression->matrix.height;
			u32 width = expression->matrix.width;

			struct tex_value *values = arena_alloc(arena, width * height,
				struct tex_value);

			value->type = TEX_VALUE_MATRIX;
			value->matrix.width  = width;
			value->matrix.height = height;
			value->matrix.values = values;
			value->matrix.delimiter = expression->matrix.delimiter;

			for (u32 i = 0; i < height; i++) {
				for (u32 j = 0; j < width; j++) {
					tex_eval_expression(expressions++, values++, arena, env);
				}
			}
		}
		break;
	case QM_EXPR_CALL:
		assert(tex_eval_call(&expression->call, value, arena, env));
		break;
	case QM_EXPR_STRING:
		value->type = TEX_VALUE_STRING;
		// NOTE: should use a conversion function in the future for escaping
		// special symbols.
		value->string.data = expression->string + 1;
		value->string.size = string_length(expression->string) - 2;
		break;
	case QM_EXPR_RAW_STRING:
		value->type = TEX_VALUE_RAW_STRING;
		// NOTE: should use a conversion function in the future for escaping
		// special symbols.
		value->string.data = expression->string + 1;
		value->string.size = string_length(expression->string) - 2;
		break;
	case QM_EXPR_VARIABLE:
		if (!tex_env_find(env, expression->variable, value)) {
			value->type = TEX_VALUE_RAW_STRING;
			value->string.data = expression->variable;
			value->string.size = string_length(expression->variable);
		}
		break;
	case QM_EXPR_NUMBER:
		value->type = TEX_VALUE_NUMBER;
		value->number = expression->number;
		break;
	}

	assert(0 <= value->type && value->type < TEX_VALUE_COUNT);
	return true;
}

static usize
tex_eval(struct qm_statement *stmt, struct qm_buffer *output,
		struct qm_memory_arena *arena, struct tex_environment *env)
{
	struct tex_value value;
	usize size = 0;

	switch (stmt->type) {
	case QM_STMT_EXPRESSION:
		if (tex_eval_expression(&stmt->expression, &value, arena, env)) {
			size += tex_value_write(&value, output);
		}
		break;
	case QM_STMT_DEFINITION:
		if (stmt->definition.parameter_count != 0) {
			value.type = TEX_VALUE_FUNCTION;
			value.function.parameters = stmt->definition.parameters;
			value.function.parameter_count = stmt->definition.parameter_count;
			value.function.expression = stmt->definition.expression;
		} else {
			tex_eval_expression(&stmt->definition.expression,
				&value, arena, env);
		}

		tex_env_define(env, arena, stmt->definition.variable, &value);
		break;
	}

	return size;
}
