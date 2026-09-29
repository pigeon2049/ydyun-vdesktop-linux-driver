/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_FW_IMAGE_H
#define MT_GUEST_FW_IMAGE_H

#include <crypto/sha2.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include "mt_fw_state.h"
#include "mt_fw_queue.h"
#include "mt_guest_heaps.h"

#define MT_FW_LOADER_NAME "mt-vgpu-guest/gen1-guest-loader.bin"

struct mt_fw_image {
	void *data;
	u8 sha256[SHA256_DIGEST_SIZE];
};

static inline void mt_fw_image_fini(struct mt_fw_image *image)
{
	kvfree(image->data);
	memset(image, 0, sizeof(*image));
}

/* Accept the exact independently verified loader-stage image, then overlay
 * this supported Guest layout. Never load an arbitrary ELF or raw .sys.
 */
static inline int mt_fw_image_load(struct device *dev, struct mt_fw_image *image,
				  const void *raw_info, u32 info_size)
{
	static const u8 expected[SHA256_DIGEST_SIZE] = {
		0x35,0xd4,0x0f,0x75,0x09,0x9a,0xaf,0x7f,0xf7,0x40,0x40,0x73,0x6e,0xe7,0xee,0x34,
		0xe0,0x9e,0x26,0xf3,0x55,0x6b,0x7e,0xa9,0x04,0x0f,0xa1,0x43,0xba,0xd9,0xce,0xe9
	};
	const struct firmware *firmware;
	struct mt_guest_heap_plan *plan;
	struct mt_fw_guest_resources resources = {0};
	u8 digest[SHA256_DIGEST_SIZE];
	u64 pb_va, flags;
	u32 pb_size, magic, version;
	int ret;
	if (!raw_info || info_size < 0xcc8 || image->data)
		return -EINVAL;
	memcpy(&magic, raw_info, 4);
	memcpy(&version, (const u8 *)raw_info + 4, 4);
	memcpy(&flags, (const u8 *)raw_info + 0x10, 8);
	memcpy(&pb_va, (const u8 *)raw_info + 0xc88, 8);
	memcpy(&pb_size, (const u8 *)raw_info + 0xc98, 4);
	if (magic != 0xaa557491 || version != 2 || !(flags & 0x10))
		return -EPROTO;
	plan = kzalloc(sizeof(*plan), GFP_KERNEL);
	if (!plan)
		return -ENOMEM;
	mt_guest_plan_heaps(plan);
	if (pb_va != plan->resources[0].va || pb_size != plan->resources[0].size) {
		ret = -EPROTO;
		goto free_plan;
	}
	ret = request_firmware(&firmware, MT_FW_LOADER_NAME, dev);
	if (ret)
		goto free_plan;
	if (firmware->size != MT_FW_MAP_SIZE) {
		ret = -EINVAL;
		goto release;
	}
	sha256(firmware->data, firmware->size, digest);
	if (memcmp(digest, expected, sizeof(expected))) {
		ret = -EBADMSG;
		goto release;
	}
	image->data = kvmalloc(MT_FW_MAP_SIZE, GFP_KERNEL);
	if (!image->data) {
		ret = -ENOMEM;
		goto release;
	}
	memcpy(image->data, firmware->data, MT_FW_MAP_SIZE);
	resources.pb_va[0] = pb_va;
	resources.pb_va[1] = pb_va + 0x100;
	/* 14002de98 clears feature word +0x18 bit 0. In 14000de94 this
	 * disables the separate 8-byte system-memory notification allocation.
	 * It is NOT the 4 KiB fence resource reserved in the GPU heap.
	 */
	resources.fence_gpu_pa = 0;
	resources.yuv_va = plan->resources[10].va;
	resources.heap1_va = plan->heaps[1].base;
	resources.heap2_va = plan->heaps[2].base;
	resources.heap7_va = plan->heaps[7].base;
	resources.heap8_va = plan->heaps[8].base;
	resources.heap10_va = plan->heaps[10].base;
	resources.heap9_va = plan->heaps[9].base;
	resources.dm_kill_offset = plan->resources[11].va - plan->heaps[2].base;
	ret = mt_fw_apply_guest_state(image->data, MT_FW_MAP_SIZE, &resources);
	if (ret)
		goto release;
	ret = mt_fw_queue_init_image((u8 *)image->data + MT_FW_STATE_BYTES, MT_FW_QUEUE_BYTES);
	if (!ret)
		sha256(image->data, MT_FW_MAP_SIZE, image->sha256);
release:
	release_firmware(firmware);
	if (ret)
		mt_fw_image_fini(image);
free_plan:
	kfree(plan);
	return ret;
}

#endif
