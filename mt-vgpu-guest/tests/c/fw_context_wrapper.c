#include "../../kernel/mt_fw_context.h"

int build(void *out, u32 bytes, const struct mt_fw_context_addresses *a)
{
	return mt_fw_context_build(out, bytes, a);
}

int refresh(void *out, u32 bytes, u64 info, u64 aperture)
{
	return mt_fw_context_refresh(out, bytes, info, aperture);
}
