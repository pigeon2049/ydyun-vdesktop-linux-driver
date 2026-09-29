// SPDX-License-Identifier: GPL-2.0
/* Temporary companion for the retained first trial, not a replacement PCI
 * driver. Uses the exact shared struct layout of that loaded module; never
 * unbinds it, frees its resources, or changes firmware state/command queues.
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include "../mt_guest_state.h"
#include "../mt_rpc_transport.h"
#include "../mt_guest_announcements.h"
#include "../mt_rpc_publish.h"

static bool enable;
module_param(enable, bool, 0400);
static bool restore_shared_irq;
module_param(restore_shared_irq, bool, 0400);
static struct pci_dev *pdev;
static struct module *owner;
static struct mt_guest *guest;
static unsigned long channels[4];
static bool irq_registered;
static atomic64_t acknowledgments = ATOMIC64_INIT(0);
static unsigned long replies, rejected;
static bool bar2_published;
static bool shared_announced;
static bool local_mmu_published;
static bool package_announced;
static struct delayed_work service_work;

/* 140023810 and host 2.3 mtgpu_vgpu_ack_irq agree on this shared-RAM
 * handshake. Claim only a pending MTT interrupt; do not claim QXL/USB IRQs.
 * Counter +8 has one writer here (initial ack before installing handler).
 */
static bool acknowledge(void)
{
	if (!mt_rpc_ack((void *)channels[0]))
		return false;
	atomic64_inc(&acknowledgments);
	return true;
}

static irqreturn_t shared_irq(int irq, void *data)
{
	if (!acknowledge())
		return IRQ_NONE;
	mod_delayed_work(system_wq, &service_work, 0);
	return IRQ_HANDLED;
}

/* Caller holds the original trial lock, also used by its sysfs operations.
 * Ring 1 is Host -> Guest requests, ring 2 replies; 14002bd70/14002bf90.
 * No payload addresses are dereferenced. Full/invalid/unknown rings remain
 * intact. Padding is zeroed rather than copying uninitialized reference data.
 */
static int answer_queries(void)
{
	struct mt_vram_block *block;
	u64 allocated = 0;
	u32 sent;
	int ret;
	list_for_each_entry(block, &guest->vram.blocks, link)
		allocated += block->size;
	ret = mt_rpc_answer_queries((void *)channels[1], guest->custom,
				     allocated, 0, 0, &sent);
	replies += sent;
	return ret ? ret : sent;
}

static void service(struct work_struct *work)
{
	int ret;
	mutex_lock(&guest->trial_lock);
	if (guest->registered == 15 && guest->trial.pinned &&
	    guest->channel[0] == channels[0] && guest->channel[1] == channels[1]) {
		ret = answer_queries();
		if (ret < 0) {
			rejected++;
			pr_warn_ratelimited("mt_live_service: unhandled RPC condition %d\n", ret);
		}
	}
	mutex_unlock(&guest->trial_lock);
	schedule_delayed_work(&service_work, msecs_to_jiffies(250));
}

static ssize_t status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct irq_data *irq = irq_get_irq_data(pdev->irq);
	u8 *ring = (u8 *)channels[1];
	return sysfs_emit(buf,
		"irq=%u disabled=%d shared_status=%u acknowledgments=%lld replies=%lu rejected=%lu request_head=%u request_tail=%u reply_head=%u reply_tail=%u\n",
		pdev->irq, irq ? irqd_irq_disabled(irq) : -1,
		READ_ONCE(*(u32 *)(channels[0] + 4)), atomic64_read(&acknowledgments),
		READ_ONCE(replies), READ_ONCE(rejected), READ_ONCE(ring[0x210]),
		READ_ONCE(ring[0x211]), READ_ONCE(ring[0x420]), READ_ONCE(ring[0x421]));
}
static DEVICE_ATTR_RO(status);

