#include "../kernel/mt_tqx_dma.h"
int plan_state(struct mt_tqx_engine_state_plan *out,u32 cores,u64 va)
{
 struct mt_device_profile profile;
 mt_device_profile_select(&profile,0x1ed5,0x222);
 return mt_tqx_engine_state_plan(out,&profile,cores,va);
}
int encode_dma(void *out,u32 n,const struct mt_tqx_upload_result *source,const struct mt_tqx_dma_input *input)
{
 struct mt_device_profile profile;
 mt_device_profile_select(&profile,0x1ed5,0x222);
 return mt_tqx_dma_encode(out,n,&profile,source,input);
}
