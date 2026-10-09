#include "../../kernel/mt_ce_copy.h"
int encode(void *out, u32 capacity, const struct mt_ce_copy_input *in)
{ return mt_ce_copy_encode(out, capacity, in); }
