// SPDX-License-Identifier: GPL-2.0
/* Query only the two updater metadata messages from 1400266e0.
 * Never invoke its 140026670 download step or install any package.
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include "../mt_guest_device.h"
#include "../mt_guest_state.h"
#include "../mt_rpc_publish.h"
#include "../mt_rpc_transport.h"

static bool enable;
module_param(enable, bool, 0400);
static struct pci_dev *pdev;
static struct module *owner;
static u64 enabled_value, package_value;
static int enabled_result = -ENODATA, package_result = -ENODATA;
static u32 telemetry_replies;
static u8 responses[2][32];

static int query(struct mt_guest *g, u8 subtype, u64 value, u64 *answer, u8 *saved)
{
	u8 record[32] = {0}, *ring = (u8 *)g->channel[1] + 3 * 0x210;
	u8 head, tail;
	u32 sent, i;
	int ret;
	/* Do not steal an outstanding query from another caller. */
	head = READ_ONCE(ring[0]);
	tail = READ_ONCE(ring[1]);
	if (head >= 16 || tail >= 16 || head != tail)
		return -EBUSY;
	memcpy(record, &value, sizeof(value));
	record[9] = subtype;
	record[11] = 1;
	ret = mt_rpc_publish((void *)g->channel[1], g->custom, 0, record, 1);
	if (ret)
		return ret;
	for (i = 0; i < 100; i++) {
		ret = mt_rpc_answer_queries((void *)g->channel[1], g->custom, 0, 0, 0, &sent);
		telemetry_replies += sent;
		if (ret)
			return ret;
		head = READ_ONCE(ring[0]);
		tail = READ_ONCE(ring[1]);
		if (head >= 16 || tail >= 16)
			return -EPROTO;
		if (head != tail) {
			rmb();
			memcpy(saved, ring + 16 + tail * 32, 32);
			if (saved[8] || saved[9] != subtype || saved[11] != 2)
				return -EPROTO;
			memcpy(answer, saved, sizeof(*answer));
			mb();
			WRITE_ONCE(ring[1], (tail + 1) & 15);
			return saved[10] ? -EREMOTEIO : 0;
		}
		usleep_range(1000, 2000);
	}
	return -ETIMEDOUT;
}

static ssize_t status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "enabled_result=%d enabled_value=%#llx package_result=%d package_value=%#llx telemetry_replies=%u downloaded=0 installed=0\nresponse5=%*phN\nresponse6=%*phN\n",
		enabled_result, enabled_value, package_result, package_value, telemetry_replies,
		32, responses[0], 32, responses[1]);
}
static DEVICE_ATTR_RO(status);
static struct attribute *attrs[] = { &dev_attr_status.attr, NULL };
static const struct attribute_group group = { .name = "mt_package_probe", .attrs = attrs };

static int __init start(void)
{
	struct mt_guest *g;
	int ret = -ENODEV;
	if (!enable)
		return -EPERM;
	pdev = mt_guest_find_s3000();
	if (!pdev)
		return -ENODEV;
	device_lock(&pdev->dev);
	if (!mt_guest_match_s3000(pdev->vendor, pdev->device, pdev->subsystem_vendor, pdev->subsystem_device) ||
	    !pdev->driver || strcmp(pdev->driver->name, MT_GUEST_DRIVER_NAME))
		goto unlock_device;
	owner = pdev->driver->driver.owner;
	if (!try_module_get(owner))
		goto unlock_device;
	g = pci_get_drvdata(pdev);
	if (!g)
		goto put_owner;
	mutex_lock(&g->trial_lock);
	ret = -EBUSY;
	if (g->registered != 15 || !g->queried || g->trial.pinned ||
	    g->trial.published || g->vram.pdev || g->rpc_value != 1 || g->version_result ||
	    !(g->host_version & BIT_ULL(56)) ||
	    readl(g->regs + 0x890) != 1 || readl(g->regs + 0x898) != 1)
		goto unlock_guest;
	enabled_result = query(g, 5, 0, &enabled_value, responses[0]);
	if (!enabled_result && enabled_value)
		package_result = query(g, 6, 0x48809490dULL, &package_value, responses[1]);
	ret = sysfs_create_group(&pdev->dev.kobj, &group);
unlock_guest:
	mutex_unlock(&g->trial_lock);
	if (!ret) {
		device_unlock(&pdev->dev);
		pr_info("mt_package_probe: metadata queries complete; no download or installation\n");
		return 0;
	}
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
	module_put(owner);
	pci_dev_put(pdev);
}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Guest updater metadata-only query; no downloads or updates");
