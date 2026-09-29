#include "../kernel/mt_tqx_copy_stream.h"
int build_copy_stream(void *out, u32 capacity, const struct mt_tqx_copy_stream_input *in)
{
	return mt_tqx_copy_stream_build(out, capacity, in);
}
