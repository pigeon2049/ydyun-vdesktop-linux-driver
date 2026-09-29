#include "../kernel/mt_tqx_destination.h"
int build_destination(void *out, u32 capacity, const struct mt_tqx_destination_input *in)
{
	return mt_tqx_destination_build(out, capacity, in);
}
