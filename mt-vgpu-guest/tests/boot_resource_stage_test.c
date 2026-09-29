/* Real boot preparation with RAM-only VRAM/heap allocation models. */
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include "../kernel/mt_device_profile.h"
#define MT_VRAM_H
#define GFP_KERNEL 0
#define PAGE_SIZE 4096
#define SZ_1M 0x100000
#define SZ_512K 0x80000
#define SZ_2M 0x200000
#define MT_POOL_NORMAL 0
#define MT_POOL_PB 1
struct mt_vram_block {void *mapping;u64 gpu_pa;u32 size;};
struct mt_vram {bool region_owned;u64 next;};
static unsigned attempts,fail_at,allocations,frees;
static void *kzalloc(size_t n,int flags)
{void *p;(void)flags;if(++attempts==fail_at)return NULL;p=calloc(1,n);assert(p);allocations++;return p;}
static void kfree(void *p){if(p){free(p);frees++;}}
#define kvzalloc kzalloc
#define kvfree kfree
static int mt_vram_alloc(struct mt_vram *v,u32 pool,u32 bytes,u32 align,struct mt_vram_block *b)
{
 void *p=kzalloc(bytes,0);(void)pool;(void)align;
 if(!p)return -ENOMEM;
 memset(p,0xa5,bytes);*b=(struct mt_vram_block){.mapping=p,.gpu_pa=v->next,.size=bytes};
 v->next+=bytes;return 0;
}
static void mt_vram_free(struct mt_vram *v,struct mt_vram_block *b)
{(void)v;kfree(b->mapping);memset(b,0,sizeof(*b));}
#include "../kernel/mt_boot_resources.h"
int main(void)
{
 struct mt_vram v={.region_owned=true,.next=0x700000000ULL};
 struct mt_vram_block fw={.gpu_pa=0x500000000ULL,.size=MT_FW_MAP_SIZE};
 struct mt_vram_block tables={.gpu_pa=0x600000000ULL,.size=MT_FW_TABLE_BYTES};
 struct mt_device_profile p;struct mt_boot_resources boot={0};u32 i,j;
 assert(!mt_device_profile_select(&p,0x1ed5,0x222));
 for(i=1;i<=10;i++){
  attempts=0;fail_at=i;
  assert(mt_boot_resources_prepare(&v,&boot,&fw,&tables,&p)==-ENOMEM);
  assert(!boot.stage && !boot.prepared && allocations==frees);
  for(j=0;j<MT_BOOT_ALLOCATION_COUNT;j++)assert(!boot.blocks[j].mapping && !boot.blocks[j].size);
 }
 fail_at=0;p.family=6;
 assert(mt_boot_resources_prepare(&v,&boot,&fw,&tables,&p)==-EOPNOTSUPP);
 assert(!boot.stage && !boot.prepared && allocations==frees);
 p.family=2;
 assert(!mt_boot_resources_prepare(&v,&boot,&fw,&tables,&p));
 assert(boot.prepared && boot.stage && boot.table_pages==6);
 assert(!memcmp((u8 *)boot.stage+MT_BOOT_PDS_OFFSET,mt_tqx_program_bytes+MT_STATIC_USC_BYTES,MT_STATIC_PDS_BYTES));
 assert(!memcmp((u8 *)boot.stage+MT_BOOT_USC_OFFSET,mt_tqx_program_bytes,MT_STATIC_USC_BYTES));
 assert(MT_BOOT_STAGE_BYTES==MT_BOOT_KILL_OFFSET+MT_STATIC_RESOURCE_BYTES+20272);
 for(i=0;i<MT_BOOT_ALLOCATION_COUNT;i++)
  for(j=0;j<boot.blocks[i].size;j++)assert(((u8 *)boot.blocks[i].mapping)[j]==0xa5);
 assert(mt_boot_resources_prepare(&v,&boot,&fw,&tables,&p)==-EINVAL);
 mt_boot_resources_fini(&v,&boot);assert(allocations==frees && !boot.stage && !boot.prepared);
 puts("PASS: actual boot preparation stages separate static PDS/USC images, ten allocation failures, profile rollback, unchanged backing bytes and balanced ownership; RAM models only");
 return 0;
}
