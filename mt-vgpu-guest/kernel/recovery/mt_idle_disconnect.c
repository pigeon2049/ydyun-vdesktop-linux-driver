// SPDX-License-Identifier: GPL-2.0
/* Finish only the fully retired OSID-4 disconnect observed at r28 cold boot.
 * Never resets firmware, uploads an image, rings a doorbell or submits work.
 * The exact retained 8 MiB snapshot is mandatory; this is not a general reset.
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/vmalloc.h>
#include "../mt_guest_device.h"
#include <crypto/sha2.h>

static bool finish;
module_param(finish, bool, 0400);
MODULE_PARM_DESC(finish, "After exact snapshot validation, publish Guest OFF after all queues and the recorded disconnect are retired");

#define FW_OFFSET 0x200000ULL
#define FW_SIZE 0x800000U
#define QUEUE_OFFSET 0x6ddd0U
#define CURSOR_OFFSET (QUEUE_OFFSET + 0x2e00U)
#define EVENT_TAIL (CURSOR_OFFSET + 40)

static int __init idle_disconnect_init(void)
{
	static const u8 expected[SHA256_DIGEST_SIZE] = {
		0xd4,0x8a,0xb7,0x79,0x33,0x78,0x73,0x24,0x60,0x00,0x00,0x3b,0x1e,0x23,0x32,0xf3,0x16,0x1b,0xd5,0x34,0x6b,0xb8,0x17,0xd6,0x13,0x12,0x06,0x5d,0x38,0x0a,0x49,0x75
	};
	struct pci_dev *pdev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(14, 0));
	void __iomem *regs = NULL, *custom = NULL, *fw = NULL;
	u8 *snapshot = NULL, digest[SHA256_DIGEST_SIZE];
	u32 dm, ring, cursor, head, tail;
	u16 command;
	int ret = -ENODEV;
	bool claimed = false;
	if (!pdev)
		return ret;
	device_lock(&pdev->dev);
	if (pdev->driver || !mt_guest_match_s3000(pdev->vendor, pdev->device, pdev->subsystem_vendor, pdev->subsystem_device) ||
	    pci_resource_start(pdev, 2) != MT_GUEST_BAR2_BASE ||
	    pci_resource_len(pdev, 2) != MT_GUEST_BAR2_BYTES ||
	    pci_resource_len(pdev, 0) != 0x10000 || pci_resource_len(pdev, 1) != 0x10000)
		goto out;
	ret = -EBUSY;
	if (pci_read_config_word(pdev, PCI_COMMAND, &command) ||
	    !(command & PCI_COMMAND_MEMORY) || (command & PCI_COMMAND_MASTER))
		goto out;
	ret = pci_request_selected_regions(pdev, BIT(0) | BIT(1) | BIT(2), "mt_idle_disconnect");
	if (ret)
		goto out;
	claimed = true;
	regs = pci_iomap(pdev, 0, 4096);
	custom = pci_iomap(pdev, 1, 4096);
	fw = ioremap(pci_resource_start(pdev, 2) + FW_OFFSET, FW_SIZE);
	snapshot = vzalloc(FW_SIZE);
	ret = -ENOMEM;
	if (!regs || !custom || !fw || !snapshot)
		goto out;
	ret = -EPROTO;
	if (readl(regs + 0x890) != 2 || readl(regs + 0x898) != 1 || readl(fw + 4) ||
	    readq(custom + 0x30) != 0x605000000ULL || readq(custom + 0x38) != FW_SIZE)
		goto out;
	memcpy_fromio(snapshot, fw, FW_SIZE);
	sha256(snapshot, FW_SIZE, digest);
	ret = -EBADMSG;
	if (memcmp(digest, expected, sizeof(expected)))
		goto out;
	/* The actual published normal-segment window contains CONNECT then
	 * DISCONNECT, both consumed. Every event is already acknowledged. */
	ret = -EPROTO;
	if (readl(fw + QUEUE_OFFSET + 12) != 0x46 ||
	    readl(fw + QUEUE_OFFSET + 80 + 12) != 0x47)
		goto out;
	for (dm = 0; dm < 6; dm++) {
		for (ring = 0; ring < 3; ring++) {
			cursor = CURSOR_OFFSET + dm * 0x2e30 + ring * 16;
			head = readl(fw + cursor);
			tail = readl(fw + cursor + 8);
			if (head >= 64 || head != tail ||
			    (!dm && ring != 1 && head != 2))
				goto out;
		}
	}
	if (readl(regs + 0x890) != 2 || readl(regs + 0x898) != 1 || readl(fw + 4))
		goto out;
	pr_info("MT_IDLE_DISCONNECT eligible=1 exact_snapshot=1 command_consumed=1 all_events_retired=1 finish=%u\n", finish);
	ret = 0;
	if (!finish)
		goto out;
	/* 140016b4c final Guest OFF step: FW READY/started=0 and every
	 * command/event already retired. No event tail or firmware data writes. */
	mb();
	ret = -EIO;
	if (readl(fw + EVENT_TAIL) != 2 || readl(fw + CURSOR_OFFSET + 32) != 2 ||
	    readl(fw + CURSOR_OFFSET) != 2 || readl(fw + CURSOR_OFFSET + 8) != 2 ||
	    readl(regs + 0x890) != 2 || readl(regs + 0x898) != 1 || readl(fw + 4))
		goto out;
	writel(0, regs + 0x890);
	mb();
	pr_info("MT_IDLE_DISCONNECT guest=%u firmware=%u started=%u event_head=%u event_tail=%u\n",
		readl(regs + 0x890), readl(regs + 0x898), readl(fw + 4),
		readl(fw + CURSOR_OFFSET + 32), readl(fw + EVENT_TAIL));
	ret = readl(regs + 0x890) == 0 && readl(regs + 0x898) == 1 && !readl(fw + 4) ? 0 : -EIO;
out:
	pr_info("MT_IDLE_DISCONNECT result=%d\n", ret);
	vfree(snapshot);
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
static void __exit idle_disconnect_exit(void) { }
module_init(idle_disconnect_init);
module_exit(idle_disconnect_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Exact fully idle OSID-4 disconnect finalization, no firmware reset");