static ssize_t publication_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t ret;
	mutex_lock(&guest->trial_lock);
	/* Read only the four address/length slots already written by the
	 * reference initialization path. These are BAR1 configuration values,
	 * not proof that Host has installed the corresponding firmware mapping.
	 */
	ret = sysfs_emit(buf,
		"bar2_gpa=%#llx local_mmu_gpa=%#llx firmware_pa=%#llx firmware_bytes=%#llx expected_firmware_pa=%#llx expected_firmware_bytes=%#x\n",
		readq(guest->custom + 0x20), readq(guest->custom + 0x28),
		readq(guest->custom + 0x30), readq(guest->custom + 0x38),
		guest->vram_blocks[0].gpu_pa, guest->vram_blocks[0].size);
	mutex_unlock(&guest->trial_lock);
	return ret;
}
static DEVICE_ATTR_RO(publication);

static ssize_t control_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	int ret = 0;
	u8 records[96];
	mutex_lock(&guest->trial_lock);
	if (!guest->trial.pinned || !guest->trial.published || guest->registered != 15 ||
	    readl(guest->regs + 0x890) != 1 || readl(guest->regs + 0x898) != 1) {
		ret = -EBUSY;
		goto out;
	}
	if (sysfs_streq(buf, "publish-bar2")) {
		/* Missing from the first trial: 140026b30 always publishes the
		 * PCI aperture GPA after its info query, before firmware connect.
		 * This is PCI BAR2's guest physical base, not a GPU allocation PA.
		 */
		if (bar2_published) {
			ret = -EALREADY;
			goto out;
		}
		writeq(pci_resource_start(pdev, 2), guest->custom + 0x20);
		mb();
		bar2_published = true;
		pr_info("mt_live_service: published BAR2 guest base=%pa\n", &pdev->resource[2].start);
	} else if (sysfs_streq(buf, "publish-local-mmu")) {
		/* 14002fe44: platform+518 is the BAR4 resource base.
		 * This recovery action is restricted to the observed absent BAR4.
		 * Publishing a nonzero aperture needs a separately reviewed mapping.
		 */
		if (local_mmu_published) {
			ret = -EALREADY;
			goto out;
		}
		if (pci_resource_start(pdev, 4) || pci_resource_flags(pdev, 4)) {
			ret = -EOPNOTSUPP;
			goto out;
		}
		writeq(0, guest->custom + 0x28);
		mb();
		local_mmu_published = true;
		pr_info("mt_live_service: published absent local-MMU BAR4 base=0\n");
	} else if (sysfs_streq(buf, "announce-package")) {
		if (package_announced) {
			ret = -EALREADY;
			goto out;
		}
		if (guest->rpc_value != 1 || guest->version_result ||
		    !(guest->host_version & BIT_ULL(56))) {
			ret = -EPROTO;
			goto out;
		}
		ret = mt_package_announcement(guest->rpc_value, records);
		if (!ret)
			ret = mt_rpc_publish((void *)channels[1], guest->custom, 0, records, 1);
		if (!ret) {
			package_announced = true;
			pr_info("mt_live_service: announced reference package token %#llx\n",
				MT_REFERENCE_PACKAGE_TOKEN);
		}
	} else if (sysfs_streq(buf, "announce-shared")) {
		if (shared_announced) {
			ret = -EALREADY;
			goto out;
		}
		ret = mt_shared_announcements((void *)guest->info, PAGE_SIZE,
			pci_resource_start(pdev, 2), pci_resource_len(pdev, 2), records);
		if (!ret)
			ret = mt_rpc_publish((void *)channels[1], guest->custom, 0, records, 3);
		if (!ret) {
			shared_announced = true;
			pr_info("mt_live_service: announced dedicated shared BAR region\n");
		}
	} else if (sysfs_streq(buf, "notify-online")) {
		/* Retry the original notification and doorbell only. Preserve
		 * all existing queue entries, including the pending disconnect.
		 */
		writeq(1, guest->custom + 0x148);
		mb();
		guest->trial.online_count++;
		writel(0, guest->regs + 0xb00);
		mb();
	} else {
		ret = -EINVAL;
	}
