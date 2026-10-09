/* Execute the actual trial orchestration on RAM. OS/MMIO primitives are
 * modeled; this verifies ownership transitions, not hardware or DMA ordering. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../kernel/mt_fw_connection.h"
#define MT_GUEST_FW_QUEUE_IO_H
#define MT_GUEST_BOOT_RESOURCES_H
#define __iomem
#define PAGE_SIZE 4096U
#define GFP_KERNEL 0
#define THIS_MODULE 0
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define WARN_ON(x) (x)
#define current NULL
#define MT_MMU_DUMMY_BYTES 0x3000U
#define MT_STATIC_RESOURCE_BYTES 0x80000U
#define MT_BOOT_DUMMY_OFFSET MT_FW_TABLE_BYTES
#define MT_BOOT_YUV_OFFSET (MT_BOOT_DUMMY_OFFSET + MT_MMU_DUMMY_BYTES)
#define MT_BOOT_KILL_OFFSET (MT_BOOT_YUV_OFFSET + MT_STATIC_RESOURCE_BYTES)
enum { MT_BOOT_DUMMY, MT_BOOT_YUV, MT_BOOT_KILL };
struct mt_vram_block { void *mapping; u32 size; u64 gpu_pa; };
struct mt_fw_queue_io { void *queue, *registers; };
struct mt_boot_resources { bool prepared; void *stage; struct mt_vram_block blocks[3]; };
static const struct mt_fw_queue_ops mt_fw_io_ops = {0};
static unsigned refs, connects, disconnects;
static bool accept;
static inline void __module_get(int unused) { (void)unused; refs++; }
static inline void module_put(int unused) { (void)unused; assert(refs); refs--; }
static inline void *kmalloc(size_t n, int flags) { (void)flags; return malloc(n); }
#define kvmalloc kmalloc
#define kfree free
#define kvfree free
#define memcpy_fromio memcpy
#define memcpy_toio memcpy
static inline void mb(void) { }
static inline u32 readl(const void *p) { u32 v; memcpy(&v, p, 4); return v; }
static inline void writel(u32 v, void *p) { memcpy(p, &v, 4); }
static inline void writeq(u64 v, void *p) { memcpy(p, &v, 8); }
static inline u32 task_tgid_nr(void *p) { (void)p; return 7; }
static inline int mt_fw_queue_try_submit(struct mt_fw_queue_io *q, u32 a, u32 b, const void *c)
{ (void)q; (void)a; (void)b; (void)c; return 0; }
static inline int mt_init_static_resources(void *a, u32 b, void *c, u32 d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }
#include "../../kernel/mt_fw_trial.h"

static u32 healthy(void *p) { (void)p; return 1; }
static int idle(void *p) { (void)p; return 1; }
static int control_idle(void *p) { return mt_trial_fw_state(p) == 2; }
static void delay(void *p) { (void)p; }
static int send(void *p, u32 opcode)
{
	struct mt_fw_trial *t = p;
	if (opcode == MT_FW_CONNECT) {
		connects++;
		if (accept) {
			writel(2, t->queue->registers + 0x898);
			writel(1, t->upload[0].block->mapping + 4);
		}
	} else {
		assert(opcode == MT_FW_DISCONNECT);
		disconnects++;
	}
	return 0;
}
static const struct mt_fw_connection_ops ops = {
	mt_trial_fw_state, mt_trial_started, mt_trial_guest_state, mt_trial_online,
	send, idle, control_idle, delay, healthy,
};

int main(void)
{
	const u32 sizes[] = {MT_FW_MAP_SIZE, MT_FW_TABLE_BYTES, PAGE_SIZE,
		MT_MMU_DUMMY_BYTES, MT_STATIC_RESOURCE_BYTES, MT_STATIC_RESOURCE_BYTES};
	unsigned scenario, i;
	{
		struct mt_fw_trial t = {0};
		struct mt_fw_queue_io queue = {.queue = calloc(1, MT_FW_QUEUE_BYTES)};
		u32 cursor = MT_FW_CURSOR_OFFSET + 32;
		assert(queue.queue);
		t.queue = &queue;
		memset(queue.queue + MT_FW_EVENT_OFFSET, 0x5a, 2 * MT_FW_EVENT_BYTES);
		writel(2, queue.queue + cursor);
		t.event_count = 127;
		assert(mt_trial_drain_events(&t) == -ENOSPC);
		assert(readl(queue.queue + cursor + 8) == 0 && t.event_count == 127);
		t.event_count = 0;
		assert(!mt_trial_drain_events(&t));
		assert(t.event_count == 2 && readl(queue.queue + cursor + 8) == 2);
		assert(t.events[0].dm == 0 && t.events[0].words[0] == 0x5a5a5a5a);
		writel(64, queue.queue + cursor);
		assert(mt_trial_drain_events(&t) == -EIO && t.event_count == 2);
		free(queue.queue);
	}
	for (scenario = 0; scenario < 3; scenario++) {
		struct mt_fw_trial t = {0};
		struct mt_vram_block blocks[6] = {0};
		u8 regs[4096] = {0}, custom[4096] = {0};
		struct mt_fw_queue_io queue = {.registers = regs};
		t.queue = &queue;
		t.custom = custom;
		t.scratch = malloc(PAGE_SIZE);
		assert(t.scratch);
		for (i = 0; i < 6; i++) {
			blocks[i].size = sizes[i];
			blocks[i].mapping = malloc(sizes[i]);
			blocks[i].gpu_pa = 0x600000000ULL + i * 0x1000000;
			t.upload[i].block = &blocks[i];
			t.upload[i].before = malloc(sizes[i]);
			t.upload[i].image = malloc(sizes[i]);
			assert(blocks[i].mapping && t.upload[i].before && t.upload[i].image);
			memset(blocks[i].mapping, 0xa5, sizes[i]);
			memset(t.upload[i].before, 0xa5, sizes[i]);
			memset(t.upload[i].image, 0x3c, sizes[i]);
		}
		writel(0, t.upload[0].image + 4);
		writel(1, regs + 0x898);
		accept = scenario != 2;
		connects = disconnects = 0;
		if (scenario == 0) {
			assert(!mt_trial_start(&t, &ops));
			assert(t.connected && t.pinned && refs == 1 && disconnects == 0);
			assert(mt_trial_run(&t, &ops) == -EBUSY && disconnects == 0);
			assert(mt_trial_restore(&t) == -EBUSY && refs == 1);
			assert(!mt_trial_disconnect(&t, &ops));
		} else if (scenario == 1) {
			assert(!mt_trial_run(&t, &ops));
			assert(t.connected && disconnects == 1);
		} else {
			assert(mt_trial_run(&t, &ops) == -ESHUTDOWN);
			assert(t.connect_result == -ETIMEDOUT && t.pinned && refs == 1);
			assert(connects == 4 && disconnects == 1);
			assert(mt_trial_restore(&t) == -EBUSY);
			/* Model a later genuine firmware transition, never force hardware. */
			writel(2, regs + 0x898);
			assert(!mt_trial_disconnect(&t, &ops));
		}
		assert(t.restored && t.disconnected && !t.pinned && refs == 0);
		for (i = 0; i < 6; i++) {
			assert(!memcmp(blocks[i].mapping, t.upload[i].before, sizes[i]));
			free(blocks[i].mapping);
		}
		mt_trial_fini(&t);
	}
	puts("PASS: startup event retention, retained successful start, bounded trial teardown, timeout ownership and byte-exact restoration");
	return 0;
}
