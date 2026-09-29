#include "../kernel/mt_tqx_program.h"
#include "../kernel/mt_tqx_texture.h"
#include "../kernel/mt_tqx_job.h"
int build_bank(void *out, u32 capacity, u32 family)
{
	struct mt_device_profile p = {.family=family,.transfer_version=1};
	return mt_tqx_program_bank_build(out, capacity, &p);
}
int build_state(void *out, u32 capacity, const struct mt_tqx_program_input *in)
{
	return mt_tqx_program_state_build(out, capacity, in);
}
int build_texture(void *out, u32 capacity, const struct mt_tqx_texture_input *in)
{
	return mt_tqx_texture_build(out, capacity, in);
}
int build_job(void *out, u32 capacity, const struct mt_tqx_job_input *in)
{
	return mt_tqx_job_build(out, capacity, in);
}