out:
	mutex_unlock(&guest->trial_lock);
	return ret ? ret : count;
}
static DEVICE_ATTR_WO(control);
static struct attribute *attrs[] = {
	&dev_attr_status.attr, &dev_attr_publication.attr, &dev_attr_control.attr, NULL
};
static const struct attribute_group group = { .name = "mt_live", .attrs = attrs };

static int __init start(void)
{
	struct irq_data *irq;
	u16 command;
	u32 i;
	int ret = -ENODEV;
	if (!enable)
		return -EPERM;
	pdev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(14, 0));
	if (!pdev || pdev->vendor != 0x1ed5 || pdev->device != 0x0222 || pdev->irq != 10)
		goto put_device;
	device_lock(&pdev->dev);
	if (!pdev->driver || strcmp(pdev->driver->name, "mt_guest_probe"))
		goto unlock;
	owner = pdev->driver->driver.owner;
	if (!try_module_get(owner))
		goto unlock;
	guest = pci_get_drvdata(pdev);
	if (!guest || guest->registered != 15 || !guest->trial.pinned ||
	    guest->vram.pdev != pdev || !guest->custom) {
		ret = -EINVAL;
		goto put_owner;
	}
	if (pci_read_config_word(pdev, PCI_COMMAND, &command) ||
	    !(command & PCI_COMMAND_INTX_DISABLE)) {
		ret = -EBUSY;
		goto put_owner;
	}
	mutex_lock(&guest->trial_lock);
	for (i = 0; i < 4; i++) {
		channels[i] = guest->channel[i];
		if (!channels[i] || !IS_ALIGNED(channels[i], PAGE_SIZE) ||
		    !virt_addr_valid((void *)channels[i])) {
			ret = -EINVAL;
			goto put_pages;
		}
		get_page(virt_to_page((void *)channels[i]));
	}
	INIT_DELAYED_WORK(&service_work, service);
	ret = sysfs_create_group(&pdev->dev.kobj, &group);
	if (ret)
		goto put_pages;
	/* Recovered acknowledgement precedes the IRQ handler installation. */
	acknowledge();
	ret = request_irq(pdev->irq, shared_irq, IRQF_SHARED, "mt_guest_live", pdev);
	if (ret) {
		sysfs_remove_group(&pdev->dev.kobj, &group);
		goto put_pages;
	}
	irq_registered = true;
	ret = answer_queries();
	pr_info("mt_live_service: initial replies=%d shared_status=%u\n", ret,
		READ_ONCE(*(u32 *)(channels[0] + 4)));
	mutex_unlock(&guest->trial_lock);
	device_unlock(&pdev->dev);
	/* Give the Host time to observe the memory acknowledgement. */
	msleep(100);
	irq = irq_get_irq_data(pdev->irq);
	if (restore_shared_irq && irq && irqd_irq_disabled(irq))
		enable_irq(pdev->irq);
	schedule_delayed_work(&service_work, msecs_to_jiffies(250));
	return 0;
put_pages:
	while (i--)
		put_page(virt_to_page((void *)channels[i]));
	mutex_unlock(&guest->trial_lock);
put_owner:
	module_put(owner);
unlock:
	device_unlock(&pdev->dev);
put_device:
	pci_dev_put(pdev);
	return ret;
}

static void __exit stop(void)
{
	sysfs_remove_group(&pdev->dev.kobj, &group);
	if (irq_registered)
		free_irq(pdev->irq, pdev);
	cancel_delayed_work_sync(&service_work);
	for (unsigned int i = 0; i < 4; i++)
		put_page(virt_to_page((void *)channels[i]));
	module_put(owner);
	pci_dev_put(pdev);
}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Retained Guest session RPC and shared IRQ acknowledgment recovery");
