#include "../../kernel/mt_mmu_bootstrap.h"

int build(void *out, u32 size, u64 pa, const struct mt_mmu_range *ranges, u32 count, u32 *used)
{
	return mt_mmu_build_bootstrap(out, size, pa, ranges, count, used);
}

int dummy(void *out, u32 size, u64 pa)
{
	return mt_mmu_build_dummy(out, size, pa);
}
