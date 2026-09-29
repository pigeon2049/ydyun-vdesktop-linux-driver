// SPDX-License-Identifier: GPL-2.0
/* One synchronous Windows-v2 information query while Guest OFF. In contrast
 * to connection bring-up this does not require FW READY, register RPC pages,
 * publish firmware/root/aperture addresses or reset anything. The optional
 * capture_retained mode only reads the guarded r25 BAR2 firmware window.
 * Only the information-request GPA is written to BAR1+0xc8. The synchronous
 * copy contract is the same as mt_read_device_info / reference 140026b30.
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>

static bool enable;
module_param(enable, bool, 0400);
static bool capture_retained;
module_param(capture_retained, bool, 0400);
/* Observation of the r28 OSID-4 READY session. No state writes/connection. */
static bool inspect_ready;
module_param(inspect_ready, bool, 0400);
static void *retained;
static void *published;
static unsigned long long bar2_gpa, local_mmu_gpa, firmware_pa, firmware_bytes;
module_param(bar2_gpa, ullong, 0444);
module_param(local_mmu_gpa, ullong, 0444);
module_param(firmware_pa, ullong, 0444);
module_param(firmware_bytes, ullong, 0444);
static struct pci_dev *device;
static unsigned long info;
static u32 before_fw, after_fw, after_guest;
module_param(before_fw, uint, 0444);
module_param(after_fw, uint, 0444);
module_param(after_guest, uint, 0444);
static ssize_t info_read(struct file *file, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	return memory_read_from_buffer(buf, count, &off, (void *)info, PAGE_SIZE);
}
static BIN_ATTR_ADMIN_RO(info, PAGE_SIZE);
static ssize_t retained_read(struct file *file, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	if (!retained)
		return -ENODATA;
	return memory_read_from_buffer(buf, count, &off, retained, SZ_8M);
}
static BIN_ATTR_ADMIN_RO(retained, SZ_8M);
static ssize_t published_read(struct file *file, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	if (!published)
		return -ENODATA;
	return memory_read_from_buffer(buf, count, &off, published, SZ_8M);
}
static BIN_ATTR_ADMIN_RO(published, SZ_8M);
static struct bin_attribute *bins[] = { &bin_attr_info, &bin_attr_retained, &bin_attr_published, NULL };
static const struct attribute_group group = { .name = "mt_offline_info", .bin_attrs = bins };

/* Deliberately scoped to the observed r25 OSID-6 layout. A changed page or
 * publication is rejected; this never follows a retained Guest RAM address.
 * The only mapped backing is the BAR2 VRAM window proved by the fresh page.
 */
static int snapshot_retained(void __iomem *regs, void __iomem *custom)
{
	static const u64 expected[6][3] = {
		{ 0x60f000000ULL, 0x5000000, 1 },
		{ 0x1ae000000ULL, 0x39e00000, 3 },
		{ 0x769fef000ULL, 0x4000000, 4 },
		{ 0, 0xc800000, 8 }, { 0, 0x5000000, 0x10 },
		{ 0x43000000, 0x200000, 0x20 },
	};
	static const u64 ready_expected[6][3] = {
		{ 0x605000000ULL, 0x5000000, 1 },
		{ 0x13a000000ULL, 0x39e00000, 3 },
		{ 0x771fef000ULL, 0x4000000, 4 },
		{ 0, 0xc800000, 8 }, { 0, 0x5000000, 0x10 },
		{ 0x43000000, 0x200000, 0x20 },
	};
	const u64 (*segments)[3] = inspect_ready ? ready_expected : expected;
	u32 guest_state = inspect_ready ? 2 : 0;
	u32 fw_state = inspect_ready ? 1 : 0;
	void __iomem *window;
	u64 cursor;
	u32 i, j;
	int ret;
	if (le32_to_cpup((__le32 *)info) != 0xaa557491 ||
	    le32_to_cpup((__le32 *)(info + 4)) != 2 ||
	    le32_to_cpup((__le32 *)(info + 8)) != (inspect_ready ? 4 : 6) ||
	    le64_to_cpup((__le64 *)(info + 0x10)) != 0x3d1 ||
	    le64_to_cpup((__le64 *)(info + 0x20)) != 0x43000000 ||
	    le32_to_cpup((__le32 *)(info + 0xc50)) != 6 ||
	    le32_to_cpup((__le32 *)(info + 0xc98)) != 0x200000)
		return -EPROTO;
	for (i = 0; i < 6; i++)
		for (j = 0; j < 3; j++)
			if (le64_to_cpup((__le64 *)(info + 0x28 + i * 24 + j * 8)) != segments[i][j])
				return -EPROTO;
	cursor = 0x200000 + segments[0][1] + segments[1][1];
	bar2_gpa = readq(custom + 0x20);
	local_mmu_gpa = readq(custom + 0x28);
	firmware_pa = readq(custom + 0x30);
	firmware_bytes = readq(custom + 0x38);
	pr_info("mt_offline_info: publication BAR2=%#llx MMU=%#llx FW=%#llx bytes=%#llx\n",
		bar2_gpa, local_mmu_gpa, firmware_pa, firmware_bytes);
	if (readl(regs + 0x890) != guest_state || readl(regs + 0x898) != fw_state ||
	    !(pci_resource_flags(device, 2) & IORESOURCE_MEM) ||
	    pci_resource_len(device, 2) < cursor + SZ_8M ||
	    bar2_gpa != pci_resource_start(device, 2) ||
	    local_mmu_gpa || firmware_pa != segments[inspect_ready ? 0 : 2][0] ||
	    firmware_bytes != SZ_8M)
		return -EBUSY;
	ret = pci_request_region(device, 2, "mt_offline_retained_read");
	if (ret)
		return ret;
	retained = vmalloc(SZ_8M);
	window = pci_iomap_range(device, 2, cursor, SZ_8M);
	ret = -ENOMEM;
	if (retained && window) {
		memcpy_fromio(retained, window, SZ_8M);
		ret = (readl(regs + 0x890) != guest_state ||
		       readl(regs + 0x898) != fw_state) ? -EAGAIN : 0;
	}
	if (window)
		pci_iounmap(device, window);
	/* r28 publication points to the normal segment start, not the FW slot.
	 * Capture both separately; neither address is executed or rewritten. */
	if (!ret && inspect_ready) {
		published = vmalloc(SZ_8M);
		window = pci_iomap_range(device, 2, 0x200000, SZ_8M);
		ret = -ENOMEM;
		if (published && window) {
			memcpy_fromio(published, window, SZ_8M);
			ret = (readl(regs + 0x890) != guest_state ||
			       readl(regs + 0x898) != fw_state) ? -EAGAIN : 0;
		}
		if (window)
			pci_iounmap(device, window);
	}
	pci_release_region(device, 2);
	return ret;
}

