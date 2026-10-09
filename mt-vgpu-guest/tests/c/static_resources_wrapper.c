#include "../../kernel/mt_static_resources.h"

int initialize(void *yuv, u32 yuv_size, void *kill, u32 kill_size)
{
	return mt_init_static_resources(yuv, yuv_size, kill, kill_size);
}
