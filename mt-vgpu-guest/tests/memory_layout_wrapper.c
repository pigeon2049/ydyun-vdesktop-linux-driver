#include "../kernel/mt_memory_layout.h"

int parse_memory(const void *raw, uint32_t size, uint64_t bar_size, struct mt_memory_layout *out)
{
	return mt_memory_parse(raw, size, bar_size, out);
}
