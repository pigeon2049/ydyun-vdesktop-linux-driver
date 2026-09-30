// SPDX-License-Identifier: GPL-2.0
/* Cleanly finalize cold boot idle state when guest driver left state 2 after failed probe.
 * Checks that all DM rings have head == tail, started == 0, FW == 1 (READY).
 * With finish=1, safely writes Guest OFF (0) to regs+0x890.
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/vmalloc.h>

static bool finish;
module_param(finish, bool, 0400);
MODULE_PARM_DESC(finish, "Publish Guest OFF after verifying all queues are idle");

#define FW_SIZE 0x800000U
#define QUEUE_OFFSET 0x6ddd0U
#define CURSOR_OFFSET (QUEUE_OFFSET + 0x2e00U)

static int __init cold_disconnect_init(void)
{
	struct pci_dev *pdev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(14, 0));
	void __iomem *regs = NULL, *custom = NULL, *fw = NULL;
	u64 fw_offset, fw_pa;
	u32 dm, ring, cursor, head, tail;
	u16 command;
	int ret = -ENODEV;
	bool claimed = false;

	if (!pdev)
		return ret;
	device_lock(&pdev->dev);
	if (pdev->driver || pdev->vendor != 0x1ed5 || pdev->device != 0x0222 ||
	    pdev->subsystem_vendor != 0x1ed5 || pdev->subsystem_device != 0x1101 ||
	    pci_resource_start(pdev, 2) != 0x800000000ULL ||
	    pci_resource_len(pdev, 2) != 0x400000000ULL ||
	    pci_resource_len(pdev, 0) != 0x10000 || pci_resource_len(pdev, 1) != 0x10000)
		goto out;

	ret = -EBUSY;
	if (pci_read_config_word(pdev, PCI_COMMAND, &command) ||
	    !(command & PCI_COMMAND_MEMORY) || (command & PCI_COMMAND_MASTER))
		goto out;

	ret = pci_request_selected_regions(pdev, BIT(0) | BIT(1) | BIT(2), "mt_cold_disconnect");
	if (ret)
		goto out;
	claimed = true;

	regs = pci_iomap(pdev, 0, 4096);
	custom = pci_iomap(pdev, 1, 4096);
	if (!regs || !custom) {
		ret = -ENOMEM;
		goto out;
	}

	fw_pa = readq(custom + 0x30);
	if (fw_pa == 0x771fef000ULL)
		fw_offset = 0x3f000000ULL;
	else
		fw_offset = 0x200000ULL;
	pr_info("MT_COLD_DISCONNECT: fw_pa=%#llx fw_offset=%#llx\n", fw_pa, fw_offset);

	fw = ioremap(pci_resource_start(pdev, 2) + fw_offset, FW_SIZE);
	if (!fw) {
		ret = -ENOMEM;
		goto out;
	}

	ret = -EPROTO;
	if (readl(regs + 0x890) != 2 || readl(regs + 0x898) != 1 || readl(fw + 4) != 0 ||
	    readq(custom + 0x38) != FW_SIZE) {
		pr_err("MT_COLD_DISCONNECT: precheck failed: guest=%u fw=%u started=%u fw_bytes=%#llx\n",
		       readl(regs + 0x890), readl(regs + 0x898), readl(fw + 4), readq(custom + 0x38));
		goto out;
	}

	/* Verify every ring is idle (head == tail) */
	for (dm = 0; dm < 6; dm++) {
		for (ring = 0; ring < 3; ring++) {
			cursor = CURSOR_OFFSET + dm * 0x2e30 + ring * 16;
			head = readl(fw + cursor);
			tail = readl(fw + cursor + 8);
			if (head >= 64 || head != tail) {
				pr_err("MT_COLD_DISCONNECT: ring not idle: dm=%u ring=%u head=%u tail=%u\n",
				       dm, ring, head, tail);
				goto out;
			}
		}
	}

	pr_info("MT_COLD_DISCONNECT: all rings idle, started=0, FW=1, finish=%u\n", finish);
	ret = 0;
	if (!finish)
		goto out;

	mb();
	writel(0, regs + 0x890);
	mb();

	pr_info("MT_COLD_DISCONNECT: state after write: guest=%u firmware=%u started=%u\n",
		readl(regs + 0x890), readl(regs + 0x898), readl(fw + 4));

	ret = (readl(regs + 0x890) == 0 && readl(regs + 0x898) == 1 && !readl(fw + 4)) ? 0 : -EIO;

out:
	pr_info("MT_COLD_DISCONNECT: result=%d\n", ret);
	if (fw)
		iounmap(fw);
	if (custom)
		pci_iounmap(pdev, custom);
	if (regs)
		pci_iounmap(pdev, regs);
	if (claimed)
		pci_release_selected_regions(pdev, BIT(0) | BIT(1) | BIT(2));
	device_unlock(&pdev->dev);
	pci_dev_put(pdev);
	return ret;
}

static void __exit cold_disconnect_exit(void) { }
module_init(cold_disconnect_init);
module_exit(cold_disconnect_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Safe idle disconnect finalization for cold boot sessions");
