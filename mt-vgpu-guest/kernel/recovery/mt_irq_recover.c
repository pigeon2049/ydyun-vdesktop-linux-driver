// SPDX-License-Identifier: GPL-2.0
/* One-shot recovery of the shared QXL/USB line after the first Guest trial.
 * Only restore IRQ10 after confirming MTT's INTx is already masked.
 * This neither unbinds the active experiment nor touches its retained pages.
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

static bool enable;
module_param(enable, bool, 0400);

static int __init recover(void)
{
	struct pci_dev *mtt, *qxl;
	struct irq_data *data;
	u16 command;
	int ret = -ENODEV;
	if (!enable)
		return -EPERM;
	mtt = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(14, 0));
	qxl = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(2, 0));
	if (!mtt || !qxl || mtt->vendor != 0x1ed5 || mtt->device != 0x0222 ||
	    mtt->irq != 10 || qxl->irq != 10 || !qxl->driver ||
	    strcmp(qxl->driver->name, "qxl") || !mtt->driver ||
	    strcmp(mtt->driver->name, "mt_guest_probe"))
		goto out;
	if (pci_read_config_word(mtt, PCI_COMMAND, &command) || !(command & PCI_COMMAND_INTX_DISABLE)) {
		ret = -EBUSY;
		goto out;
	}
	data = irq_get_irq_data(10);
	if (!data)
		goto out;
	if (irqd_irq_disabled(data)) {
		pr_info("mt_irq_recover: MTT INTx masked; re-enabling shared IRQ10\n");
		enable_irq(10);
	} else {
		pr_info("mt_irq_recover: shared IRQ10 is already enabled\n");
	}
	ret = irqd_irq_disabled(data) ? -EIO : 0;
	pr_info("mt_irq_recover: shared IRQ10 disabled=%u result=%d\n", irqd_irq_disabled(data), ret);
out:
	pci_dev_put(qxl);
	pci_dev_put(mtt);
	return ret;
}
static void __exit done(void) { }
module_init(recover);
module_exit(done);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("One-shot guarded shared IRQ10 recovery");
