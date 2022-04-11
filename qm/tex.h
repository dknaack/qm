enum tex_value_type {
	TEX_VALUE_NONE,
	TEX_VALUE_NUMBER,
	TEX_VALUE_FUNCTION,
	TEX_VALUE_MATRIX,
	TEX_VALUE_STRING,
	TEX_VALUE_RAW_STRING,
	TEX_VALUE_COUNT
};

struct tex_function {
	u8 **parameters;
	u32 parameter_count;

	struct qm_expression expression;
};

struct tex_matrix {
	u32 width;
	u32 height;
	u8 delimiter;
	struct tex_value *values;
};

struct tex_string {
	u8 *data;
	u32 size;
};

struct tex_value {
	enum tex_value_type type;

	union {
		struct tex_matrix matrix;
		struct tex_function function;
		struct tex_string string;
		i32 number;
	};
};

struct tex_environment {
	u8 **keys;
	struct tex_value *values;

	u32 used;
	u32 size;

	struct tex_environment *parent;
};
