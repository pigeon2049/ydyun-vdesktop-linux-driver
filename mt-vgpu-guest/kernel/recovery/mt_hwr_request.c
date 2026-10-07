// SPDX-License-Identifier: GPL-2.0
/* Explicit one-shot Guest HWR request, mtkm64.sys 140022f24.
 * WARNING: old Host 2.3 can schedule MPC-wide reset_gpu. This is not a
 * VM-local reset primitive. Loading never issues the request; root must
 * explicitly write request-hwr to control after authorizing its scope.
 * No firmware, queue, Guest state, or shared control fields are modified.
 */
#include <linux/module.h>
#include <linux/pci.h>
#include "../mt_guest_device.h"
#include "../mt_guest_state.h"

static bool enable;
module_param(enable, bool, 0400);
static struct pci_dev *pdev;
static struct module *owner;
static bool attempted;
static u32 before_guest, before_firmware;
static u8 before_normal;

/* Caller holds device_lock, then trial_lock before dereferencing buffers. */
static struct mt_guest *bound_guest(void)
{
	if (!pdev->driver || pdev->driver->driver.owner != owner ||
	    strcmp(pdev->driver->name, MT_GUEST_DRIVER_NAME))
		return NULL;
	return pci_get_drvdata(pdev);
}

static ssize_t status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mt_guest *g;
	ssize_t ret = -ENODEV;
	device_lock(&pdev->dev);
	g = bound_guest();
	if (g) {
		mutex_lock(&g->trial_lock);
		if (g->regs && g->registered == 15 && g->channel[0])
			ret = sysfs_emit(buf,
				"attempted=%u guest=%u firmware=%u gpu_normal=%u before_guest=%u before_firmware=%u before_normal=%u scope=potential_mpc_reset\n",
				attempted, readl(g->regs + 0x890), readl(g->regs + 0x898),
				READ_ONCE(*(u8 *)g->channel[0]), before_guest, before_firmware, before_normal);
		mutex_unlock(&g->trial_lock);
	}
	device_unlock(&pdev->dev);
	return ret;
}

static ssize_t control_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct mt_guest *g;
	ssize_t ret = -ENODEV;
	if (!sysfs_streq(buf, "request-hwr"))
		return -EINVAL;
	device_lock(&pdev->dev);
	g = bound_guest();
	if (!g)
		goto unlock_device;
	mutex_lock(&g->trial_lock);
	ret = -EALREADY;
	if (attempted)
		goto unlock_guest;
	ret = -EBUSY;
	/* Only the investigated retained recovery session, never a new trial. */
	if (!g->regs || !g->custom || !g->info || !g->queried ||
	    g->registered != 15 || !g->channel[0] || !g->channel[1] ||
	    !g->channel[2] || !g->channel[3] || g->trial.pinned ||
	    g->trial.published || g->vram.pdev || g->memory_result ||
	    g->rpc_value != 1 || g->version_result || !(g->host_version & BIT_ULL(56)) ||
	    mt_info_u32((void *)g->info, 8) != 4 ||
	    readq(g->custom + 0x30) != 0x771fef000ULL ||
	    readq(g->custom + 0x38) != MT_FW_MAP_SIZE)
		goto unlock_guest;
	before_guest = readl(g->regs + 0x890);
	before_firmware = readl(g->regs + 0x898);
	before_normal = READ_ONCE(*(u8 *)g->channel[0]);
	if (before_guest != 1 || before_firmware != 1)
		goto unlock_guest;
	/* Commit the attempt before the notification; no automatic retry. */
	attempted = true;
	mb();
	writeq(1, g->custom + 0x118);
	mb();
	ret = count;
	dev_info(&pdev->dev, "Guest HWR requested once; this does not confirm recovery\n");
unlock_guest:
	mutex_unlock(&g->trial_lock);
unlock_device:
	device_unlock(&pdev->dev);
	return ret;
}

static DEVICE_ATTR_RO(status);
static DEVICE_ATTR_WO(control);
static struct attribute *attrs[] = { &dev_attr_status.attr, &dev_attr_control.attr, NULL };
static const struct attribute_group group = { .name = "mt_hwr_request", .attrs = attrs };

static int __init start(void)
{
	int ret = -ENODEV;
	if (!enable)
		return -EPERM;
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
	module_put(owner);
	pci_dev_put(pdev);
}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Explicit Guest HWR request; potentially affects the Host MPC, not a VM-local reset");
