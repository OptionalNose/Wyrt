#include "codegen.h"

void codegen_init(
		CodeGen *cg,
		FILE *output,
		const AstNode *nodes,
		size_t node_count,
		char *const *identifiers
)
{
	*cg = (CodeGen) {
		.output = output,
		.nodes = nodes,
		.node_count = node_count,
		.identifiers = identifiers,
		.fn_sig_count = 0,
		.fn_sigs = NULL,
	};
}

void codegen_clean(const CodeGen *cg)
{
	for(size_t i = 0; i < cg->fn_sig_count; i++) {
		free(cg->fn_sigs[i].args);
	}
	if(cg->fn_sigs) free(cg->fn_sigs);
}

void codegen_gen(CodeGen *cg, bool exec, PlatformType plat, Error *err)
{
	switch(plat) {
	case PLATFORM_LINUX:
		codegen_gen_linux(cg, exec, err);
		break;
	case PLATFORM_WINDOWS:
		codegen_gen_windows(cg, exec, err);
		break;
	}

	return;
}
