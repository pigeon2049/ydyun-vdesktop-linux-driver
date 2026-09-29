#include "../kernel/mt_tqx_texture.h"
int build_texture(void *out, u32 capacity, const struct mt_tqx_texture_input *in)
{
	return mt_tqx_texture_build(out, capacity, in);
}
