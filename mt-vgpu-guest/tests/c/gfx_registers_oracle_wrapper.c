#include "../../kernel/mt_gfx_registers.h"
int encode(void *out,u32 capacity,const struct mt_gfx_register_source *source,u32 family)
{
 struct mt_device_profile p;
 if(mt_device_profile_select(&p,0x1ed5,family<<8)) return -EINVAL;
 return mt_gfx_registers_encode(out,capacity,&p,source);
}
