#include <stdlib.h>
#include "../../kernel/mt_process_resources.h"

/* Export real binder output for comparison with original per-page calls. */
int shared_ranges(u64 *out, u32 mask, u32 dynamic)
{
	const u32 indices[]={0,4,10,11,6,3};
	const u32 sizes[]={0x200000,0x100000,0x80000,0x80000,4096,0x400000};
	struct mt_bo tables={.refs=1,.backing={.gpu_pa=0x600000000ULL,.bytes=65536}};
	struct mt_bo resources[9]={0},*bo[9];
	struct mt_guest_pool_spec specs[MT_GUEST_POOL_COUNT];
	struct mt_gpu_vm vm={0};
	struct mt_device_profile p;
	void *image=calloc(1,65536),*scratch=calloc(1,65536);
	u32 i;int ret;
	if(!image||!scratch){ret=-ENOMEM;goto done;}
	mt_device_profile_select(&p,0x1ed5,0x222);
	ret=mt_gpu_vm_init(&vm,&tables,image,scratch,65536);
	if(ret)goto done;
	for(i=0;i<6;i++){
		resources[i].refs=1;resources[i].backing.bytes=sizes[i];
		resources[i].backing.gpu_pa=0x610000000ULL+indices[i]*0x800000ULL;
		bo[i]=&resources[i];
	}
	if(!(mask&1))bo[0]=NULL;
	if(!(mask&4))bo[4]=NULL;
	mt_guest_plan_pools(specs);
	for(i=0;i<MT_GUEST_POOL_COUNT;i++){
		resources[6+i].refs=1;resources[6+i].backing.bytes=specs[i].bytes;
		resources[6+i].backing.gpu_pa=0x680000000ULL+specs[i].heap*0x800000ULL;
		bo[6+i]=&resources[6+i];
	}
	ret=mt_process_resources_bind_pools(&vm,&p,bo,dynamic?bo+6:NULL);
	if(!ret){
		for(i=0;i<vm.count;i++){
			out[i*4]=vm.bindings[i].va;out[i*4+1]=vm.bindings[i].bo->backing.gpu_pa;
			out[i*4+2]=vm.bindings[i].bytes;out[i*4+3]=vm.bindings[i].flags;
		}
		ret=vm.count;
	}
	/* Owner refs keep stack BOs alive: no backend free callback is invoked. */
	mt_gpu_vm_fini(&vm);
done:
	free(image);free(scratch);return ret;
}
