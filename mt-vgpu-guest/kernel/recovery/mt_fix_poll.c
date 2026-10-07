// SPDX-License-Identifier: GPL-2.0
/* Reset d->service.poll_session back to NULL safely. */
#include "../mt_guest_device.h"

static_assert(sizeof(struct mt_guest_device) == 30784);

static int __init mt_fix_poll_init(void)
{
	struct pci_dev *pdev;
	struct mt_guest *g;
	struct mt_guest_device *d;

	pdev = mt_guest_find_s3000();
	if (!pdev)
		return -ENODEV;
	device_lock(&pdev->dev);
	g = pci_get_drvdata(pdev);
	if (g) {
		d = container_of(g, struct mt_guest_device, state);
		mutex_lock(&g->trial_lock);
		d->service.poll_session = NULL;
		pr_info("mt_fix_poll: d->service.poll_session cleared to NULL\n");
		mutex_unlock(&g->trial_lock);
	}
	device_unlock(&pdev->dev);
	pci_dev_put(pdev);
	return 0;
}

static void __exit mt_fix_poll_exit(void)
{
}

module_init(mt_fix_poll_init);
module_exit(mt_fix_poll_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Reset d->service.poll_session back to NULL safely.");
