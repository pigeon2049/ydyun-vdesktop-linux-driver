#include "../../kernel/mt_tqx_upload.h"
int bind_heap(void *out, u32 n, const struct mt_tqx_heap_input *in)
{
 struct mt_device_profile profile;
 mt_device_profile_select(&profile, 0x1ed5, 0x222);
 return mt_tqx_heap_stream_input(out, n, &profile, in);
}
int build_heap(void *out, u32 n, const struct mt_tqx_heap_input *in)
{
 struct mt_tqx_copy_stream_input request;
 int ret=bind_heap(&request,sizeof(request),in);
 return ret ? ret : mt_tqx_copy_stream_build(out,n,&request);
}
void plan_heaps(struct mt_guest_heap_plan *out) { mt_guest_plan_heaps(out); }
int relative(const struct mt_guest_heap *h, u64 va, u32 bytes, u32 alignment, u64 *out)
{ return mt_tqx_heap_relative(h,va,bytes,alignment,out); }
int upload_pages(void *out,u32 capacity,const struct mt_tqx_heap_input *in)
{
 struct mt_tqx_copy_stream_image stream;
 u32 i,offset,cursor=0;
 int ret;
 if(!out || capacity<36864)return -EINVAL;
 ret=build_heap(&stream,sizeof(stream),in);
 if(ret)return ret;
 for(i=0;i<5;i++)for(offset=0;offset<mt_tqx_buffer_bytes[i];offset+=4096) {
  mt_tqx_upload_pack_page((u8 *)out+cursor,i,offset,&stream);cursor+=4096;
 }
 return 0;
}
