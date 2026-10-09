#include "../../kernel/mt_gfx_packet.h"
int encode_packet(void *out, u32 capacity, const struct mt_gfx_packet_source *source,
		const struct mt_gfx_packet_input *input, u32 family);
int encode_packet(void *out, u32 capacity, const struct mt_gfx_packet_source *source,
		const struct mt_gfx_packet_input *input, u32 family)
{
	struct mt_device_profile p;
	if (mt_device_profile_select(&p, 0x1ed5, family << 8))
		return -EINVAL;
	return mt_gfx_packet_encode(out, capacity, &p, source, input);
}
