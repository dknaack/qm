static bool
pandoc_next_string(struct qm_buffer *input)
{
	u8 *at = input->data + input->start;
	u32 length = input->size - input->start;

	if (*at == '"') {
		do {
			if (*at == '\\') {
				at++;
				length--;
			}

			at++;
			length--;
		} while (length > 0 && *at != '"');

		if (length == 0) {
			return false;
		}

		length--;
		at++;
	}

	while (length > 0 && *at != '"') {
		length--;
		at++;
	}

	input->start = at - input->data;
	return *at == '"';
}

static bool
pandoc_key_equals(struct qm_buffer *input, char *key)
{
	u8 *at = input->data + input->start + 1;
	u32 length = input->size - input->start;
	u32 key_length = strlen(key);

	if (key_length + 2 <= length && memcmp(at, key, key_length) == 0) {
		return at[key_length + 1] == ':';
	} else {
		return false;
	}
}

static bool
pandoc_string_equals(struct qm_buffer *input, char *str)
{
	u8 *at = input->data + input->start + 1;
	u32 length = input->size - input->start;
	u32 str_length = strlen(str);

	return str_length <= length + 2 && memcmp(at, str, str_length) == 0;
}

static u32
pandoc_string_length(struct qm_buffer *input)
{
	u8 *at = input->data + input->start + 1;
	u32 length = 1;

	assert(*at != '"');
	while (*at != '"') {
		if (*at == '\\') {
			length++;
			at++;
		}

		length++;
		at++;
	}

	return length + 1;
}

/* TODO: encode all characters that have to be encoded */
static usize
pandoc_encode_string(u8 *string, u8 *encoded_string)
{
	u8 *at = string + 1;
	u32 count = 0;

	do {
		u8 c = *at;
		if (c == '\\') {
			at++;

			if (*at == 'n') {
				c = '\n';
			} else {
				c = *at;
			}
		}

		if (encoded_string) {
			*encoded_string++ = c;
		}

		at++;
		count++;
	} while (*at != '"');

	return count;
}

/*
 * NOTE: this is by no means a complete json parser. All it does is iterate
 * over json strings and tries to match those with a pattern that matches
 * inline and display math elements from the pandoc json format. This should
 * probably be replaced by a proper json parser or something similar.
 */
static bool
pandoc_next_math_block(struct qm_buffer *input, struct qm_memory_arena *arena,
		struct qm_buffer *output)
{
	u32 start = input->start;
	u32 state = 0;
	while (state < 6 && pandoc_next_string(input)) {
		switch (state) {
		case 0:
		case 3:
			if (pandoc_key_equals(input, "t")) {
				state++;
			} else {
				state = 0;
			}
			break;
		case 1:
			if (pandoc_string_equals(input, "Math")) {
				state++;
			} else {
				state = 0;
			}
			break;
		case 2:
			if (pandoc_key_equals(input, "c")) {
				state++;
			} else {
				state = 0;
			}
			break;
		case 4:
			if (pandoc_string_equals(input, "DisplayMath") ||
					pandoc_string_equals(input, "InlineMath")) {
				state++;
			} else {
				state = 0;
			}
			break;
		case 5:
			state += 1;
			output->start = 0;
			output->size = pandoc_encode_string(input->data + input->start, 0);
			output->data = arena_alloc(arena, output->size + 1, u8);
			pandoc_encode_string(input->data + input->start, output->data);
			output->data[output->size] = '\0';
			break;
		}
	}

	u32 count = input->start - start;
	fwrite(input->data + start, count, 1, stdout);
	if (state == 6) {
		input->start += pandoc_string_length(input);
	}

	return state == 6;
}

static void
pandoc_print_string(u8 *string, usize size)
{
	u8 *at = string;

	while (size > 0) {
		if (*at == '"') {
			printf("\\\"");
		} else if (*at == '\n') {
			printf("\\\n");
		} else if (*at == '\\') {
			printf("\\\\");
		} else {
			putchar(*at);
		}

		size--;
		at++;
	}
}
