/* Real system-memory preparation with modeled allocation and page primitives. */
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include "../kernel/mt_system_address.h"
#define GFP_KERNEL 0
#define PAGE_SIZE 4096
struct page { u64 gpa; };
static unsigned allocations,frees,fail_at,attempts,lookups,fail_page;
static bool virtual_mapping;
static u64 bad_gpa;
static void *cpu;
static void *kvzalloc(size_t n,int flags)
{
 void *p;(void)flags;if(++attempts==fail_at)return NULL;
 p=calloc(1,n);assert(p);allocations++;cpu=p;return p;
}
static void *kvcalloc(size_t n,size_t size,int flags)
{
 void *p;(void)flags;if(++attempts==fail_at)return NULL;
 p=calloc(n,size);assert(p);allocations++;return p;
}
static void kvfree(void *p){if(p){frees++;free(p);}}
static bool is_vmalloc_addr(const void *p){(void)p;return virtual_mapping;}
static struct page *lookup(void *p)
{
 static struct page page;u64 i=((char *)p-(char *)cpu)/4096;
 lookups++;if(lookups==fail_page && !bad_gpa)return NULL;
 page.gpa=lookups==fail_page?bad_gpa:0x10000000+i*8192;return &page;
}
static struct page *vmalloc_to_page(void *p){assert(virtual_mapping);return lookup(p);}
static struct page *virt_to_page(void *p){assert(!virtual_mapping);return lookup(p);}
#define page_to_phys(p) ((p)->gpa)
#include "../kernel/mt_system_memory.h"

int main(void)
{
 const struct mt_system_address address={.bar2=0x800000000,.bar2_bytes=0x400000000,.bias=0x8800000000};
 struct mt_system_memory m={0};u32 i,j;
 for(i=1;i<=2;i++){
  attempts=0;fail_at=i;
  assert(mt_system_memory_prepare(&m,0x400000,&address)==-ENOMEM);
  assert(!m.cpu && !m.page_pa && !m.bytes && allocations==frees);
 }
 fail_at=0;
 for(i=0;i<2;i++){
  virtual_mapping=i;lookups=0;fail_page=1024;bad_gpa=0;
  assert(mt_system_memory_prepare(&m,0x400000,&address)==-EFAULT);
  assert(!m.cpu && !m.page_pa && !m.bytes && allocations==frees);
  lookups=0;bad_gpa=0x800000000;
  assert(mt_system_memory_prepare(&m,0x400000,&address)==-EXDEV);
  assert(!m.cpu && !m.page_pa && !m.bytes && allocations==frees);
  lookups=0;fail_page=0;
  assert(!mt_system_memory_prepare(&m,0x400000,&address) && lookups==1024);
  assert(mt_system_memory_prepare(&m,4096,&address)==-EINVAL);
  for(j=0;j<m.bytes;j++)assert(!((u8 *)m.cpu)[j]);
  for(j=0;j<1024;j++)assert(m.page_pa[j]==0x8810000000ULL+j*8192);
  mt_system_memory_fini(&m);assert(allocations==frees && !m.bytes && !m.cpu && !m.page_pa);
  mt_system_memory_fini(&m);assert(allocations==frees);
 }
 assert(mt_system_memory_prepare(&m,0,&address)==-EINVAL);
 assert(mt_system_memory_prepare(&m,4097,&address)==-EINVAL);
 puts("PASS: system-RAM preparation, kmalloc/vmalloc page paths, both allocation failures, final-page lookup/translation failures, zero policy and balanced ownership; OS primitives modeled");
 return 0;
}
