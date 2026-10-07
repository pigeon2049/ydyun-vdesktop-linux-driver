// SPDX-License-Identifier: GPL-2.0
/* Read-only r23 post-fault evidence from six allocations already owned by
 * the bound driver. No new MMIO mapping, register access or device writes.
 * A snapshot is observational: it does not declare the GPU quiescent. */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include "../mt_guest_device.h"
#include "../mt_guest_state.h"

static bool enable;
module_param(enable, bool, 0400);
static bool r28;
module_param(r28, bool, 0400);
static struct pci_dev *device;
static struct module *owner;
static void *snapshot;
#define SNAPSHOT_BYTES 0x26000
static ssize_t memory_read(struct file *file, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	return memory_read_from_buffer(buf, count, &off, snapshot, SNAPSHOT_BYTES);
}
static BIN_ATTR_ADMIN_RO(memory, SNAPSHOT_BYTES);
static struct bin_attribute *bins[] = { &bin_attr_memory, NULL };
static const struct attribute_group group = { .name = "mt_tqx_snapshot", .bin_attrs = bins };

static int __init start(void)
{
	static const struct { u64 offset, pa; u32 size; } ranges[] = {
		{0x120d000, 0x61000d000ULL, 0x20000},
		{0x122d000, 0x61002d000ULL, 0x1000},
		{0x122e000, 0x61002e000ULL, 0x1000},
		{0x122f000, 0x61002f000ULL, 0x1000},
		{0x1230000, 0x610030000ULL, 0x2000},
		{0x1232000, 0x610032000ULL, 0x1000},
	};
	struct mt_guest *g;
	struct mt_vram_block *block;
	void __iomem *mapping[ARRAY_SIZE(ranges)] = {0};
	u32 i, cursor = 0;
	int ret = -ENODEV;
	if (!enable)
		return -EPERM;
	device = mt_guest_find_s3000();
	if (!device)
		return -ENODEV;
	device_lock(&device->dev);
	if (device->vendor != 0x1ed5 || device->device != 0x0222 ||
	    !device->driver || strcmp(device->driver->name, MT_GUEST_DRIVER_NAME))
		goto unlock_device;
	owner = device->driver->driver.owner;
	if (!owner || !try_module_get(owner))
		goto unlock_device;
	g = pci_get_drvdata(device);
	if (!g)
		goto put_owner;
	mutex_lock(&g->trial_lock);
	if (!g->trial.pinned || !g->trial.published || g->vram.pdev != device ||
	    !g->vram.region_owned)
		goto unlock_guest;
	if (r28 && (!g->queried || !g->info ||
	    le32_to_cpup((__le32 *)(g->info + 8)) != 4 ||
	    le64_to_cpup((__le64 *)(g->info + 0x28)) != 0x605000000ULL))
		goto unlock_guest;
	for (i = 0; i < ARRAY_SIZE(ranges); i++) {
		list_for_each_entry(block, &g->vram.blocks, link)
			if (block->bar_offset == ranges[i].offset &&
			    block->gpu_pa == ranges[i].pa - (r28 ? 0xa000000ULL : 0) &&
			    block->size == ranges[i].size && block->mapping)
				mapping[i] = block->mapping;
		if (!mapping[i])
			goto unlock_guest;
	}
	snapshot = kvmalloc(SNAPSHOT_BYTES, GFP_KERNEL);
	if (!snapshot) {
		ret = -ENOMEM;
		goto unlock_guest;
	}
	for (i = 0; i < ARRAY_SIZE(ranges); i++) {
		memcpy_fromio(snapshot + cursor, mapping[i], ranges[i].size);
		cursor += ranges[i].size;
	}
	ret = sysfs_create_group(&device->dev.kobj, &group);
	if (!ret) {
		mutex_unlock(&g->trial_lock);
		device_unlock(&device->dev);
		pr_info("mt_tqx_snapshot: captured %u bytes from retained allocations; no hardware writes\n", cursor);
		return 0;
	}
	kvfree(snapshot);
unlock_guest:
	mutex_unlock(&g->trial_lock);
put_owner:
	module_put(owner);
unlock_device:
	device_unlock(&device->dev);
	pci_dev_put(device);
	return ret;
}
static void __exit stop(void)
{
	sysfs_remove_group(&device->dev.kobj, &group);
	kvfree(snapshot);
	module_put(owner);
	pci_dev_put(device);
}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only snapshot of retained r23/r28 TQX allocations");
