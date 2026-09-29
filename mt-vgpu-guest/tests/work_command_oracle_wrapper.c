#include "../kernel/mt_work_command.h"
int encode(void *out, u32 size, const struct mt_work_command_inputs *in, u32 fence)
{ return mt_work_command_encode(out, size, in, fence); }
int node(struct mt_node_route *out, u32 type, u32 index)
{ return mt_node_route_build(out, type, index); }
