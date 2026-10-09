#include "../../kernel/mt_linux_tdm.h"
int normalize(void *out, u32 n, const void *packet, u32 bytes,
	u64 native_dma_va, u64 native_size_array_va,
	const struct mt_tqx_upload_result *source, const struct mt_tqx_dma_input *input)
{
	struct mt_device_profile profile;
	mt_device_profile_select(&profile, 0x1ed5, 0x222);
	return mt_linux_tdm_normalize(out, n, &profile, packet, bytes,
		native_dma_va, native_size_array_va, source, input);
}
