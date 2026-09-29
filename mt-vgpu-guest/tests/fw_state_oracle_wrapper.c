#include "../kernel/mt_fw_state.h"
#include "../kernel/mt_guest_heaps.h"

void plan_heaps(struct mt_guest_heap_plan *out)
{
	mt_guest_plan_heaps(out);
}

int apply_guest_state(void *image, uint32_t size, const struct mt_fw_guest_resources *r)
{
	return mt_fw_apply_guest_state(image, size, r);
}

int build_layout(void *out, uint64_t va)
{
	return mt_fw_build_guest_layout(out, va);
}
