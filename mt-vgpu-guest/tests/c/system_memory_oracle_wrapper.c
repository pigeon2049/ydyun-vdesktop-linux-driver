#include <stdlib.h>
#include "../../kernel/mt_system_address.h"
#include "../../kernel/mt_gpu_vm.h"

int system_translate(const void *info, const void *windows, const u64 *gpa,
                     u32 count, u64 *out, u64 bar4, u64 bar4_bytes)
{
 struct mt_system_address a;
 u32 i;int ret=mt_system_address_init(&a,info,0xcc8,windows,MT_GUEST_WINDOWS_BYTES,bar4,bar4_bytes);
 if(ret)return ret;
 for(i=0;i<count;i++){ret=mt_system_page_address(&a,gpa[i],&out[i]);if(ret)return ret;}
 return 0;
}

/* Bind an offset slice of scatter RAM, then walk the real VM independently. */
int system_vm(const u64 *pa, u32 count, u32 first, u32 pages, u64 *out)
{
 const u64 root=0x600000000ULL,va=0x81ff800000ULL;
 struct mt_bo tables={.refs=1,.backing={.gpu_pa=root,.bytes=65536}};
 struct mt_bo ram={.refs=1,.backing={.gpu_pa=pa[0],.bytes=count*4096},.page_pa=pa};
 struct mt_gpu_vm vm={0};
 void *image=calloc(1,65536),*scratch=calloc(1,65536);
 u32 i,pc;u64 pd,pte,offset;int ret;
 if(!image||!scratch){ret=-ENOMEM;goto done;}
 ret=mt_gpu_vm_init(&vm,&tables,image,scratch,65536);if(ret)goto done;
 ret=mt_gpu_vm_bind(&vm,&ram,va,first*4096,pages*4096,0);if(ret)goto finish;
 for(i=0;i<pages;i++){
  u64 address=va+i*4096ULL;
  memcpy(&pc,(u8 *)image+((address>>30)&1023)*4,4);
  offset=((u64)(pc&0xfffffff0U)<<8)-root;if(!(pc&1)||offset>65536-4096){ret=-EFAULT;goto finish;}
  memcpy(&pd,(u8 *)image+offset+((address>>21)&511)*8,8);
  offset=(pd&0xfffffff000ULL)-root;if(!(pd&1)||offset>65536-4096){ret=-EFAULT;goto finish;}
  memcpy(&pte,(u8 *)image+offset+((address>>12)&511)*8,8);
  if((pte&7)!=1 || (pte&(1ULL<<62))){ret=-EFAULT;goto finish;}
  out[i]=pte&0xfffffff000ULL;
 }
finish:
 mt_gpu_vm_fini(&vm);
 if(ram.refs!=1||tables.refs!=1)ret=-EFAULT;
done:
 free(image);free(scratch);return ret;
}
