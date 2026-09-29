#include "../kernel/mt_static_programs.h"
int build_static(void *pds,u32 pds_bytes,void *usc,u32 usc_bytes,u32 family)
{
 struct mt_device_profile p={.family=family,.transfer_version=1};
 return mt_static_programs_build(pds,pds_bytes,usc,usc_bytes,&p);
}
