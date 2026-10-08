/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_FW_TRIAL_H
#define MT_GUEST_FW_TRIAL_H

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/sched.h>
#include "mt_fw_connection.h"
#include "mt_fw_queue_io.h"
#include "mt_boot_resources.h"

#define MT_TRIAL_BLOCKS 6
#define MT_TRIAL_BACKUP_BYTES (MT_FW_MAP_SIZE + MT_FW_TABLE_BYTES + PAGE_SIZE + \
	MT_MMU_DUMMY_BYTES + 2 * MT_STATIC_RESOURCE_BYTES)

struct mt_trial_upload {
	struct mt_vram_block *block;
	void *before, *image;
	bool written;
};

struct mt_fw_trial {
	struct mt_trial_upload upload[MT_TRIAL_BLOCKS];
	struct mt_fw_queue_io *queue;
	void __iomem *custom;
	u8 *scratch;
	bool pinned, published, connected, disconnected, restored, verified;
	int connect_result, disconnect_result, result;
	u32 online_count, connect_commands, disconnect_commands;
	u32 event_count;
	struct { u32 dm, words[6]; } events[128];
};

/* Poll the same events 14000be34 consumes in its interrupt path. In this
 * connect-only trial there are no render/job contexts: 14000e6a4/e7a4/e5c8
 * are no-ops for NULL context, and e784 is a no-op for Guest. Preserve every
 * record for diagnostics before acknowledging it; stop if storage is full.
 * This policy must be replaced before introducing any workload submission.
 */
static inline int mt_trial_drain_events(struct mt_fw_trial *t)
{
	u32 dm, cursor, head, tail, available;
	for (dm = 0; dm < MT_FW_DM_COUNT; dm++) {
		cursor = dm * MT_FW_DM_BYTES + MT_FW_CURSOR_OFFSET + 32;
		head = readl(t->queue->queue + cursor);
		tail = readl(t->queue->queue + cursor + 8);
		if (head >= 64 || tail >= 64)
			return -EIO;
		available = (head - tail) & 63;
		if (available > ARRAY_SIZE(t->events) - t->event_count)
			return -ENOSPC;
		if (!available)
			continue;
		while (tail != head) {
			t->events[t->event_count].dm = dm;
			memcpy_fromio(t->events[t->event_count].words,
				t->queue->queue + dm * MT_FW_DM_BYTES + MT_FW_EVENT_OFFSET + tail * MT_FW_EVENT_BYTES,
				MT_FW_EVENT_BYTES);
			t->event_count++;
			tail = (tail + 1) & 63;
		}
		mb();
		writel(tail, t->queue->queue + cursor + 8);
		mb();
	}
	return 0;
}

static inline u32 mt_trial_fw_state(void *opaque)
{
	struct mt_fw_trial *t = opaque;
	return readl(t->queue->registers + 0x898);
}
static inline u32 mt_trial_started(void *opaque)
{
	struct mt_fw_trial *t = opaque;
	return readl(t->upload[0].block->mapping + 4);
}
static inline void mt_trial_guest_state(void *opaque, u32 state)
{
	struct mt_fw_trial *t = opaque;
	writel(state, t->queue->registers + 0x890);
	mb();
}
static inline void mt_trial_online(void *opaque)
{
	struct mt_fw_trial *t = opaque;
	writeq(1, t->custom + 0x148);
	mb();
	t->online_count++;
}
static inline int mt_trial_send(void *opaque, u32 opcode)
{
	struct mt_fw_trial *t = opaque;
	u8 command[MT_FW_COMMAND_BYTES];
	int ret;
	mt_fw_kernel_command(command, opcode, task_tgid_nr(current), NULL);
	ret = mt_fw_queue_try_submit(t->queue, 0, 0, command);
	if (!ret) {
		if (opcode == MT_FW_CONNECT)
			t->connect_commands++;
		else if (opcode == MT_FW_DISCONNECT)
			t->disconnect_commands++;
	}
	return ret;
}
static inline int mt_trial_work_idle(void *opaque)
{
	struct mt_fw_trial *t = opaque;
	int ret = mt_trial_drain_events(t);
	if (ret)
		return ret;
	return mt_fw_queue_work_idle(&mt_fw_io_ops, t->queue, MT_FW_QUEUE_BYTES);
}
static inline int mt_trial_control_idle(void *opaque)
{
	struct mt_fw_trial *t = opaque;
	int ret = mt_trial_drain_events(t);
	if (ret)
		return ret;
	return mt_fw_queue_dm_idle(&mt_fw_io_ops, t->queue, MT_FW_QUEUE_BYTES, 0);
}

static inline bool mt_trial_equal(struct mt_fw_trial *t, struct mt_trial_upload *u, const void *data)
{
	u32 offset;
	for (offset = 0; offset < u->block->size; offset += PAGE_SIZE) {
		memcpy_fromio(t->scratch, u->block->mapping + offset, PAGE_SIZE);
		if (memcmp(t->scratch, (const u8 *)data + offset, PAGE_SIZE))
			return false;
	}
	return true;
}

static inline int mt_trial_restore(struct mt_fw_trial *t)
{
	int i;
	bool restored = true;
	/* A failed connection/timeout cannot be treated as cancellation. */
	if (t->published && !t->disconnected)
		return -EBUSY;
	for (i = MT_TRIAL_BLOCKS - 1; i >= 0; i--) {
		struct mt_trial_upload *u = &t->upload[i];
		if (!u->written)
			continue;
		memcpy_toio(u->block->mapping, u->before, u->block->size);
		mb();
		if (!mt_trial_equal(t, u, u->before))
			restored = false;
	}
	t->restored = restored;
	if (restored && t->pinned) {
		t->pinned = false;
		module_put(THIS_MODULE);
	}
	return restored ? 0 : -EIO;
}

