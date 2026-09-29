// SPDX-License-Identifier: GPL-2.0
/* One NULL command through the retained driver's own submission/fence ops.
 * The owning driver keeps pending resources even if this observer times out.
 * No workload gate, firmware state, descriptor or resource mapping is changed.
 */
#include "../mt_guest_device.h"

static bool enable;
module_param(enable, bool, 0400);
static int result = -ENODATA;
module_param(result, int, 0444);
static unsigned long long sequence;
module_param(sequence, ullong, 0444);

/* These offsets are checked against the saved, currently loaded r23 module.
 * They deliberately reject another architecture/configuration/layout. */
static_assert(sizeof(struct mt_guest_device) == 30784);
static_assert(offsetof(struct mt_guest_device, service) == 29552);
static_assert(offsetof(struct mt_guest_device, runtime) == 29712);
static_assert(offsetof(struct mt_guest_device, markers) == 29888);

static int __init mt_live_marker_init(void)
{
	struct pci_dev *pdev;
	struct module *owner = NULL;
	struct mt_guest *g;
	struct mt_guest_device *d;
	struct mt_marker_store *s;
	struct dma_fence *fence = NULL;
	long waited;
	int ret = -ENODEV;
	u32 dm;
	(void)mt_fw_event_io_ops;

	if (!enable)
		return -EPERM;
	pdev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(14, 0));
	if (!pdev)
		return -ENODEV;
	device_lock(&pdev->dev);
	if (pdev->vendor != 0x1ed5 || pdev->device != 0x0222 ||
	    pdev->subsystem_vendor != 0x1ed5 || pdev->subsystem_device != 0x1101 ||
	    !pdev->driver || strcmp(pdev->driver->name, "mt_guest_probe"))
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
	s = &d->markers;
	mutex_lock(&g->trial_lock);
	ret = -EBUSY;
	if (!d->runtime.published || d->runtime.event_result ||
	    !g->trial.pinned || !g->trial.connected || !d->service.running ||
	    s->lock != &g->trial_lock || s->queue != &g->firmware_queue ||
	    s->opaque != g || !s->ops || !s->ops->submit || !s->can_submit ||
	    s->total || s->ready || s->work_ready)
		goto unlock_session;
	for (dm = 0; dm < MT_FW_DM_COUNT; dm++)
		if (s->count[dm] || !list_empty(&s->pending[dm]))
			goto unlock_session;
	ret = s->can_submit(g);
	if (ret)
		goto unlock_session;
	s->ready = true;
	/* Crucial: invoke code owned by the original module, never this helper's
	 * inline copy, so the pending fence pins the correct module on timeout. */
	ret = s->ops->submit(s, 1, &fence);
	s->ready = false;
	if (!ret)
		sequence = fence->seqno;
unlock_session:
	mutex_unlock(&g->trial_lock);
unlock_device:
	device_unlock(&pdev->dev);
	if (!ret) {
		waited = dma_fence_wait_timeout(fence, false, msecs_to_jiffies(3000));
		ret = waited > 0 ? dma_fence_get_status(fence) :
			(waited < 0 ? (int)waited : -ETIMEDOUT);
		if (ret == 1)
			ret = 0;
	}
	dma_fence_put(fence);
	if (owner)
		module_put(owner);
	pci_dev_put(pdev);
	result = ret;
	pr_info("mt_live_marker: dm=1 sequence=%llu result=%d workload_enabled=0\n",
		sequence, result);
	return 0;
}
static void __exit mt_live_marker_exit(void) {}
module_init(mt_live_marker_init);
module_exit(mt_live_marker_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("One guarded NULL submission through the retained Guest driver");
