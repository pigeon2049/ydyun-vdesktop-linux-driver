#include "../../kernel/mt_ce_stream.h"
int prepare(void *out, u32 capacity, const struct mt_ce3_stream_input *in)
{
 struct mt_bo table={.refs=1}, command={.refs=1}, src={.refs=1}, dst={.refs=1};
 /* The binding table is heap-owned, so build it through the public bind path.
  * These fixtures skip mt_gpu_vm_init because they own no table image; only
  * the binding view matters to the encoder under test. */
 struct mt_gpu_vm vm={.tables=&table,.max_ranges=mt_boot_max_ranges(64)};
 struct mt_vm_binding seed[3];
 command.backing=(struct mt_bo_backing){.bytes=4096,.gpu_pa=0x600000000ULL};
 src.backing=(struct mt_bo_backing){.bytes=4096,.gpu_pa=0x600001000ULL};
 dst.backing=(struct mt_bo_backing){.bytes=4096,.gpu_pa=0x600002000ULL};
 seed[0]=(struct mt_vm_binding){&command,in->command_va & ~4095ULL,0,4096,0};
 seed[1]=(struct mt_vm_binding){&src,in->copy.src,0,4096,0};
 seed[2]=(struct mt_vm_binding){&dst,in->copy.dst,0,4096,0};
 if(mt_gpu_vm_bind_many(&vm,seed,3))return -ENOMEM;
 vm.tables=&table;vm.uploaded=true;
 return mt_ce3_stream_prepare(out,capacity,&vm,&command,&src,&dst,in);
}
