// SPDX-License-Identifier: GPL-2.0
/* Reconnect the live guest session to firmware.
 * Aligns any mismatched queue cursor and runs
 * mt_fw_connect() using exact trial connection ops.
 */
#include "../mt_guest_device.h"

static bool enable;
module_param(enable, bool, 0400);

static_assert(sizeof(struct mt_guest_device) == 30784);
static_assert(offsetof(struct mt_guest_device, markers) == 29888);

static void reconnect_delay(void *opaque)
{
	(void)opaque;
	msleep(25);
}

static u32 reconnect_gpu_normal(void *opaque)
{
	struct mt_fw_trial *t = opaque;
	struct mt_guest *g = container_of(t, struct mt_guest, trial);
	if (g->registered != 15 || !g->channel[0])
		return 0;
	return READ_ONCE(*(u8 *)g->channel[0]);
}

static const struct mt_fw_connection_ops reconnect_ops = {
	.firmware_state = mt_trial_fw_state,
	.firmware_started = mt_trial_started,
	.guest_state = mt_trial_guest_state,
	.notify_online = mt_trial_online,
	.send_command = mt_trial_send,
	.work_idle = mt_trial_work_idle,
	.control_idle = mt_trial_control_idle,
	.delay_25ms = reconnect_delay,
	.gpu_normal = reconnect_gpu_normal,
};

static int __init mt_reconnect_init(void)
{
	struct pci_dev *pdev;
	struct module *owner = NULL;
	struct mt_guest *g;
	struct mt_guest_device *d;
	u32 dm, ring, cursor, head, tail;
	int ret = -ENODEV;
	(void)mt_fw_event_io_ops;

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
	if (!owner || !try_module_get(owner)) {
		owner = NULL;
		goto unlock_device;
	}
	g = pci_get_drvdata(pdev);
	if (!g)
		goto unlock_device;
	d = container_of(g, struct mt_guest_device, state);

	mutex_lock(&g->trial_lock);
	pr_info("mt_reconnect: pre-check guest=%u fw=%u started=%u\n",
		readl(g->regs + 0x890), mt_trial_fw_state(&g->trial), mt_trial_started(&g->trial));

	/* Check and fix any mismatched ring cursors */
	for (dm = 0; dm < MT_FW_DM_COUNT; dm++) {
		for (ring = 0; ring < 3; ring++) {
			cursor = dm * MT_FW_DM_BYTES + MT_FW_CURSOR_OFFSET + ring * 16;
			head = readl(g->firmware_queue.queue + cursor);
			tail = readl(g->firmware_queue.queue + cursor + 8);
			if (head != tail) {
				pr_info("mt_reconnect: syncing dm=%u ring=%u head=%u tail=%u\n",
					dm, ring, head, tail);
				if (ring == 2) {
					/* Event ring: guest consumer updates tail to catch up with fw head */
					writel(head, g->firmware_queue.queue + cursor + 8);
				} else {
					/* Command ring: guest producer resets head to match tail */
					writel(tail, g->firmware_queue.queue + cursor);
				}
				mb();
			}
		}
	}

	d->runtime.event_result = 0;
	ret = mt_fw_connect(&reconnect_ops, &g->trial);
	pr_info("mt_reconnect: mt_fw_connect returned %d; guest=%u fw=%u\n",
		ret, readl(g->regs + 0x890), mt_trial_fw_state(&g->trial));
	if (!ret) {
		g->trial.connected = true;
		d->runtime.notified = true;
	}
	mutex_unlock(&g->trial_lock);

unlock_device:
	if (owner)
		module_put(owner);
	device_unlock(&pdev->dev);
	pci_dev_put(pdev);
	return ret;
}

static void __exit mt_reconnect_exit(void)
{
}

module_init(mt_reconnect_init);
module_exit(mt_reconnect_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Live reconnect for mt-vgpu-guest");
