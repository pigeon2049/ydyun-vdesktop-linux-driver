/* Compile the actual main-module submission gate extracted by the verifier.
 * The nonzero trial offset detects accidentally passing mt_guest to a callback
 * that expects mt_fw_trial; no hardware or other probe code executes. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include "../kernel/mt_fw_event.h"
#define READ_ONCE(x) (x)
#define lockdep_assert_held(p) assert(*(p))
struct mt_runtime_context { bool published; int event_result; };
struct mt_fw_trial { bool pinned, connected; u32 firmware, started; };
struct mt_rpc_service { bool running; };
struct mt_guest {
	u8 padding[32];
	struct mt_fw_trial trial;
	struct mt_runtime_context runtime;
	struct mt_rpc_service service;
	bool trial_lock;
	u8 *regs;
	unsigned long channel[1];
	u32 normal;
};
static struct mt_guest *fixture;
static unsigned trial_callbacks, register_reads;
static struct mt_runtime_context *mt_runtime(struct mt_guest *g) { return &g->runtime; }
static struct mt_rpc_service *mt_service(struct mt_guest *g) { return &g->service; }
static u32 mt_guest_trial_gpu_normal(void *p)
{ assert(p == &fixture->trial); trial_callbacks++; return fixture->normal; }
static u32 mt_trial_fw_state(void *p)
{ assert(p == &fixture->trial); trial_callbacks++; return fixture->trial.firmware; }
static u32 mt_trial_started(void *p)
{ assert(p == &fixture->trial); trial_callbacks++; return fixture->trial.started; }
static u32 readl(void *p)
{ u32 value; assert(p == fixture->regs + 0x890); register_reads++; memcpy(&value, p, 4); return value; }
#include <runtime_submit_gate.h>

int main(void)
{
	u8 regs[4096] = {0};
	u32 channel[1024] = {0}, state = 2;
	struct mt_guest g = {.trial = {true, true, 2, 1}, .runtime = {true, 0},
		.service = {true}, .trial_lock = true, .regs = regs,
		.channel = {(unsigned long)channel}, .normal = 1};
	fixture = &g;
	memcpy(regs + 0x890, &state, 4);
	assert(!mt_runtime_can_submit(&g) && trial_callbacks == 3 && register_reads == 1);
	channel[0x330 / 4] = 1;
	assert(mt_runtime_can_submit(&g) == -EAGAIN);
	channel[0x330 / 4] = 0x80000001;
	assert(!mt_runtime_can_submit(&g));
	g.normal = 0;
	assert(mt_runtime_can_submit(&g) == -EHOSTDOWN);
	g.normal = 1;
	g.trial.firmware = 1;
	assert(mt_runtime_can_submit(&g) == -EHOSTDOWN);
	g.trial.firmware = 2;
	g.trial.started = 0;
	assert(mt_runtime_can_submit(&g) == -EHOSTDOWN);
	g.trial.started = 1;
	g.runtime.published = false;
	trial_callbacks = register_reads = 0;
	assert(mt_runtime_can_submit(&g) == -EHOSTDOWN && !trial_callbacks && !register_reads);
	puts("PASS: actual main-module submission gate, correct trial callback pointers, shared pause and unpublished no-read path");
	return 0;
}
