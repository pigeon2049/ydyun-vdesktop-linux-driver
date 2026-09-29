/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_STATE_H
#define MT_GUEST_STATE_H
#include <linux/mutex.h>
#include "mt_mmu.h"
#include "mt_fw_state.h"
#include "mt_guest_heaps.h"
#include "mt_vram.h"
#include "mt_fw_queue_io.h"
#include "mt_boot_resources.h"
#include "mt_fw_image.h"
#include "mt_fw_connection.h"
#include "mt_fw_trial.h"


/* Shared with the temporary live-session recovery helper. Keep layout stable
 * while the first trial module remains loaded. No new members were added. */
struct mt_guest {
	void __iomem *regs;
	void __iomem *custom;
	unsigned long info;
	bool queried;
	unsigned long channel[4];
	u8 registered;
	bool mode_started;
	int rpc_result;
	u64 rpc_value;
	int version_result;
	u64 host_version;
	int memory_result;
	u64 firmware_bar_offset, shared_bar_offset;
	void *memory_snapshot;
	struct mt_vram vram;
	struct mt_vram_block vram_blocks[3];
	struct mt_fw_queue_io firmware_queue;
	int queue_idle[MT_FW_DM_COUNT];
	struct mt_boot_resources boot;
	struct mt_fw_image firmware_image;
	void *firmware_backup;
	bool firmware_written, firmware_verified, firmware_restored;
	int firmware_upload_result;
	struct mt_fw_trial trial;
	struct mutex trial_lock;
	int vram_result;
	bool vram_write_performed, vram_write_verified, vram_restore_verified;
	u8 vram_samples[2 * PAGE_SIZE];
	u8 channel_snapshot[4 * PAGE_SIZE];
};
#endif
