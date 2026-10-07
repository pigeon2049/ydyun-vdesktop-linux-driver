// SPDX-License-Identifier: GPL-2.0
/* One Guest-local COMMAND.MASTER + existing-queue notification experiment.
 * Loading is read-only. No reset, firmware upload, queue append, or state write.
 * Automatically clears the MASTER bit after 20 seconds, or at unload.
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/workqueue.h>
#include "../mt_guest_device.h"
#include "../mt_guest_state.h"

static bool enable;
module_param(enable, bool, 0400);
static struct pci_dev *pdev;
static struct module *owner;
static bool attempted, owns_master;
static struct delayed_work restore_work;

static void restore_master(void)
{
	if (owns_master) {
		pci_clear_master(pdev);
		owns_master = false;
	}
}

static void restore_worker(struct work_struct *work)
{
	device_lock(&pdev->dev);
	restore_master();
	device_unlock(&pdev->dev);
}

static ssize_t status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u16 command = 0;
	int ret;
	device_lock(&pdev->dev);
	ret = pci_read_config_word(pdev, PCI_COMMAND, &command);
	if (!ret)
		ret = sysfs_emit(buf, "attempted=%u owns_master=%u command=%04x\n",
			attempted, owns_master, command);
	else
		ret = -EIO;
	device_unlock(&pdev->dev);
	return ret;
}

static ssize_t control_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct mt_guest *g;
	void __iomem *window;
	u32 dm, ring, cursor, i;
	u16 command;
	int ret = -ENODEV;
	if (!sysfs_streq(buf, "enable-and-notify"))
		return -EINVAL;
	device_lock(&pdev->dev);
	if (!pdev->driver || pdev->driver->driver.owner != owner ||
	    strcmp(pdev->driver->name, MT_GUEST_DRIVER_NAME))
		goto unlock_device;
	g = pci_get_drvdata(pdev);
	if (!g)
		goto unlock_device;
	mutex_lock(&g->trial_lock);
	ret = -EALREADY;
	if (attempted)
		goto unlock_guest;
	ret = -EBUSY;
	if (!g->regs || !g->custom || !g->queried || !g->info || g->registered != 15 ||
	    !g->channel[0] || !g->channel[1] || !g->channel[2] || !g->channel[3] ||
	    g->trial.pinned || g->trial.published || g->vram.pdev || g->memory_result ||
	    g->rpc_result || g->rpc_value != 1 || g->version_result ||
	    !(g->host_version & BIT_ULL(56)) ||
	    readl(g->regs + 0x890) != 1 || readl(g->regs + 0x898) != 1 ||
	    g->firmware_bar_offset != 0x3f000000ULL ||
	    readq(g->custom + 0x20) != pci_resource_start(pdev, 2) ||
	    readq(g->custom + 0x30) != 0x771fef000ULL ||
	    readq(g->custom + 0x38) != MT_FW_MAP_SIZE ||
	    pci_read_config_word(pdev, PCI_COMMAND, &command) || command != 3)
		goto unlock_guest;
	ret = pci_request_region(pdev, 2, "mt_master_notify");
	if (ret)
		goto unlock_guest;
	window = pci_iomap_range(pdev, 2, g->firmware_bar_offset, SZ_1M);
	if (!window) {
		ret = -ENOMEM;
		goto release;
	}
	ret = -EBUSY;
	if (readl(window + 4))
		goto unmap;
	for (dm = 0; dm < MT_FW_DM_COUNT; dm++)
		for (ring = 0; ring < 3; ring++) {
			cursor = MT_FW_STATE_BYTES + dm * MT_FW_DM_BYTES + MT_FW_CURSOR_OFFSET + ring * 16;
			if (readl(window + cursor) != ((!dm && !ring) ? 5 : 0) ||
			    readl(window + cursor + 8))
				goto unmap;
		}
	for (i = 0; i < 5; i++)
		if (readl(window + MT_FW_STATE_BYTES + i * MT_FW_COMMAND_BYTES + 0xc) !=
		    (i == 4 ? MT_FW_DISCONNECT : MT_FW_CONNECT))
			goto unmap;
	attempted = true;
	pci_set_master(pdev);
	owns_master = true;
	if (pci_read_config_word(pdev, PCI_COMMAND, &command) || command != 7) {
		restore_master();
		ret = -EIO;
		goto unmap;
	}
	/* Arm rollback before sending either notification. */
	schedule_delayed_work(&restore_work, msecs_to_jiffies(20000));
	writeq(1, g->custom + 0x148); /* 14002249c: Guest online notification */
	mb();
	writel(0, g->regs + 0xb00); /* 14000bf20: DM0, no command appended */
	mb();
	ret = count;
	dev_info(&pdev->dev, "MASTER enabled and existing queue notified once; automatic rollback in 20 seconds\n");
unmap:
	pci_iounmap(pdev, window);
release:
	pci_release_region(pdev, 2);
unlock_guest:
	mutex_unlock(&g->trial_lock);
unlock_device:
	device_unlock(&pdev->dev);
	return ret;
}

static DEVICE_ATTR_RO(status);
static DEVICE_ATTR_WO(control);
static struct attribute *attrs[] = { &dev_attr_status.attr, &dev_attr_control.attr, NULL };
static const struct attribute_group group = { .name = "mt_master_notify", .attrs = attrs };

static int __init start(void)
{
	int ret = -ENODEV;
	if (!enable)
		return -EPERM;
	INIT_DELAYED_WORK(&restore_work, restore_worker);
	pdev = mt_guest_find_s3000();
	if (!pdev)
		return -ENODEV;
	device_lock(&pdev->dev);
	if (!mt_guest_match_s3000(pdev->vendor, pdev->device, pdev->subsystem_vendor, pdev->subsystem_device) ||
	    !pdev->driver || strcmp(pdev->driver->name, MT_GUEST_DRIVER_NAME))
		goto unlock;
	owner = pdev->driver->driver.owner;
	if (!try_module_get(owner))
		goto unlock;
	ret = sysfs_create_group(&pdev->dev.kobj, &group);
	if (ret)
		module_put(owner);
unlock:
	device_unlock(&pdev->dev);
	if (ret)
		pci_dev_put(pdev);
	return ret;
}

static void __exit stop(void)
{
	sysfs_remove_group(&pdev->dev.kobj, &group);
	cancel_delayed_work_sync(&restore_work);
	device_lock(&pdev->dev);
	restore_master();
	device_unlock(&pdev->dev);
	module_put(owner);
	pci_dev_put(pdev);
}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Bounded Guest BusMaster and retained-queue notification experiment; no reset");
