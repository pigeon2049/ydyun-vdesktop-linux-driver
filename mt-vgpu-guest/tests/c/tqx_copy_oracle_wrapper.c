#include "../../kernel/mt_tqx_copy.h"
int build_plan(void *out, u32 capacity, const struct mt_tqx_copy_input *in)
{
	return mt_tqx_copy_plan_build(out, capacity, in);
}
