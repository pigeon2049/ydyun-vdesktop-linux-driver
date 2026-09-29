/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_RPC_SERVICE_H
#define MT_RPC_SERVICE_H
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include "mt_guest_state.h"
#include "mt_rpc_transport.h"

/* Kept outside struct mt_guest: the retained first-trial companion uses that
 * exact ABI. Page addresses remain fixed from start until IRQ/work teardown.
 */
struct mt_rpc_service {
	struct mt_guest *guest;
	struct pci_dev *pdev;
	struct delayed_work work;
	void *page0;
	bool running, ready;
	atomic64_t acknowledgments;
	u64 replies, rejected;
	/* Optional post-connect service, serialized by the same trial_lock. */
	void (*poll_session)(struct mt_guest *guest);
};

/* Caller holds trial_lock. This also runs inline while a connection wait
 * holds that lock, so telemetry is not starved for the whole 10-second wait.
 */
static inline int mt_rpc_service_poll(struct mt_rpc_service *s)
{
	struct mt_guest *g = s->guest;
	struct mt_vram_block *b;
	u64 allocated = 0;
	u32 sent;
	int ret;
	if (g->registered != 15)
		return 0;
	if (g->vram.pdev)
		list_for_each_entry(b, &g->vram.blocks, link)
			allocated += b->size;
	ret = mt_rpc_answer_queries((void *)g->channel[1], g->custom,
				     allocated, 0, 0, &sent);
	s->replies += sent;
	if (ret) {
		s->rejected++;
		dev_warn_ratelimited(&s->pdev->dev, "RPC service condition %d; request retained\n", ret);
	}
	if (s->poll_session)
		s->poll_session(g);
	return ret ? ret : sent;
}

static irqreturn_t mt_rpc_service_irq(int irq, void *data)
{
	struct mt_rpc_service *s = data;
	if (!mt_rpc_ack(s->page0))
		return IRQ_NONE;
	atomic64_inc(&s->acknowledgments);
	if (READ_ONCE(s->running))
		mod_delayed_work(system_wq, &s->work, 0);
	return IRQ_HANDLED;
}

static void mt_rpc_service_work(struct work_struct *work)
{
	struct mt_rpc_service *s = container_of(to_delayed_work(work), struct mt_rpc_service, work);
	/* Never block on this mutex: teardown may hold it while cancelling work.
	 * Probe/resource allocation holds it too; ready stays false until complete.
	 */
	if (READ_ONCE(s->ready) && mutex_trylock(&s->guest->trial_lock)) {
		mt_rpc_service_poll(s);
		mutex_unlock(&s->guest->trial_lock);
	}
	if (READ_ONCE(s->running))
		schedule_delayed_work(&s->work, msecs_to_jiffies(250));
}

/* Called after allocating all four pages, BEFORE registering the first one. */
static inline int mt_rpc_service_start(struct mt_rpc_service *s,
		struct pci_dev *pdev, struct mt_guest *g)
{
	int ret;
	if (s->running || !g->channel[0] || g->registered || !pdev->irq)
		return -EINVAL;
	s->guest = g;
	s->pdev = pdev;
	s->page0 = (void *)g->channel[0];
	atomic64_set(&s->acknowledgments, 0);
	INIT_DELAYED_WORK(&s->work, mt_rpc_service_work);
	WRITE_ONCE(s->running, true);
	ret = request_irq(pdev->irq, mt_rpc_service_irq, IRQF_SHARED, "mt_guest_rpc", s);
	if (ret) {
		WRITE_ONCE(s->running, false);
		return ret;
	}
	schedule_delayed_work(&s->work, msecs_to_jiffies(250));
	return 0;
}

/* Unregister Host channels first; keep their backing pages until this returns. */
static inline void mt_rpc_service_stop(struct mt_rpc_service *s)
{
	if (!s->running)
		return;
	WRITE_ONCE(s->ready, false);
	WRITE_ONCE(s->running, false);
	free_irq(s->pdev->irq, s);
	cancel_delayed_work_sync(&s->work);
	s->page0 = NULL;
}
#endif
