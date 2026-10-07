// SPDX-License-Identifier: GPL-2.0
/* Finish only the recorded, already-consumed 2026-09-29 disconnect.
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
MODULE_PARM_DESC(finish, "After exact snapshot validation, acknowledge one retired event and publish Guest OFF");

#define FW_OFFSET 0x3f000000ULL
#define FW_SIZE 0x800000U
#define QUEUE_OFFSET 0x6ddd0U
#define CURSOR_OFFSET (QUEUE_OFFSET + 0x2e00U)
#define EVENT_TAIL (CURSOR_OFFSET + 40)

static int __init retired_disconnect_init(void)
{
	static const u8 expected[SHA256_DIGEST_SIZE] = {
		0x46,0xc2,0x84,0x3f,0x5a,0x3b,0x51,0x91,0x04,0xed,0xc2,0x71,0xb5,0x64,0xb2,0xd2,
		0x1f,0xde,0xc4,0xf4,0xc3,0x25,0x4f,0x50,0xfc,0x5f,0x63,0x21,0x3f,0x91,0xa4,0xdf,
	};
	struct pci_dev *pdev = mt_guest_find_s3000();
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
	ret = pci_request_selected_regions(pdev, BIT(0) | BIT(1) | BIT(2), "mt_retired_disconnect");
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
	    readq(custom + 0x30) != 0x769fef000ULL || readq(custom + 0x38) != FW_SIZE)
		goto out;
	memcpy_fromio(snapshot, fw, FW_SIZE);
	sha256(snapshot, FW_SIZE, digest);
	ret = -EBADMSG;
	if (memcmp(digest, expected, sizeof(expected)))
		goto out;
	/* Snapshot's sole pending event is the zero-valued, null-context
	 * completion following CONNECT/DISCONNECT (opcodes 0x46/0x47).
	 * Both commands were consumed, all other rings empty. Confirm live. */
	ret = -EPROTO;
	for (dm = 0; dm < 6; dm++) {
		for (ring = 0; ring < 3; ring++) {
			cursor = CURSOR_OFFSET + dm * 0x2e30 + ring * 16;
			head = readl(fw + cursor);
			tail = readl(fw + cursor + 8);
			if (dm == 0 && ring == 2) {
				if (head != 2 || tail != 1)
					goto out;
			} else if (head >= 64 || head != tail) {
				goto out;
			}
		}
	}
	if (readl(regs + 0x890) != 2 || readl(regs + 0x898) != 1 || readl(fw + 4))
		goto out;
	pr_info("MT_RETIRED_DISCONNECT eligible=1 exact_snapshot=1 command_consumed=1 pending_null_event=1 finish=%u\n", finish);
	ret = 0;
	if (!finish)
		goto out;
	/* Same consumer acknowledgement as 14000be34; same Guest OFF write
	 * as 140016b4c after 14000bdc4 confirms all three DM0 rings empty. */
	mb();
	writel(2, fw + EVENT_TAIL);
	mb();
	ret = -EIO;
	if (readl(fw + EVENT_TAIL) != 2 || readl(fw + CURSOR_OFFSET + 32) != 2 ||
	    readl(fw + CURSOR_OFFSET) != readl(fw + CURSOR_OFFSET + 8) ||
	    readl(fw + CURSOR_OFFSET + 16) != readl(fw + CURSOR_OFFSET + 24) ||
	    readl(regs + 0x890) != 2 || readl(regs + 0x898) != 1 || readl(fw + 4))
		goto out;
	writel(0, regs + 0x890);
	mb();
	pr_info("MT_RETIRED_DISCONNECT guest=%u firmware=%u started=%u event_head=%u event_tail=%u\n",
		readl(regs + 0x890), readl(regs + 0x898), readl(fw + 4),
		readl(fw + CURSOR_OFFSET + 32), readl(fw + EVENT_TAIL));
	ret = readl(regs + 0x890) == 0 && readl(regs + 0x898) == 1 && !readl(fw + 4) ? 0 : -EIO;
out:
	pr_info("MT_RETIRED_DISCONNECT result=%d\n", ret);
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
static void __exit retired_disconnect_exit(void) { }
module_init(retired_disconnect_init);
module_exit(retired_disconnect_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Exact retained disconnect completion, no firmware reset");
