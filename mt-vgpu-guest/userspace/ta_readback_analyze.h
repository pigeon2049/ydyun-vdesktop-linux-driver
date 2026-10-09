/* SPDX-License-Identifier: GPL-2.0 */
/* r417: Pixel content analysis for the T2 readback tool.
 *
 * Extracted from mt-ta-readback.c so the counting logic is unit-testable
 * (tests/c/pvr_bridge_core_test.c). Behavior is locked to the r416 tool:
 * nonzero counts pixels with any nonzero RGB channel; black (0,0,0)
 * counts as a distinct color but not as nonzero; at most
 * TA_READBACK_MAX_DISTINCT distinct colors are tracked; the alpha
 * channel is ignored.
 */
#ifndef TA_READBACK_ANALYZE_H
#define TA_READBACK_ANALYZE_H

#include <stdint.h>

#define TA_READBACK_MAX_DISTINCT 16U

struct ta_readback_stats {
	unsigned int nonzero;
	unsigned int n_distinct;
	uint32_t distinct[TA_READBACK_MAX_DISTINCT];
};

/* Analyze npixels RGBA8 pixels (alpha ignored). st is always
 * zero-initialized first; npixels==0 is a no-op (NULL-safe). */
static inline void ta_readback_analyze(const uint8_t *pixels,
				       unsigned int npixels,
				       struct ta_readback_stats *st)
{
	unsigned int i, j;

	st->nonzero = 0;
	st->n_distinct = 0;
	for (i = 0; i < npixels; i++) {
		uint32_t px = ((uint32_t)pixels[i * 4 + 0] << 16) |
			      ((uint32_t)pixels[i * 4 + 1] << 8) |
			      (uint32_t)pixels[i * 4 + 2];
		int seen = 0;

		if (px)
			st->nonzero++;
		for (j = 0; j < st->n_distinct; j++) {
			if (st->distinct[j] == px) {
				seen = 1;
				break;
			}
		}
		if (!seen && st->n_distinct < TA_READBACK_MAX_DISTINCT)
			st->distinct[st->n_distinct++] = px;
	}
}

#endif /* TA_READBACK_ANALYZE_H */