static int __init start(void)
{
	void __iomem *regs = NULL, *custom = NULL;
	u16 command;
	bool claimed = false;
	int ret = -ENODEV;
	if (!enable)
		return -EPERM;
	device = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(14, 0));
	if (!device)
		return -ENODEV;
	device_lock(&device->dev);
	if (device->driver || device->vendor != 0x1ed5 || device->device != 0x0222 ||
	    device->subsystem_vendor != 0x1ed5 || device->subsystem_device != 0x1101 ||
	    pci_resource_len(device, 0) != SZ_64K || pci_resource_len(device, 1) != SZ_64K)
		goto out;
	pci_read_config_word(device, PCI_COMMAND, &command);
	if (!(command & PCI_COMMAND_MEMORY) || (command & PCI_COMMAND_MASTER))
		goto out;
	ret = pci_request_selected_regions(device, BIT(0) | BIT(1), "mt_offline_info");
	if (ret)
		goto out;
	claimed = true;
	regs = pci_iomap(device, 0, PAGE_SIZE);
	custom = pci_iomap(device, 1, PAGE_SIZE);
	info = get_zeroed_page(GFP_KERNEL);
	ret = -ENOMEM;
	if (!regs || !custom || !info)
		goto out;
	ret = -EBUSY;
	before_fw = readl(regs + 0x898);
	if (inspect_ready ? (readl(regs + 0x890) != 2 || before_fw != 1) :
	    (readl(regs + 0x890) || before_fw > 1))
		goto out;
	*(__le64 *)(info + 0xc48) = cpu_to_le64(3);
	wmb();
	writeq(virt_to_phys((void *)info), custom + 0xc8);
	mb();
	after_fw = readl(regs + 0x898);
	after_guest = readl(regs + 0x890);
	pr_info("mt_offline_info: magic=%08x version=%u osid=%u before_fw=%u after_fw=%u guest=%u\n",
		le32_to_cpup((__le32 *)info), le32_to_cpup((__le32 *)(info + 4)),
		le32_to_cpup((__le32 *)(info + 8)), before_fw, after_fw, after_guest);
	if (capture_retained) {
		ret = snapshot_retained(regs, custom);
		if (ret)
			goto out;
	}
	ret = sysfs_create_group(&device->dev.kobj, &group);
out:
	if (custom)
		pci_iounmap(device, custom);
	if (regs)
		pci_iounmap(device, regs);
	if (claimed)
		pci_release_selected_regions(device, BIT(0) | BIT(1));
	device_unlock(&device->dev);
	if (ret) {
		vfree(published);
		vfree(retained);
		if (info)
			free_page(info);
		pci_dev_put(device);
	}
	return ret;
}
static void __exit stop(void)
{
	sysfs_remove_group(&device->dev.kobj, &group);
	vfree(published);
	vfree(retained);
	free_page(info);
	pci_dev_put(device);
}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("One synchronous vGPU information query without firmware bring-up");
