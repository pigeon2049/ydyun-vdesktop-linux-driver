/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <string.h>
#include "../../kernel/mt_ce_copy.h"

#define CHECK(expr) do { \
	if (!(expr)) { \
		fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
			__FILE__, __LINE__, #expr); \
		return 1; \
	} \
} while (0)

int main(void)
{
	struct mt_bo_ops ops = {0};
	int store;
	u64 scatter[2] = {0x100000, 0x300000};
	u64 contiguous[2] = {0x100000, 0x101000};
	struct mt_bo tables = {
		.backing.bytes = 4096,
		.ops = &ops,
		.store = &store,
		.refs = 1,
	};
	struct mt_bo source = {
		.backing = {.gpu_pa = scatter[0], .bytes = 8192},
		.ops = &ops,
		.store = &store,
		.refs = 1,
		.page_pa = scatter,
	};
	struct mt_vm_binding binding = {
		.bo = &source,
		.va = 0x40000000,
		.bytes = 8192,
		.flags = MT_GPU_MAP_DEFAULT,
	};
	struct mt_gpu_vm vm = {
		.tables = &tables,
		.bindings = &binding,
		.count = 1,
	};
	u64 physical = 0;
	int ret;

	/* A short range contained in one DMA page has a linear device address. */
	ret = mt_ce_copy_resolve(&vm, &source, binding.va + 0x100, 256, &physical);
	CHECK(!ret && physical == 0x100100);

	/* A span crossing pages is accepted only when the IOVA pages are
	 * physically consecutive, preserving the resolver's linear-range contract.
	 */
	source.page_pa = contiguous;
	ret = mt_ce_copy_resolve(&vm, &source, binding.va + 4096 - 128, 256,
				 &physical);
	CHECK(!ret && physical == 0x100f80);
	source.page_pa = scatter;
	ret = mt_ce_copy_resolve(&vm, &source, binding.va + 4096 - 128, 256,
				 &physical);
	CHECK(ret == -EOPNOTSUPP);

	/* Reject malformed page alignment and preserve permission checks. */
	contiguous[0] = 0x100001;
	source.page_pa = contiguous;
	ret = mt_ce_copy_resolve(&vm, &source, binding.va, 64, &physical);
	CHECK(ret == -ERANGE);
	contiguous[0] = 0x100000;
	binding.flags = MT_GPU_MAP_READ_ONLY;
	ret = mt_ce_copy_resolve_access(&vm, &source, binding.va, 64, true,
					&physical);
	CHECK(ret == -EACCES);

	/* Existing contiguous BO behavior is unchanged. */
	source.page_pa = NULL;
	source.backing.gpu_pa = 0x200000;
	binding.flags = MT_GPU_MAP_DEFAULT;
	ret = mt_ce_copy_resolve(&vm, &source, binding.va + 0x80, 64, &physical);
	CHECK(!ret && physical == 0x200080);

	puts("PASS: DMA page-list resolution, linear-span guard, permissions and contiguous BO compatibility");
	return 0;
}
