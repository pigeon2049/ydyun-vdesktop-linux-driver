#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../kernel/mt_tqx_dma.h"
int main(void)
{
 struct mt_device_profile profile;
 struct mt_tqx_heap_input heap={.copy={0x40100001,0x40200000,8191},
  .va={0x40000000,0x8400000000ULL,0x8100000000ULL,0x8100001000ULL,0xf000000000ULL}};
 struct mt_tqx_copy_stream_input request;struct mt_tqx_copy_stream_image stream;
 struct mt_tqx_upload_result source;
 struct mt_tqx_dma_input in={0x40010000,0x40020000,1};
 struct mt_tqx_engine_state_plan state,previous;
 struct mt_tqx_dma_image *image=malloc(sizeof(*image)+16),*saved=malloc(sizeof(*saved));
 u32 i;u64 field;
 assert(image && saved && !mt_device_profile_select(&profile,0x1ed5,0x222));
 for(i=1;i<=8;i++) {
  assert(!mt_tqx_engine_state_plan(&state,&profile,i,0x8040000000ULL-4096));
  assert(state.required_bytes==i*384 && state.allocation_bytes==4096 &&
   state.reference_alignment==128 && !state.reserved && state.encoded_va==0x8040000000ULL-4096);
 }
 previous=state;
 assert(mt_tqx_engine_state_plan(&state,&profile,1,1ULL<<40)==-ERANGE);
 assert(!memcmp(&state,&previous,sizeof(state)));
 assert(mt_tqx_engine_state_plan(&state,NULL,1,0x40020000)==-EOPNOTSUPP);
 assert(!mt_tqx_heap_stream_input(&request,sizeof(request),&profile,&heap));
 assert(!mt_tqx_copy_stream_build(&stream,sizeof(stream),&request));
 memcpy(source.record,stream.record,296);memcpy(source.page_record,stream.page_record,16);
 memcpy(source.root_export,stream.root_export,16);memset(image,0xa5,sizeof(*image)+16);
 assert(!mt_tqx_dma_encode(image,sizeof(*image)+16,&profile,&source,&in));*saved=*image;
 assert(image->bytes==0x1300 && image->descriptor[0x1c]==0x67);
 memcpy(&field,image->descriptor+0x160,8);assert(field==heap.va[0]);
 memcpy(&field,image->descriptor+0x168,8);assert(field==in.dma_va+0x200);
 memcpy(&field,image->descriptor+0x170,8);assert(field==in.state_va);
 for(i=0;i<16;i++)assert(((u8 *)(image+1))[i]==0xa5);
 assert(mt_tqx_dma_encode(image,sizeof(*image),NULL,&source,&in)==-EOPNOTSUPP);
 profile.family=3;
 assert(mt_tqx_dma_encode(image,sizeof(*image),&profile,&source,&in)==-EOPNOTSUPP);profile.family=2;
 source.record[0x28]=1;
 assert(mt_tqx_dma_encode(image,sizeof(*image),&profile,&source,&in)==-EOPNOTSUPP);source.record[0x28]=0;
 assert(!memcmp(image,saved,sizeof(*image)));
 /* Entire allocation ends exactly at the ordinary heap boundary. */
 in.dma_va=0x8040000000ULL-8192;in.cores=8;
 assert(!mt_tqx_dma_encode(image,sizeof(*image),&profile,&source,&in));
 assert(image->bytes==0x1680 && image->descriptor[0x580]==240);
 in.dma_va+=4096;*saved=*image;
 assert(mt_tqx_dma_encode(image,sizeof(*image),&profile,&source,&in)==-ERANGE);
 assert(!memcmp(image,saved,sizeof(*image)));
 free(saved);free(image);
 puts("PASS: TQX DMA profile/record gates, output guards, separate engine-state pointer and full-allocation heap boundary; CPU only");
 return 0;
}
