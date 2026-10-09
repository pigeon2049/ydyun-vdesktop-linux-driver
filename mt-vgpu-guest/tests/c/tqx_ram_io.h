#ifndef MT_TEST_TQX_RAM_IO_H
#define MT_TEST_TQX_RAM_IO_H
/* Real upload/BO/VM code with RAM I/O callbacks; no device access. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../kernel/mt_tqx_upload.h"
/* mt_tqx_work.h uses these OS primitives through its production boot-BO
 * dependency. Keep only that device-specific layer modeled in RAM tests. */
#define MT_TEST_GFP_KERNEL 0
#define GFP_KERNEL MT_TEST_GFP_KERNEL
#define kzalloc(bytes, flags) ((void)(flags), calloc(1, (bytes)))
#define kfree(pointer) free(pointer)
#define lockdep_assert_held(lock) ((void)(lock))
#undef WARN_ON
#define WARN_ON(condition) ((condition) ? (assert(!(condition)), 1) : 0)
#include "mt_boot_bo_model.h"
struct store { u64 next; unsigned allocs,frees,maps,map_fail,writes,reads,write_fail,read_fail,corrupt; };
static int alloc(void *opaque,u32 n,u32 align,struct mt_bo_backing *b)
{
 struct store *s=opaque;void *p=malloc(n);if(!p)return -ENOMEM;
 s->next=(s->next+align-1)&~(u64)(align-1);
 *b=(struct mt_bo_backing){p,s->next,s->next,n};s->next+=n;s->allocs++;return 0;
}
static int clear(void *s,const struct mt_bo_backing *b) { (void)s;memset(b->handle,0,b->bytes);return 0; }
static void release(void *opaque,const struct mt_bo_backing *b) { struct store *s=opaque;free(b->handle);s->frees++; }
static int map(void *opaque,const struct mt_bo_backing *b,void **out)
{ struct store *s=opaque;if(++s->maps==s->map_fail)return -ENXIO;*out=b->handle;return 0; }
static void unmap(void *s,const struct mt_bo_backing *b) { (void)s;(void)b; }
static const struct mt_bo_ops bo_ops={alloc,clear,release,map,unmap};
static int write_bo(struct mt_bo *bo,u64 offset,const void *data,u64 n)
{
 struct store *s=bo->store;void *mapping;int ret;
 if(++s->writes==s->write_fail)return -EIO;
 ret=mt_bo_check_range(bo,offset,n);if(ret)return ret;
 ret=mt_bo_cpu_begin(bo,&mapping);if(ret)return ret;
 memcpy((u8 *)mapping+offset,data,n);return mt_bo_cpu_end(bo);
}
static int read_bo(struct mt_bo *bo,u64 offset,void *data,u64 n)
{
 struct store *s=bo->store;void *mapping;int ret;
 if(++s->reads==s->read_fail)return -EIO;
 ret=mt_bo_check_range(bo,offset,n);if(ret)return ret;
 ret=mt_bo_cpu_begin(bo,&mapping);if(ret)return ret;
 memcpy(data,(u8 *)mapping+offset,n);
 if(s->reads==s->corrupt)((u8 *)data)[n-1]^=1;
 return mt_bo_cpu_end(bo);
}
static const struct mt_tqx_upload_ops io={write_bo,read_bo};
#endif
