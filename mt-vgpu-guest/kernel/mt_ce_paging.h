/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_CE_PAGING_H
#define MT_GUEST_CE_PAGING_H
#include "mt_ce_stream.h"

#define MT_CE_EXPORT_BYTES 48U
#define MT_CE_PAGING_BYTES 0x150U
/* The reference advances by 32 or 128 bytes per job. Reserve the larger
 * slot until the device-specific stride is known; only 16 bytes are used. */
#define MT_CE_PAGING_STATE_BYTES 128U

/* 1400e2690, restricted to exactly one 256-byte software record. This
 * export is an intermediate structure, not a firmware queue command. */
static inline int mt_ce_record_export_one(void *out, u32 capacity,
		const void *record, u32 record_bytes)
{
	u8 next[MT_CE_EXPORT_BYTES] = {0};
	const u8 *r = record;
	if (!out || capacity < sizeof(next) || !r || record_bytes != MT_CE_RECORD_BYTES)
		return -EINVAL;
	mt_fw_put64(next, 0, 1);
	memcpy(next + 8, r, 8);
	memcpy(next + 16, r + 16, 8);
	memcpy(next + 24, r + 8, 8);
	memcpy(next + 32, r + 32, 8);
	memcpy(next + 40, r + 40, 8);
	memcpy(out, next, sizeof(next));
	return 0;
}

struct mt_ce_paging_image {
	u8 descriptor[MT_CE_PAGING_BYTES];
	u8 state[MT_CE_PAGING_STATE_BYTES];
};

/* Zero-initialized single-job path: 14002171c followed by 140019f10.
 * This reconstructs the Windows paging descriptor, including its outer
 * header. It does not establish firmware acceptance or complete unrelated
 * submission metadata. No pointer in this image is dereferenced here. */
static inline int mt_ce_paging_encode(void *out, u32 capacity,
		const void *record, u32 record_bytes, u64 state_va)
{
	struct mt_ce_paging_image next = {0};
	u8 exported[MT_CE_EXPORT_BYTES];
	int ret;
	if (!out || capacity < sizeof(next))
		return -EINVAL;
	ret = mt_ce_record_export_one(exported, sizeof(exported), record, record_bytes);
	if (ret)
		return ret;
	/* 14002171c: four fields; export +32 is not copied by this adapter. */
	memcpy(next.descriptor + 0x128, exported + 8, 8);
	memcpy(next.descriptor + 0x138, exported + 16, 8);
	memcpy(next.descriptor + 0x130, exported + 24, 8);
	memcpy(next.descriptor + 0x70, exported + 40, 8);
	/* 140019f10: relocate state to a separate slot, then normalize header. */
	memcpy(next.state, next.descriptor + 0x70, 8);
	mt_fw_put64(next.descriptor, 0x130, state_va);
	mt_fw_put32(next.descriptor, 0x1c, 0x69);
	mt_fw_put32(next.descriptor, 0x28, 0x48);
	mt_fw_put32(next.descriptor, 0x2c, 1);
	mt_fw_put32(next.descriptor, 0x48, 4);
	mt_fw_put32(next.descriptor, 0x50, 0x60);
	mt_fw_put32(next.descriptor, 0x58, 0xe8);
	mt_fw_put32(next.descriptor, 0x64, 0x08000000);
	mt_fw_put32(next.descriptor, 0x68, 0x18);
	mt_fw_put32(next.descriptor, 0x6c, 1);
	memcpy(out, &next, sizeof(next));
	return 0;
}

struct mt_ce3_paging_input {
	struct mt_ce3_stream_input stream;
	u64 descriptor_va, state_va;
};
struct mt_ce3_paging_image {
	struct mt_ce3_stream_image stream;
	struct mt_ce_paging_image paging;
};

/* All three command components occupy verified intervals of command BO.
 * Serialize with BO/VM mutation. CPU staging only, with transactional output;
 * no upload, resource pin, queue publication, or inferred device profile. */
static inline int mt_ce3_paging_prepare(void *out, u32 capacity,
		const struct mt_gpu_vm *vm, const struct mt_bo *command,
		const struct mt_bo *src, const struct mt_bo *dst,
		const struct mt_ce3_paging_input *in)
{
	struct mt_ce3_paging_image next;
	u64 physical[5], va[5];
	u32 bytes[5], i, j;
	int ret;
	if (!out || capacity < sizeof(next) || !in ||
	    (in->descriptor_va & 31) || (in->state_va & 31))
		return -EINVAL;
	ret = mt_ce3_stream_prepare(&next.stream, sizeof(next.stream), vm, command, src, dst, &in->stream);
	if (ret)
		return ret;
	va[0] = in->stream.command_va; bytes[0] = MT_CE3_STREAM_BYTES;
	va[1] = in->descriptor_va; bytes[1] = MT_CE_PAGING_BYTES;
	va[2] = in->state_va; bytes[2] = MT_CE_PAGING_STATE_BYTES;
	va[3] = in->stream.copy.src; bytes[3] = in->stream.copy.bytes;
	va[4] = in->stream.copy.dst; bytes[4] = in->stream.copy.bytes;
	for (i = 0; i < 5; i++) {
		ret = mt_ce_copy_resolve(vm, i < 3 ? command : i == 3 ? src : dst,
			va[i], bytes[i], &physical[i]);
		if (ret)
			return ret;
		for (j = 0; j < i; j++)
			if (physical[i] < physical[j] + bytes[j] && physical[j] < physical[i] + bytes[i])
				return -EINVAL;
	}
	ret = mt_ce_paging_encode(&next.paging, sizeof(next.paging), next.stream.record,
		MT_CE_RECORD_BYTES, in->state_va);
	if (ret)
		return ret;
	memcpy(out, &next, sizeof(next));
	return 0;
}
#endif
