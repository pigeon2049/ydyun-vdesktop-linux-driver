// SPDX-License-Identifier: GPL-2.0
/* Drain any stuck or timed-out pending marker fences in s->pending.
 * Restores s->total and s->count back to 0 without disturbing live HW.
 */
#include "../mt_guest_device.h"

static bool enable;
module_param(enable, bool, 0400);

static_assert(sizeof(struct mt_guest_device) == 30784);
static_assert(offsetof(struct mt_guest_device, markers) == 29888);

static int __init mt_drain_pending_init(void)
{
	struct pci_dev *pdev;
	struct module *owner = NULL;
	struct mt_guest *g;
	struct mt_guest_device *d;
	struct mt_marker_store *s;
	u32 dm, drained = 0;
	int ret = -ENODEV;
	(void)mt_fw_event_io_ops;

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
	if (!owner || !try_module_get(owner)) {
		owner = NULL;
		goto unlock_device;
	}
	g = pci_get_drvdata(pdev);
	if (!g)
		goto unlock_device;
	d = container_of(g, struct mt_guest_device, state);
	s = &d->markers;

	void __iomem *bar0 = pci_iomap(pdev, 0, 4096);
	void __iomem *fw = ioremap(pci_resource_start(pdev, 2) + 0x3f000000ULL, 4096);
	u32 fw0 = fw ? readl(fw) : 999;
	u32 fw4 = fw ? readl(fw + 4) : 999;
	mutex_lock(&g->trial_lock);
	pr_info("mt_drain_pending: inspecting markers: total=%u ready=%d work_ready=%d\n",
		s->total, s->ready, s->work_ready);
	pr_info("mt_drain_pending: diag: pub=%d ev_res=%d pin=%d conn=%d srv=%d normal=%d direct_890=%u direct_898=%u fw0=%#x fw4=%u\n",
		d->runtime.published, d->runtime.event_result, g->trial.pinned, g->trial.connected,
		d->service.running, (g->registered == 15 && g->channel[0]) ? READ_ONCE(*(u8 *)g->channel[0]) : 0,
		bar0 ? readl(bar0 + 0x890) : 999,
		bar0 ? readl(bar0 + 0x898) : 999,
		fw0, fw4);
	if (fw)
		iounmap(fw);
	if (bar0)
		pci_iounmap(pdev, bar0);

	for (dm = 0; dm < MT_FW_DM_COUNT; dm++) {
		struct mt_marker_fence *m, *tmp;
		pr_info("mt_drain_pending: dm[%u] count=%u empty=%d\n",
			dm, s->count[dm], list_empty(&s->pending[dm]));
		list_for_each_entry_safe(m, tmp, &s->pending[dm], link) {
			list_del(&m->link);
			if (s->count[dm])
				s->count[dm]--;
			if (s->total)
				s->total--;
			drained++;
			if (m->job.state == MT_JOB_PUBLISHED)
				mt_work_job_complete(&m->job);
			if (m->context) {
				m->context->active_jobs--;
				m->context = NULL;
			}
			dma_fence_set_error(&m->fence, -ETIMEDOUT);
			dma_fence_signal(&m->fence);
			dma_fence_put(&m->fence);
		}
	}
	pr_info("mt_drain_pending: drained=%u remaining_total=%u\n", drained, s->total);
	ret = 0;
	mutex_unlock(&g->trial_lock);

unlock_device:
	if (owner)
		module_put(owner);
	device_unlock(&pdev->dev);
	pci_dev_put(pdev);
	return ret;
}

static void __exit mt_drain_pending_exit(void)
{
}

module_init(mt_drain_pending_init);
module_exit(mt_drain_pending_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Drain pending marker fences in mt-vgpu-guest");
