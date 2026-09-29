#include "../kernel/mt_fw_layout.h"

int build_guest_layout(void *out, uint64_t va)
{
	return mt_fw_build_guest_layout(out, va);
}
