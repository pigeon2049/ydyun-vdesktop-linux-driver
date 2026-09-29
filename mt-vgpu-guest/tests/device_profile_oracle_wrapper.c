#include "../kernel/mt_device_profile.h"
int select_profile(struct mt_device_profile *out, u32 vendor, u32 device)
{
	return mt_device_profile_select(out, vendor, device);
}
#include "../kernel/mt_tqx_topology.h"
int topology(u32 *out,const struct mt_device_profile *profile,const void *info,u32 bytes)
{
 return mt_tqx_topology_from_info(out,profile,info,bytes);
}
