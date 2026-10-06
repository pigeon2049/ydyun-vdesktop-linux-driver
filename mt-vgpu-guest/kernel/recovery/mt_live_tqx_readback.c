// SPDX-License-Identifier: GPL-2.0
/* Read-only diagnosis of the retained one-shot DMA-source TQX destination.
 * Reuses the main driver's existing BAR mapping; it requests no regions,
 * writes no MMIO, and refuses to read while a job is pending.
 */
#include "../mt_guest_device.h"
#include "../mt_addr_plan.h"

#define TEST_TARGET_BAR_OFFSET 0x122e000ULL
#define TEST_BYTES 4096U

static bool enable;
module_param(enable, bool, 0400);
static int result = -ENODATA;
module_param(result, int, 0444);
static bool verified;
module_param(verified, bool, 0444);
static u32 mismatch_offset = U32_MAX;
module_param(mismatch_offset, uint, 0444);
static u32 actual_word, expected_word;
module_param(actual_word, uint, 0444);
module_param(expected_word, uint, 0444);
static unsigned long long root_pa, expected_gpu_pa;
module_param(root_pa, ullong, 0400);
module_param(expected_gpu_pa, ullong, 0400);
static unsigned long long source_pte, source_leaf_pa;
module_param(source_pte, ullong, 0444);
module_param(source_leaf_pa, ullong, 0444);
static bool root_present, directory_present, leaf_present;
module_param(root_present, bool, 0444);
module_param(directory_present, bool, 0444);
module_param(leaf_present, bool, 0444);
static bool source_pte_matches;
module_param(source_pte_matches, bool, 0444);

static int __init readback_init(void)
{
	struct pci_dev *pdev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(14, 0));
	struct module *owner = NULL;
	struct mt_guest *g;
	struct mt_guest_device *d;
	struct mt_vram_block *block;
	u8 *bytes = NULL;
	u8 *tables = NULL;
	u32 root_entry, directory_offset;
	u64 directory_entry;
	u32 i;
	int ret = -ENODEV;
	(void)mt_fw_event_io_ops;

	if (!enable)
		return -EPERM;
	if (!pdev)
		return ret;
	device_lock(&pdev->dev);
	if (!pdev->driver || strcmp(pdev->driver->name, "mt_guest_probe"))
		goto unlock_device;
	owner = pdev->driver->driver.owner;
	if (!owner || !try_module_get(owner)) {
		owner = NULL;
		goto unlock_device;
	}
	g = pci_get_drvdata(pdev);
	if (!g)
		goto put_owner;
	d = container_of(g, struct mt_guest_device, state);
	mutex_lock(&g->trial_lock);
	ret = -EBUSY;
	if (!g->trial.connected || !g->trial.pinned || !d->runtime.published ||
	    d->runtime.event_result || d->markers.total || d->markers.ready ||
	    d->markers.work_ready || !g->vram.region_owned || g->vram.pdev != pdev)
		goto unlock_session;
	ret = -EINVAL;
	if (!IS_ALIGNED(root_pa, PAGE_SIZE) ||
	    !IS_ALIGNED(expected_gpu_pa, PAGE_SIZE) ||
	    root_pa >= (1ULL << MT_GPU_VA_BITS) ||
	    expected_gpu_pa >= (1ULL << MT_GPU_VA_BITS))
		goto unlock_session;
	ret = -ENOENT;
	list_for_each_entry(block, &g->vram.blocks, link)
		if (block->bar_offset == TEST_TARGET_BAR_OFFSET &&
		    block->size == TEST_BYTES && block->mapping)
			break;
	if (&block->link == &g->vram.blocks)
		goto unlock_session;
	bytes = kmalloc(TEST_BYTES, GFP_KERNEL);
	if (!bytes) {
		ret = -ENOMEM;
		goto unlock_session;
	}
	memcpy_fromio(bytes, block->mapping, TEST_BYTES);
	memcpy(&actual_word, bytes, sizeof(actual_word));
	expected_word = 0;
	for (i = 0; i < sizeof(expected_word); i++)
		expected_word |= (u32)(u8)((i * 73 + 19) ^ (i >> 3)) << (i * 8);
	/* The TQX test uses the same deterministic source and 0xa5 destination
	 * guard as mt_live_tqx.c. Only the first 256 bytes should change.
	 */
	for (i = 0; i < TEST_BYTES; i++) {
		u8 expected = i < 256 ? (u8)((i * 73 + 19) ^ (i >> 3)) : 0xa5;

		if (bytes[i] != expected) {
			mismatch_offset = i;
			break;
		}
	}
	verified = mismatch_offset == U32_MAX;
	ret = verified ? 0 : -EILSEQ;
	/* Independently inspect the retained CPU copy of the private page-table
	 * BO. Match it by the root physical address recorded at prepare time.
	 */
	list_for_each_entry(block, &g->vram.blocks, link)
		if (block->gpu_pa == root_pa && block->mapping &&
		    block->size >= 15 * PAGE_SIZE)
			break;
	if (&block->link != &g->vram.blocks) {
		tables = kmalloc(block->size, GFP_KERNEL);
		if (tables) {
			memcpy_fromio(tables, block->mapping, block->size);
			memcpy(&root_entry, tables +
			       ((MT_TQX_STREAM_SRC_VA / MT_TQX_CMD_VA) % 1024) * 4,
			       sizeof(root_entry));
			root_present = !!(root_entry & 1);
			if (root_present) {
				directory_offset =
					((u64)(root_entry & 0xfffffff0U) << 8) - root_pa;
				if (directory_offset <= block->size - PAGE_SIZE) {
					memcpy(&directory_entry, tables + directory_offset +
					       ((0x40100000ULL / 0x200000ULL) % 512) * 8,
					       sizeof(directory_entry));
					directory_present = !!(directory_entry & 1);
					if (directory_present) {
						u64 leaf_offset =
							(directory_entry & 0xfffffff000ULL) - root_pa;

						if (leaf_offset <= block->size - PAGE_SIZE) {
							memcpy(&source_pte, tables + leaf_offset +
							       ((0x40100000ULL / PAGE_SIZE) % 512) * 8,
							       sizeof(source_pte));
							leaf_present = !!(source_pte & 1);
							if (leaf_present)
								source_leaf_pa = source_pte &
									0xfffffff000ULL;
							source_pte_matches = leaf_present &&
								source_leaf_pa == expected_gpu_pa;
						}
				}
			}
		}
	}
	}
unlock_session:
	kfree(tables);
	kfree(bytes);
	mutex_unlock(&g->trial_lock);
put_owner:
	module_put(owner);
unlock_device:
	device_unlock(&pdev->dev);
	pci_dev_put(pdev);
	result = ret;
	pr_info("mt_live_tqx_readback: offset=%#llx result=%d verified=%u mismatch=%u actual_word=%#x expected_word=%#x root=%#llx root_present=%u directory_present=%u leaf_present=%u source_pte=%#llx source_leaf=%#llx expected_gpu_pa=%#llx pte_matches=%u read_only=1\n",
		TEST_TARGET_BAR_OFFSET, result, verified, mismatch_offset,
		actual_word, expected_word, root_pa, root_present,
		directory_present, leaf_present, source_pte, source_leaf_pa,
		expected_gpu_pa, source_pte_matches);
	return 0;
}

static void __exit readback_exit(void) { }
module_init(readback_init);
module_exit(readback_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only inspection of the retained TQX DMA-source destination");
