#include "../kernel/mt_gfx_context.h"

int test_gfx_context_build(void *out, u32 capacity, const struct mt_gfx_context_bo_addresses *addrs);

int test_gfx_context_build(void *out, u32 capacity, const struct mt_gfx_context_bo_addresses *addrs)
{
	return mt_gfx_context_build_csw(out, capacity, addrs);
}
