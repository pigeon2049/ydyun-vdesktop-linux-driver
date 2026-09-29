/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_CE_STREAM_H
#define MT_GUEST_CE_STREAM_H
#include "mt_ce_copy.h"

#define MT_CE3_STREAM_BYTES 176U
#define MT_CE_RECORD_BYTES 256U
struct mt_ce3_stream_input {
	struct mt_ce_copy_input copy;
	u64 command_va;
};
struct mt_ce3_stream_image {
	u8 commands[MT_CE3_STREAM_BYTES];
	/* Software library record from 1400e3474, NOT a firmware queue packet. */
	u8 record[MT_CE_RECORD_BYTES];
};
/* Restricted CE3 sequence, matching reference initialization + one copy +
 * explicit barrier + finalization with no optional prefix, external sync,
 * or saved state. Both buffers remain CPU staging. The live device profile
 * and conversion from the library record to the submitted DMA buffer are
 * still unverified: do not send this image to firmware as a finished job. */
static inline int mt_ce3_stream_prepare(void *out, u32 capacity,
		const struct mt_gpu_vm *vm, const struct mt_bo *command,
		const struct mt_bo *src, const struct mt_bo *dst,
		const struct mt_ce3_stream_input *in)
{
	struct mt_ce3_stream_image next = {0};
	u64 cmd_pa, src_pa, dst_pa;
	int ret;
	if (!out || capacity < sizeof(next) || !in || in->copy.version != 3 ||
	    (in->command_va & 31) ||
	    (in->command_va & 4095) > 4096 - MT_CE3_STREAM_BYTES)
		return -EINVAL;
	ret = mt_ce_copy_prepare(next.commands + 56, MT_CE_COPY_BYTES,
		vm, src, dst, &in->copy);
	if (ret)
		return ret;
	ret = mt_ce_copy_resolve(vm, command, in->command_va, MT_CE3_STREAM_BYTES, &cmd_pa);
	if (ret)
		return ret;
	ret = mt_ce_copy_resolve(vm, src, in->copy.src, in->copy.bytes, &src_pa);
	if (ret)
		return ret;
	ret = mt_ce_copy_resolve(vm, dst, in->copy.dst, in->copy.bytes, &dst_pa);
	if (ret)
		return ret;
	/* Command bytes cannot alias either data interval, even at different VAs. */
	if ((cmd_pa < src_pa + in->copy.bytes && src_pa < cmd_pa + MT_CE3_STREAM_BYTES) ||
	    (cmd_pa < dst_pa + in->copy.bytes && dst_pa < cmd_pa + MT_CE3_STREAM_BYTES))
		return -EINVAL;
	/* 1400e4d04: reserve 56 bytes and jump past initial state storage. */
	mt_fw_put64(next.commands, 0, 0x4000000000000000ULL | (in->command_va + 56));
	/* 1400e9b88: backpatch copy + barrier + end byte count (40+24+16). */
	mt_fw_put64(next.commands, 8, 80);
	mt_fw_put64(next.commands, 16, 0x5000000000000001ULL);
	/* 1400e4b7c / 1400e5360: pending-copy synchronization, no address write. */
	mt_fw_put64(next.commands, 96, 0x1000000010000008ULL);
	/* 1400e9420: 16-byte end followed by 32-byte-aligned state storage. */
	mt_fw_put64(next.commands, 120, 0x5000000000000000ULL);
	mt_fw_put64(next.commands, 160, 24);
	mt_fw_put64(next.record, 0, 32);
	mt_fw_put64(next.record, 8, in->command_va + 160);
	mt_fw_put64(next.record, 16, in->command_va);
	mt_fw_put64(next.record, 40, 24);
	memcpy(out, &next, sizeof(next));
	return 0;
}
#endif
