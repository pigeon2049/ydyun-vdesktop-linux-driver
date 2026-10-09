/* CPU program/state staging; no device execution. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../kernel/mt_tqx_program.h"
#include "../../kernel/mt_tqx_heap.h"
int main(void)
{
	struct mt_device_profile p;
	struct mt_tqx_program_state state, saved;
	struct mt_tqx_program_input in = {.operation=8,.constants_va=0x1234567000ULL,
		.shader_heap_base=0x10000,.source_descriptor_index=0x87654321};
	u8 *bank=malloc(MT_TQX_PROGRAM_BANK_BYTES+16);
	u32 operation, i;
	assert(bank && !mt_device_profile_select(&p,0x1ed5,0x222));
	memset(bank,0xa5,MT_TQX_PROGRAM_BANK_BYTES+16);
	assert(!mt_tqx_program_bank_build(bank,MT_TQX_PROGRAM_BANK_BYTES+16,&p));
	for(i=0;i<16;i++)assert(bank[MT_TQX_PROGRAM_BANK_BYTES+i]==0xa5);
	assert(mt_tqx_program_bank_build(bank,MT_TQX_PROGRAM_BANK_BYTES-1,&p)==-EINVAL);
	assert(!mt_device_profile_select(&p,0x1ed5,0x600));
	assert(mt_tqx_program_bank_build(bank,MT_TQX_PROGRAM_BANK_BYTES,&p)==-EOPNOTSUPP);
	assert(!memcmp(bank,mt_tqx_program_bytes,MT_TQX_PROGRAM_BANK_BYTES));
	for(operation=0;operation<128;operation++) {
		memset(&state,0xa5,sizeof(state));saved=state;in.operation=operation;
		if(operation==8 || operation==16 || operation==21) {
			assert(!mt_tqx_program_state_build(&state,sizeof(state),&in));
			assert(state.constants_dwords==5 && state.pds_constants_dwords==4);
		} else {
			assert(mt_tqx_program_state_build(&state,sizeof(state),&in)==-EOPNOTSUPP);
			assert(!memcmp(&state,&saved,sizeof(state)));
		}
	}
	in.operation=8;
	assert(!mt_tqx_program_state_build(&state,sizeof(state),&in));saved=state;
	for(i=1;i<128;i++) {
		in.shader_heap_base=0x10000+i;
		assert(mt_tqx_program_state_build(&state,sizeof(state),&in)==-EINVAL);
		assert(!memcmp(&state,&saved,sizeof(state)));
	}
	in.shader_heap_base=0xffffff80;
	assert(mt_tqx_program_state_build(&state,sizeof(state),&in)==-ERANGE);
	in.shader_heap_base=0x10000;in.constants_va=(1ULL<<40)-4;
	assert(mt_tqx_program_state_build(&state,sizeof(state),&in)==-ERANGE);
	assert(!memcmp(&state,&saved,sizeof(state)));
	{
		struct mt_tqx_texture_input req={.source_va=0x100001,.width=16,.height=16};
		u8 texture[80], old[80];
		for(i=0;i<=32;i++) {
			req.element_bytes=i;memset(texture,0xa5,sizeof(texture));memcpy(old,texture,sizeof(old));
			if(i && i<=16 && !(i&(i-1))) {
				assert(!mt_tqx_texture_build(texture,sizeof(texture),&req));
				assert(!memcmp(texture+64,old+64,16));
			} else {
				assert(mt_tqx_texture_build(texture,sizeof(texture),&req)==-EINVAL);
				assert(!memcmp(texture,old,sizeof(old)));
			}
		}
		req.element_bytes=16;req.width=32768;req.height=32768;
		assert(!mt_tqx_texture_build(texture,sizeof(texture),&req));memcpy(old,texture,sizeof(old));
		req.source_va=(1ULL<<40)-1;
		assert(mt_tqx_texture_build(texture,sizeof(texture),&req)==-ERANGE);
		req.source_va=0;req.width++;
		assert(mt_tqx_texture_build(texture,sizeof(texture),&req)==-EINVAL);
		assert(!memcmp(texture,old,sizeof(old)));
	}
	{
		struct mt_tqx_job_input good={.source_va=0x100001,.destination_va=0x200003,
			.constants_va=0x300000,.element_bytes=16,.width=32768,.height=32768,
			.shader_heap_base=0x10000,.source_descriptor_index=0x1234,
			.pds_code_heap_base=0x7000,.pds_execution_state=0x9000,.pds_constant_state=0xc000};
		struct mt_tqx_job_input req;
		u8 image[sizeof(struct mt_tqx_job_image)+16], old[sizeof(image)];
		_Static_assert(sizeof(struct mt_tqx_job_image)==240,"job image layout");
		memset(image,0xa5,sizeof(image));
		assert(!mt_tqx_job_build(image,sizeof(image),&good));
		for(i=240;i<sizeof(image);i++)assert(image[i]==0xa5);
		memcpy(old,image,sizeof(old));
		/* Fail at successive stages, including after texture/program construction:
		 * no partial output from late address/geometry rejection may escape. */
		for(i=0;i<12;i++) {
			req=good;
			switch(i) {
			case 0:req.source_va=(1ULL<<40)-1;break;
			case 1:req.destination_va=(1ULL<<40)-1;break;
			case 2:req.constants_va=(1ULL<<40)-4;break;
			case 3:req.element_bytes=3;break;
			case 4:req.width=32769;break;
			case 5:req.height=0;break;
			case 6:req.shader_heap_base=0xffffff80;break;
			case 7:req.pds_code_heap_base=0xfffffff0;break;
			case 8:req.pds_execution_state=0xfffffff0;break;
			case 9:req.pds_execution_state++;break;
			case 10:req.pds_constant_state++;break;
			case 11:req.pds_code_heap_base++;break;
			}
			assert(mt_tqx_job_build(image,sizeof(image),&req)<0);
			assert(!memcmp(image,old,sizeof(old)));
		}
		assert(mt_tqx_job_build(image,239,&good)==-EINVAL);
		assert(mt_tqx_job_build(image,sizeof(image),NULL)==-EINVAL);
		assert(mt_tqx_job_build(NULL,sizeof(image),&good)==-EINVAL);
		assert(!memcmp(image,old,sizeof(old)));
		/* Exact upper range endpoint is valid, not an overflowing span. */
		good.element_bytes=1;good.width=1;good.height=1;
		good.source_va=good.destination_va=(1ULL<<40)-1;
		good.pds_execution_state=0xffffffd0;good.pds_constant_state=0xfffffff0;
		assert(!mt_tqx_job_build(image,sizeof(image),&good));
	}
	{
		struct mt_tqx_stream_input good={.job={.source_va=0x100001,.destination_va=0x200003,
			.constants_va=0x300000,.element_bytes=4,.width=17,.height=32,
			.shader_heap_base=0x10000,.source_descriptor_index=0x1234,
			.pds_code_heap_base=0x7000,.pds_execution_state=0xa000,.pds_constant_state=0xd000},
			.command_va=0x400000,.pds_initial_state=0x9000}, req;
		struct mt_tqx_stream_image *image=malloc(sizeof(*image)+16), saved;
		_Static_assert(sizeof(struct mt_tqx_stream_image)==568,"stream layout");
		assert(image);memset(image,0xa5,sizeof(*image)+16);
		assert(!mt_tqx_stream_build(image,sizeof(*image)+16,&good));saved=*image;
		assert(image->job.destination[72]==0x2d && image->record[0x30]==96);
		assert(image->record[0x124]==80 && image->record[0xa0]==1 && image->page_record[8]==80);
		for(i=0;i<16;i++)assert(((u8 *)(image+1))[i]==0xa5);
		for(i=0;i<8;i++) {
			req=good;
			switch(i) {
			case 0:req.command_va=0;break;
			case 1:req.command_va=1;break;
			case 2:req.command_va=1ULL<<40;break;
			case 3:req.pds_initial_state++;break;
			case 4:req.job.pds_code_heap_base=0xfffffff0;break;
			case 5:req.job.destination_va=(1ULL<<40)-1;break;
			case 6:req.job.constants_va=1;break;
			case 7:req.job.pds_constant_state++;break;
			}
			assert(mt_tqx_stream_build(image,sizeof(*image),&req)<0);
			assert(!memcmp(image,&saved,sizeof(saved)));
		}
		assert(mt_tqx_stream_build(image,sizeof(*image)-1,&good)==-EINVAL);
		assert(mt_tqx_stream_build(image,sizeof(*image),NULL)==-EINVAL);
		assert(mt_tqx_stream_build(NULL,sizeof(*image),&good)==-EINVAL);
		assert(!memcmp(image,&saved,sizeof(saved)));
		good.command_va=(1ULL<<40)-4096;good.pds_initial_state=0xfffffff0;
		assert(!mt_tqx_stream_build(image,sizeof(*image),&good));
		free(image);
	}
	{
		struct mt_tqx_copy_stream_input req={.copy={0x100001,0x200000000ULL,0xffffffffULL},
			.command_va=0x400000,.shader_heap_base=0x10000,.pds_code_heap_base=0x7000,
			.pds_initial_state=0x9000};
		struct mt_tqx_copy_stream_image *image=malloc(sizeof(*image)+16), *saved=malloc(sizeof(*image));
		_Static_assert(sizeof(struct mt_tqx_copy_stream_image)==1312,"copy stream layout");
		assert(image && saved);
		for(i=0;i<4;i++)req.addresses[i]=(struct mt_tqx_chunk_addresses){
			.constants_va=0x300000+i*4096,.source_descriptor_index=0x1234+i*256,
			.pds_execution_state=0xa000+i*16384,.pds_constant_state=0xd000+i*16384};
		memset(image,0xa5,sizeof(*image)+16);
		assert(!mt_tqx_copy_stream_build(image,sizeof(*image)+16,&req));*saved=*image;
		assert(image->count==4 && image->command_bytes==320 && image->root_export[0]==1);
		for(i=0;i<4;i++)assert(image->commands[i*80+72]==(i==3?0x2d:0x25));
		for(i=0;i<16;i++)assert(((u8 *)(image+1))[i]==0xa5);
		for(i=0;i<4;i++) {
			u64 old_va=req.addresses[i].constants_va;
			req.addresses[i].constants_va=1;
			assert(mt_tqx_copy_stream_build(image,sizeof(*image),&req)==-EINVAL);
			assert(!memcmp(image,saved,sizeof(*saved)));
			req.addresses[i].constants_va=old_va;
		}
		assert(mt_tqx_copy_stream_build(image,sizeof(*image)-1,&req)==-EINVAL);
		assert(mt_tqx_copy_stream_build(image,sizeof(*image),NULL)==-EINVAL);
		assert(!memcmp(image,saved,sizeof(*saved)));
		/* An inactive address slot is irrelevant, and stale output must clear
		 * when replacing a four-job image with a one-job image. */
		req.copy.bytes=1;req.addresses[3].constants_va=1;
		assert(!mt_tqx_copy_stream_build(image,sizeof(*image),&req));
		assert(image->count==1 && image->command_bytes==80 && image->commands[72]==0x2d);
		for(i=80;i<320;i++)assert(!image->commands[i]);
		for(i=1;i<4;i++) {
			u8 zero[96]={0};
			assert(!memcmp(image->sources[i],zero,64));
			assert(!memcmp(&image->programs[i],zero,96));
		}
		free(saved);free(image);
	}
 {
  struct mt_tqx_heap_input req={.copy={0x40100001,0x40200000,8191},
   .va={0x40000000,0x8400000000ULL,0x8100000000ULL,0x8100001000ULL,0xf000000000ULL}};
  struct mt_tqx_copy_stream_input input,old;
  assert(!mt_device_profile_select(&p,0x1ed5,0x222));
  assert(!mt_tqx_heap_stream_input(&input,sizeof(input),&p,&req));old=input;
  assert(input.shader_heap_base==0 && input.pds_code_heap_base==0 && input.pds_initial_state==4096);
  assert(input.addresses[0].constants_va==req.va[3]+52 && input.addresses[0].source_descriptor_index==0);
  assert(mt_tqx_heap_stream_input(&input,sizeof(input)-1,&p,&req)==-EINVAL);
  assert(mt_tqx_heap_stream_input(&input,sizeof(input),NULL,&req)==-EOPNOTSUPP);
  assert(mt_tqx_heap_stream_input(&input,sizeof(input),&p,NULL)==-EINVAL);
  assert(mt_tqx_heap_stream_input(NULL,sizeof(input),&p,&req)==-EINVAL);
  p.family=3;
  assert(mt_tqx_heap_stream_input(&input,sizeof(input),&p,&req)==-EOPNOTSUPP);
  assert(!memcmp(&input,&old,sizeof(old)));
 }
	free(bank);
	puts("PASS: TQX program bank, combined job and 1-4 chunk streams/root export: platform/format gates, output guards, transactional late failures, unaligned addresses and range endpoints; CPU only");
	return 0;
}