static inline int mt_trial_disconnect(struct mt_fw_trial *t,
		const struct mt_fw_connection_ops *ops)
{
	if (!t->published || !t->pinned)
		return -EINVAL;
	t->disconnect_result = mt_fw_disconnect(ops, t);
	if (t->disconnect_result)
		return t->disconnect_result;
	t->disconnected = true;
	return mt_trial_restore(t);
}

static inline int mt_trial_prepare(struct mt_fw_trial *t, struct mt_fw_queue_io *queue,
		void __iomem *custom, struct mt_vram_block *primary,
		struct mt_boot_resources *boot, const void *firmware_image)
{
	u32 i;
	if (!boot->prepared || !firmware_image || t->scratch)
		return -EINVAL;
	t->queue = queue;
	t->custom = custom;
	t->connect_result = t->disconnect_result = -ENODATA;
	t->upload[0].block = &primary[0];
	t->upload[1].block = &primary[1];
	t->upload[2].block = &primary[2];
	t->upload[3].block = &boot->blocks[MT_BOOT_DUMMY];
	t->upload[4].block = &boot->blocks[MT_BOOT_YUV];
	t->upload[5].block = &boot->blocks[MT_BOOT_KILL];
	t->scratch = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!t->scratch)
		return -ENOMEM;
	for (i = 0; i < MT_TRIAL_BLOCKS; i++) {
		struct mt_trial_upload *u = &t->upload[i];
		u->before = kvmalloc(u->block->size, GFP_KERNEL);
		u->image = kvmalloc(u->block->size, GFP_KERNEL);
		if (!u->before || !u->image)
			return -ENOMEM;
		memcpy_fromio(u->before, u->block->mapping, u->block->size);
		memcpy(u->image, u->before, u->block->size);
	}
	memcpy(t->upload[0].image, firmware_image, MT_FW_MAP_SIZE);
	memcpy(t->upload[1].image, boot->stage, MT_FW_TABLE_BYTES);
	memset(t->upload[2].image, 0xba, PAGE_SIZE);
	memcpy(t->upload[3].image, (u8 *)boot->stage + MT_BOOT_DUMMY_OFFSET, MT_MMU_DUMMY_BYTES);
	/* The reference only writes these defined constants; preserve padding.
	 * PDS/USC and Host PB allocation do not themselves initialize memory.
	 */
	return mt_init_static_resources(t->upload[4].image, MT_STATIC_RESOURCE_BYTES,
		t->upload[5].image, MT_STATIC_RESOURCE_BYTES);
}

static inline int mt_trial_start(struct mt_fw_trial *t,
		const struct mt_fw_connection_ops *ops)
{
	u32 i;
	int ret;
	u32 reg890;
	if (!t->scratch || t->published || t->pinned)
		return -EBUSY;
	/* 0x890==2 at boot means firmware pre-initialized (cold boot does not
	 * clear it). The upload below is idempotent and ensures VRAM contents;
	 * connect then proceeds normally. See r372. */
	reg890 = readl(t->queue->registers + 0x890);
	if (reg890 != 0 && reg890 != 2)
		return -EBUSY;
	if (mt_trial_fw_state(t) != 1)
		return -EBUSY;
	/* Removal must not free live mappings after an unacknowledged transition.
	 * PCI manual bind/unbind is suppressed by the owning driver as well.
	 */
	__module_get(THIS_MODULE);
	t->pinned = true;
	for (i = 0; i < MT_TRIAL_BLOCKS; i++) {
		struct mt_trial_upload *u = &t->upload[i];
		u->written = true;
		memcpy_toio(u->block->mapping, u->image, u->block->size);
		mb();
		if (!mt_trial_equal(t, u, u->image)) {
			ret = mt_trial_restore(t);
			return ret ? ret : -EIO;
		}
	}
	t->verified = true;
	if (readl(t->queue->registers + 0x890) != reg890 || mt_trial_fw_state(t) != 1) {
		ret = mt_trial_restore(t);
		return ret ? ret : -EBUSY;
	}
	/* 1400159cc -> 1400224b0. This submits a device PA, never a CPU GPA.
	 * Guest does not execute the Native root-register/core-boot path.
	 */
	writeq(t->upload[0].block->gpu_pa, t->custom + 0x30);
	writeq(MT_FW_MAP_SIZE, t->custom + 0x38);
	mb();
	t->published = true;
	t->connect_result = mt_fw_connect(ops, t);
	t->connected = !t->connect_result;
	return t->connect_result;
}

/* The original bounded trial remains connect -> disconnect -> restore.
 * Runtime users may keep a successful mt_trial_start session self-pinned. */
static inline int mt_trial_run(struct mt_fw_trial *t,
		const struct mt_fw_connection_ops *ops)
{
	int start, ret;
	if (t->published || t->pinned)
		return -EBUSY;
	start = mt_trial_start(t, ops);
	if (!t->published || !t->pinned)
		return start;
	/* Even a connect failure can have a queued command. Attempt the actual
	 * disconnect protocol, and retain the entire session if it does not ack.
	 */
	ret = mt_trial_disconnect(t, ops);
	return ret ? ret : start;
}

static inline void mt_trial_fini(struct mt_fw_trial *t)
{
	u32 i;
	if (WARN_ON(t->pinned))
		return;
	for (i = 0; i < MT_TRIAL_BLOCKS; i++) {
		kvfree(t->upload[i].before);
		kvfree(t->upload[i].image);
	}
	kfree(t->scratch);
	memset(t, 0, sizeof(*t));
}

#endif
