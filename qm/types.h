#define MAX(a, b) ((a) > (b)? (a) : (b))

#define arena_alloc(arena, count, type) \
	((type *)arena_alloc_(arena, (count) * sizeof(type)))

typedef size_t usize;

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

typedef int64_t i64;
typedef int32_t i32;
typedef int16_t i16;
typedef int8_t  i8;

typedef double f64;
typedef float  f32;

struct qm_memory_block {
	void *data;
	usize size;
	usize used;

	struct qm_memory_block *prev;
};

struct qm_memory_arena {
	struct qm_memory_block *block;
};

enum qm_token_type {
	QM_TOKEN_INVALID,
	QM_TOKEN_EOF,
	QM_TOKEN_LPAREN,
	QM_TOKEN_RPAREN,
	QM_TOKEN_LBRACKET,
	QM_TOKEN_RBRACKET,
	QM_TOKEN_LBRACE,
	QM_TOKEN_RBRACE,
	QM_TOKEN_COMMA,
	QM_TOKEN_NEWLINE,
	QM_TOKEN_IDENTIFIER,
	QM_TOKEN_NUMBER,
	QM_TOKEN_STRING,
	QM_TOKEN_RAW_STRING,
    QM_TOKEN_VAR,
    QM_TOKEN_FN,
	QM_TOKEN_OP,
	QM_TOKEN_OPR,
	QM_TOKEN_OPP,
	QM_TOKEN_COUNT
};

struct qm_token {
	i32 type;
	u32 start;
};

struct qm_buffer {
	u8 *data;
	u32 size;
	u32 start;
};

enum qm_result {
	QM_OK,
	QM_ERR_INVALID_TOKEN,
};

struct qm_operator_table {
	u8 **keys;
	i32 *lbp;
	i32 *rbp;

	u32 used;
	u32 size;
};

struct qm_parser {
	struct qm_buffer buffer;
	struct qm_token token;
	struct qm_operator_table operators;

	bool is_initialized;
	i32 result;
    i32 bp;
};

struct qm_expression;

struct qm_matrix {
	u32 width;
	u32 height;
	u8 delimiter;

	struct qm_expression *expressions;
};

struct qm_call {
	struct qm_expression *callee;
	struct qm_expression *arg;
};

enum qm_expression_type {
	QM_EXPR_NONE,
	QM_EXPR_MATRIX,
	QM_EXPR_CALL,
	QM_EXPR_VARIABLE,
	QM_EXPR_NUMBER,
	QM_EXPR_STRING,
	QM_EXPR_RAW_STRING,
	QM_EXPR_COUNT
};

struct qm_expression {
	i32 type;

	union {
		struct qm_matrix matrix;
		struct qm_call call;
		u8 *variable;
		u8 *string;
		i32 number;
	};
};

struct qm_definition {
	u8 *variable;
	u8 **parameters;
	u32 parameter_count;
	struct qm_expression expression;
};

enum qm_statement_type {
	QM_STMT_NONE,
	QM_STMT_EXPRESSION,
	QM_STMT_DEFINITION,
	QM_STMT_COUNT
};

struct qm_statement {
	i32 type;

	union {
		struct qm_expression expression;
		struct qm_definition definition;
	};
};
