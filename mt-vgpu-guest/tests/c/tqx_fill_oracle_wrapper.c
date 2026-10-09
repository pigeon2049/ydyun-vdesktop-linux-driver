#include "../../kernel/mt_tqx_fill.h"
int build_fill(void *out, u32 capacity, const struct mt_tqx_fill_input *in)
{
	return mt_tqx_fill_build(out, capacity, in);
}
