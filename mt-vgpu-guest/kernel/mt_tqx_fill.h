/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_TQX_FILL_H
#define MT_GUEST_TQX_FILL_H
#include "mt_tqx_stream.h"

#define MT_TQX_FILL_BYTES 76U
struct mt_tqx_fill_input {
	u64 destination_va, command_va;
	u32 element_bytes, width, height, x, y, rect_width, rect_height;
	/* Packed destination pixel, not float RGBA. */
	u32 color[4];
	u32 shader_heap_base, pds_code_heap_base, pds_initial_state;
};
struct mt_tqx_fill_image {
	u8 command[MT_TQX_FILL_BYTES], pds_initial[16];
	u8 record[MT_TQX_RECORD_BYTES], page_record[16];
};

/* Original 14010ae38 -> 14011e4c4 -> vtable+568 (1400dc430).
 * A native inline-color rectangle is 19 DWORDs, followed by no source texture
 * or per-pixel CPU upload. 14011d194 changes the last control word to 0x2d.
 * The destination is a single tightly packed linear surface; clipping is
 * explicit and entirely inside that surface. Ownership is checked separately.
 */
static inline int mt_tqx_fill_build(void *out, u32 capacity,
		const struct mt_tqx_fill_input *in)
{
	struct mt_tqx_fill_image next = {0};
	struct mt_tqx_stream_input finish = {0};
	struct mt_tqx_destination_input dest = {0};
	u8 base[MT_TQX_DESTINATION_BYTES];
	u32 right, bottom, i;
	int ret;
	if (!out || !in || capacity < sizeof(next) || !in->rect_width ||
	    !in->rect_height || in->x >= in->width || in->y >= in->height ||
	    in->rect_width > in->width - in->x ||
	    in->rect_height > in->height - in->y)
		return -EINVAL;
	dest = (struct mt_tqx_destination_input){.destination_va = in->destination_va,
		.element_bytes = in->element_bytes, .width = in->width, .height = in->height};
	ret = mt_tqx_destination_build(base, sizeof(base), &dest);
	if (ret)
		return ret;
	memcpy(next.command, base, 52);
	right = in->x + in->rect_width - 1;
	bottom = in->y + in->rect_height - 1;
	mt_fw_put32(next.command, 12, bottom | (in->y << 16));
	mt_fw_put32(next.command, 16, right | (in->x << 16));
	mt_fw_put32(next.command, 40, 0xb8000000U);
	mt_fw_put32(next.command, 44, right | (in->x << 16));
	mt_fw_put32(next.command, 48, bottom | (in->y << 16));
	for (i = 0; i < 4; i++)
		mt_fw_put32(next.command, 52 + i * 4, in->color[i]);
	mt_fw_put32(next.command, 68, 0x2d);
	finish.command_va = in->command_va;
	finish.pds_initial_state = in->pds_initial_state;
	finish.job.shader_heap_base = in->shader_heap_base;
	finish.job.pds_code_heap_base = in->pds_code_heap_base;
	ret = mt_tqx_stream_finish(next.pds_initial, next.record, next.page_record,
		&finish, MT_TQX_FILL_BYTES);
	if (ret)
		return ret;
	memcpy(out, &next, sizeof(next));
	return 0;
}
#endif
