#pragma once

#include "util.h"
#include "types.h"

typedef enum {
	GEN_EXE = 0x00,
	GEN_OBJ = 0x01,
	GEN_ASM = 0x02,
	GEN_SHR = 0x03,

	GEN_DBG = 0x04,
	GEN_OPT1 = 0x08,
	GEN_OPT2 = 0x10,
	GEN_OPT3 = 0x18
} GenOptions;

typedef void *WyrtContext;
typedef void *WyrtLvalue;
typedef void *WyrtRvalue;
typedef void *WyrtFunction;
typedef void *WyrtBlock;
typedef void *WyrtParam;

typedef struct {
	WyrtContext (*get_ctx)(Error*);
	void (*compile)(WyrtContext, GenOptions, const char*, Error*);
	void (*release_ctx)(WyrtContext);

	WyrtParam (*new_param)(WyrtContext, const DebugInfo*, Type, TypeContext const *, char const *, Error*);
	WyrtFunction (*new_function)(
		WyrtContext,
		const DebugInfo*,
		Type,
		TypeContext const *,
		WyrtParam*,
		size_t,
		bool, // true == imported
		char const *,
		Error*
	);
	WyrtParam (*function_get_param)(WyrtContext, WyrtFunction, size_t, Error*);

	WyrtRvalue (*new_cast)(WyrtContext, const DebugInfo*, WyrtRvalue, Type, TypeContext const *, Error*);
	WyrtRvalue (*rvalue_from_lvalue)(WyrtLvalue);
	WyrtRvalue (*rvalue_from_param)(WyrtParam);

	WyrtRvalue (*rvalue_null)(WyrtContext, Type, TypeContext *tc, Error*);
	WyrtRvalue (*rvalue_int_lit)(WyrtContext, intmax_t, TypeType, Error*);
	WyrtRvalue (*rvalue_struct_lit)(
		WyrtContext,
		const DebugInfo*,
		Type,
		TypeContext const*,
		WyrtRvalue*,
		size_t,
		Error*
	);

	WyrtRvalue (*rvalue_array_lit)(
		WyrtContext,
		const DebugInfo*,
		Type,
		TypeContext const*,
		WyrtRvalue*,
		size_t,
		Error*
	);
	WyrtRvalue (*rvalue_cstring_lit)(WyrtContext, const DebugInfo*, char const*, Error*);

	WyrtRvalue (*rvalue_binary_op)(
		WyrtContext,
		const DebugInfo*,
		AstNodeType,
		TypeType,
		WyrtRvalue,
		WyrtRvalue,
		Error*
	);
	WyrtRvalue (*rvalue_unary_op)(
		WyrtContext,
		const DebugInfo*,
		AstNodeType,
		TypeType,
		WyrtRvalue,
		Error*
	);
	WyrtRvalue (*rvalue_address)(WyrtContext, const DebugInfo*, WyrtLvalue, Error*);
	WyrtRvalue (*rvalue_field)(
		WyrtContext,
		const DebugInfo*,
		WyrtRvalue,
		Type,
		TypeContext const *,
		size_t,
		Error*
	);

	WyrtRvalue (*rvalue_fn_call)(WyrtContext, const DebugInfo*, WyrtFunction, WyrtRvalue*, size_t, Error*);


	WyrtLvalue (*block_new_variable)(
		WyrtContext,
		const DebugInfo*,
		WyrtBlock,
		Type,
		TypeContext const *,
		const char*,
		Error*
	);

	WyrtLvalue (*lvalue_subscript)(WyrtContext, const DebugInfo*, WyrtRvalue, WyrtRvalue, Error*);
	WyrtLvalue (*lvalue_deref_field)(
		WyrtContext,
		const DebugInfo*,
		WyrtRvalue,
		Type,
		TypeContext const *,
		size_t,
		Error*
	);

	WyrtLvalue (*lvalue_deref)(WyrtContext, const DebugInfo*, WyrtRvalue, Error*);
	WyrtLvalue (*lvalue_field)(
		WyrtContext,
		const DebugInfo*,
		WyrtLvalue,
		Type,
		TypeContext const *,
		size_t,
		Error*
	);

	WyrtBlock (*new_block)(WyrtContext, WyrtFunction, Error*);
	void (*block_add_eval)(WyrtContext, const DebugInfo*, WyrtBlock, WyrtRvalue, Error*);
	void (*block_add_assign)(WyrtContext, const DebugInfo*, WyrtBlock, WyrtLvalue, WyrtRvalue, Error*);
	void (*block_add_compound_assign)(
		WyrtContext,
		const DebugInfo*,
		WyrtBlock,
		WyrtLvalue,
		WyrtRvalue,
		AstNodeType,
		Error*
	);

	void (*block_end_with_return)(WyrtContext, const DebugInfo*, WyrtBlock, WyrtRvalue, Error*);
	void (*block_end_with_cond)(
		WyrtContext,
		const DebugInfo*,
		WyrtBlock,
		WyrtRvalue,
		WyrtBlock,
		WyrtBlock,
		Error*
	);
	void (*block_end_with_jump)(
		WyrtContext,
		const DebugInfo*,
		WyrtBlock,
		WyrtBlock,
		Error*
	);

} WyrtBackend;
