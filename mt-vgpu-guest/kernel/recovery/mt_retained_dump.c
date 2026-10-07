// SPDX-License-Identifier: GPL-2.0
/* Read-only snapshot of the six ranges uploaded by the retained first trial.
 * Does not replace the bound transport or change any device register/memory.
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include "../mt_guest_device.h"
#include "../mt_guest_state.h"

static bool enable;
module_param(enable, bool, 0400);
static struct pci_dev *pdev;
static struct module *owner;
static void *snapshot;

static ssize_t memory_read(struct file *file, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	return memory_read_from_buffer(buf, count, &off, snapshot, MT_TRIAL_BACKUP_BYTES);
}
static BIN_ATTR_ADMIN_RO(memory, MT_TRIAL_BACKUP_BYTES);
static struct bin_attribute *bins[] = { &bin_attr_memory, NULL };
static const struct attribute_group group = {
	.name = "mt_retained_dump", .bin_attrs = bins,
};

static int __init start(void)
{
	static const struct { u32 pool, offset, size; } ranges[] = {
		{ MT_POOL_FIRMWARE, 0, MT_FW_MAP_SIZE },
		{ MT_POOL_NORMAL, 0, MT_FW_TABLE_BYTES },
		{ MT_POOL_NORMAL, 0x6000, PAGE_SIZE },
		{ MT_POOL_NORMAL, 0x7000, MT_MMU_DUMMY_BYTES },
		{ MT_POOL_NORMAL, 0x20a000, MT_STATIC_RESOURCE_BYTES },
		{ MT_POOL_NORMAL, 0x28a000, MT_STATIC_RESOURCE_BYTES },
	};
	struct mt_memory_layout layout;
	struct mt_guest *g;
	void __iomem *mapping;
	u32 i, cursor = 0;
	int ret = -ENODEV;
	if (!enable)
		return -EPERM;
	pdev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(14, 0));
	if (!pdev)
		return -ENODEV;
	device_lock(&pdev->dev);
	if (!mt_guest_match_s3000(pdev->vendor, pdev->device, pdev->subsystem_vendor, pdev->subsystem_device) ||
	    !pdev->driver || strcmp(pdev->driver->name, "mt_guest_probe"))
		goto unlock_device;
	owner = pdev->driver->driver.owner;
	if (!try_module_get(owner))
		goto unlock_device;
	g = pci_get_drvdata(pdev);
	if (!g)
		goto put_owner;
	mutex_lock(&g->trial_lock);
	ret = -EBUSY;
	if (!g->queried || !g->info || g->registered != 15 || g->trial.pinned ||
	    g->trial.published || g->vram.pdev || g->memory_result ||
	    readl(g->regs + 0x890) != 1 || readl(g->regs + 0x898) != 1)
		goto unlock_guest;
	ret = mt_memory_parse((void *)g->info, PAGE_SIZE, pci_resource_len(pdev, 2), &layout);
	if (ret)
		goto unlock_guest;
	if (layout.pool[MT_POOL_FIRMWARE].gpu_pa != 0x771fef000ULL ||
	    layout.pool[MT_POOL_FIRMWARE].bar_offset != 0x3f000000ULL ||
	    layout.pool[MT_POOL_NORMAL].gpu_pa != 0x605800000ULL ||
	    layout.pool[MT_POOL_NORMAL].bar_offset != 0xa00000ULL) {
		ret = -EPROTO;
		goto unlock_guest;
	}
	snapshot = kvmalloc(MT_TRIAL_BACKUP_BYTES, GFP_KERNEL);
	if (!snapshot) {
		ret = -ENOMEM;
		goto unlock_guest;
	}
	ret = pci_request_region(pdev, 2, "mt_retained_dump");
	if (ret)
		goto free_snapshot;
	for (i = 0; i < ARRAY_SIZE(ranges); i++) {
		const struct mt_memory_range *pool = &layout.pool[ranges[i].pool];
		if (ranges[i].offset > pool->size || ranges[i].size > pool->size - ranges[i].offset ||
		    ranges[i].size > MT_TRIAL_BACKUP_BYTES - cursor) {
			ret = -ERANGE;
			goto release;
		}
		mapping = pci_iomap_range(pdev, 2, pool->bar_offset + ranges[i].offset, ranges[i].size);
		if (!mapping) {
			ret = -ENOMEM;
			goto release;
		}
		memcpy_fromio(snapshot + cursor, mapping, ranges[i].size);
		pci_iounmap(pdev, mapping);
		cursor += ranges[i].size;
	}
	if (cursor != MT_TRIAL_BACKUP_BYTES || readl(g->regs + 0x890) != 1 ||
	    readl(g->regs + 0x898) != 1) {
		ret = -EAGAIN;
		goto release;
	}
	ret = sysfs_create_group(&pdev->dev.kobj, &group);
release:
	pci_release_region(pdev, 2);
	if (!ret) {
		mutex_unlock(&g->trial_lock);
		device_unlock(&pdev->dev);
		pr_info("mt_retained_dump: captured %u bytes; no device writes\n", cursor);
		return 0;
	}
free_snapshot:
	kvfree(snapshot);
	snapshot = NULL;
unlock_guest:
	mutex_unlock(&g->trial_lock);
put_owner:
	module_put(owner);
unlock_device:
	device_unlock(&pdev->dev);
	pci_dev_put(pdev);
	return ret;
}

static void __exit stop(void)
{
	sysfs_remove_group(&pdev->dev.kobj, &group);
	kvfree(snapshot);
	module_put(owner);
	pci_dev_put(pdev);
}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only snapshot of the investigated retained Guest firmware ranges");
