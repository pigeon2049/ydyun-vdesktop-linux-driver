/* Fabricated gate for the check-only translator envelope (r113/r147).
 * A synthetic kick stamps only the envelope tag; every other byte must equal
 * the verified Linux packet template. RAM only: no hardware, no modules.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

#ifndef __maybe_unused
#define __maybe_unused __attribute__((unused))
#endif

#include "../../kernel/mt_gfx_packet_template.h"
#include "../../kernel/mt_translate_kick.h"

static u8 out[MT_GFX_LINUX_PACKET_BYTES];
static u8 out2[MT_GFX_LINUX_PACKET_BYTES];

int main(void)
{
	u32 i;
	u8 tag_le[8];
	u64 tag = 0x0102030405060708ULL;

	assert(sizeof(mt_gfx_linux_packet_template) == MT_GFX_LINUX_PACKET_BYTES);
	assert(MT_TRANSLATE_TAG_OFFSET == 0x08U);

	mt_translate_frame_emit(out, mt_gfx_linux_packet_template,
				MT_GFX_LINUX_PACKET_BYTES, tag);
	/* Length preserved. */
	/* Tag slot holds the little-endian sequence. */
	for (i = 0; i < 8; i++)
		tag_le[i] = (u8)(tag >> (8 * i));
	assert(memcmp(out + MT_TRANSLATE_TAG_OFFSET, tag_le, 8) == 0);
	/* Everything else is template-identical. */
	assert(memcmp(out, mt_gfx_linux_packet_template,
		      MT_TRANSLATE_TAG_OFFSET) == 0);
	assert(memcmp(out + MT_TRANSLATE_TAG_OFFSET + 8,
		      mt_gfx_linux_packet_template + MT_TRANSLATE_TAG_OFFSET + 8,
		      MT_GFX_LINUX_PACKET_BYTES - MT_TRANSLATE_TAG_OFFSET - 8) == 0);
	/* Two kicks differ in exactly the tag slot. */
	mt_translate_frame_emit(out2, mt_gfx_linux_packet_template,
				MT_GFX_LINUX_PACKET_BYTES, ~tag);
	for (i = 0; i < MT_GFX_LINUX_PACKET_BYTES; i++) {
		bool in_tag = i >= MT_TRANSLATE_TAG_OFFSET &&
			      i < MT_TRANSLATE_TAG_OFFSET + 8;
		assert((out[i] != out2[i]) == in_tag);
	}
	/* RT patch: exactly the four firmware-required slots change, to the
	 * documented values; everything else stays template-identical. */
	{
		u8 patched[MT_GFX_LINUX_PACKET_BYTES];
		u64 va = MT_TRANSLATE_RT_VA, stride, extent, direct;
		memcpy(patched, mt_gfx_linux_packet_template,
		       MT_GFX_LINUX_PACKET_BYTES);
		mt_translate_rt_patch(patched, va);
		memcpy(&stride, patched + MT_TRANSLATE_RT_OFF_STRIDE, 8);
		memcpy(&extent, patched + MT_TRANSLATE_RT_OFF_EXTENT, 8);
		memcpy(&va, patched + MT_TRANSLATE_RT_OFF_VA, 8);
		memcpy(&direct, patched + MT_TRANSLATE_RT_OFF_DIRECT, 8);
		assert(va == MT_TRANSLATE_RT_VA && direct == MT_TRANSLATE_RT_VA);
		assert(stride == MT_TRANSLATE_RT_STRIDE &&
		       extent == MT_TRANSLATE_RT_EXTENT);
		for (i = 0; i < MT_GFX_LINUX_PACKET_BYTES; i++) {
			bool in_rt = (i >= MT_TRANSLATE_RT_OFF_VA &&
				      i < MT_TRANSLATE_RT_OFF_VA + 8) ||
				     (i >= MT_TRANSLATE_RT_OFF_STRIDE &&
				      i < MT_TRANSLATE_RT_OFF_STRIDE + 8) ||
				     (i >= MT_TRANSLATE_RT_OFF_EXTENT &&
				      i < MT_TRANSLATE_RT_OFF_EXTENT + 8) ||
				     (i >= MT_TRANSLATE_RT_OFF_DIRECT &&
				      i < MT_TRANSLATE_RT_OFF_DIRECT + 8);
			if (!in_rt)
				assert(patched[i] ==
				       mt_gfx_linux_packet_template[i]);
		}
	}
	puts("PASS: translator envelope is template-identical outside the tag slot");
	return 0;
}
