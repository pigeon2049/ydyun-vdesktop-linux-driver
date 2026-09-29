/* SPDX-License-Identifier: GPL-2.0 */
/* Bounded diagnostic using the vendor LMA page allocator; never publishes a root. */
#include "pvr/services/physheap.h"
#include "pvr/services/osfunc.h"
#include "mt_memory_layout.h"
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/io.h>

static bool guest_mmu_smoke;
module_param(guest_mmu_smoke, bool, 0400);
MODULE_PARM_DESC(guest_mmu_smoke, "Allocate/map/write/restore two private LMA pages, then stop before GPU init");
static bool guest_mmu_smoke_done;

static int mtgpu_guest_mmu_heap_config(PVRSRV_DEVICE_CONFIG *config,
				      PHYS_HEAP_CONFIG *out, u64 *host_base)
{
	struct mtgpu_platform_data *pdata;
	struct pci_dev *pdev;
	struct device *pcidev, *osdev;
	struct mt_memory_layout layout;
	PHYS_HEAP_CONFIG *template = NULL;
	u64 cpu_base, bar_size;
	u32 i;
	int err;

	BUILD_BUG_ON(offsetof(PVRSRV_DEVICE_CONFIG, hSysData) != 0x78);
	if (!config || !config->hSysData || !config->pasPhysHeaps ||
	    config->ui32PhysHeapCount != 4)
		return -EINVAL;
	/* Same sysdata -> platform link as GuestLocalDevPAddrToCpuPAddr. */
	pdata = *(struct mtgpu_platform_data **)((u8 *)config->hSysData + 8);
	if (!pdata || !pdata->vz_data.vgpu_info)
		return -EINVAL;
	osdev = config->pvOSDevice;
	if (!osdev || !osdev->parent || !osdev->parent->parent)
		return -ENODEV;
	/* Vendor API returns struct device *, despite the PcieDevice name. */
	pcidev = OSGetPcieDeviceFromOSDevice(osdev);
	if (!pcidev || !dev_is_pci(pcidev))
		return -ENODEV;
	pdev = to_pci_dev(pcidev);
	if (pdev->vendor != 0x1ed5 || pdev->device != 0x0222)
		return -ENODEV;
	cpu_base = pci_resource_start(pdev, 2);
	bar_size = pci_resource_len(pdev, 2);
	if (!cpu_base || cpu_base != pdata->pcie_memory_base ||
	    cpu_base > U64_MAX - bar_size || pci_resource_len(pdev, 4))
		return -ERANGE;
	err = mt_memory_parse(pdata->vz_data.vgpu_info, 4096, bar_size, &layout);
	if (err)
		return err;
	for (i = 0; i < config->ui32PhysHeapCount; i++)
		if (config->pasPhysHeaps[i].ui32UsageFlags == PHYS_HEAP_USAGE_MMU_TABLE)
			template = &config->pasPhysHeaps[i];
	if (!template || template->eType != PHYS_HEAP_TYPE_LMA ||
	    !template->psMemFuncs || !template->hPrivData)
		return -EINVAL;
	*out = *template;
	out->sStartAddr.uiAddr = cpu_base + layout.pool[MT_POOL_NORMAL].bar_offset;
	out->sCardBase.uiAddr = layout.pool[MT_POOL_NORMAL].bar_offset;
	out->uiSize = layout.pool[MT_POOL_NORMAL].size;
	out->psSegmentInfo = NULL;
	out->pszPDumpMemspaceName = "MTGuestMMUSmoke";
	*host_base = layout.pool[MT_POOL_NORMAL].gpu_pa;
	return 0;
}

static void mtgpu_guest_mmu_smoke(PVRSRV_DEVICE_NODE *node,
				 PVRSRV_DEVICE_CONFIG *config)
{
	PHYS_HEAP_CONFIG heap_config;
	PHYS_HEAP *heap = NULL;
	PG_HANDLE handles[2] = {{0}};
	IMG_DEV_PHYADDR dev[2] = {{0}};
	IMG_CPU_PHYADDR cpu = {0};
	void *mapped[2] = {NULL, NULL};
	u8 *saved = NULL, *pattern = NULL, *readback = NULL;
	u64 host_base = 0, offset;
	unsigned int i, j, allocated = 0, mapped_count = 0, written = 0;
	int result;
	PVRSRV_ERROR error;
	bool restored = true;

	result = mtgpu_guest_mmu_heap_config(config, &heap_config, &host_base);
	if (result)
		goto finish;
	pr_info("MT_MMU_SMOKE heap cpu=%llx relative=%llx host=%llx size=%llx\n",
		(unsigned long long)heap_config.sStartAddr.uiAddr,
		(unsigned long long)heap_config.sCardBase.uiAddr,
		(unsigned long long)host_base, (unsigned long long)heap_config.uiSize);
	saved = kmalloc(8192, GFP_KERNEL);
	pattern = kmalloc(4096, GFP_KERNEL);
	readback = kmalloc(4096, GFP_KERNEL);
	result = -ENOMEM;
	if (!saved || !pattern || !readback)
		goto finish;
	error = PhysHeapCreateHeapFromConfig(node, &heap_config, &heap);
	pr_info("MT_MMU_SMOKE create=%u\n", error);
	if (error != PVRSRV_OK)
		goto finish;
	for (i = 0; i < 2; i++) {
		error = PhysHeapPagesAlloc(heap, 4096, &handles[i], &dev[i], 0);
		pr_info("MT_MMU_SMOKE alloc[%u]=%u relative=%llx\n", i, error,
			(unsigned long long)dev[i].uiAddr);
		if (error != PVRSRV_OK)
			goto finish;
		allocated++;
		result = -ERANGE;
		if (dev[i].uiAddr < heap_config.sCardBase.uiAddr || (dev[i].uiAddr & 4095))
			goto finish;
		offset = dev[i].uiAddr - heap_config.sCardBase.uiAddr;
		if (offset > heap_config.uiSize - 4096 || (i && dev[i].uiAddr == dev[0].uiAddr))
			goto finish;
		cpu.uiAddr = 0;
		PhysHeapDevPAddrToCpuPAddr(heap, 1, &cpu, &dev[i]);
		if (cpu.uiAddr != heap_config.sStartAddr.uiAddr + offset ||
		    GuestDevicePAddrToHostDevicePAddr(config, dev[i].uiAddr) != host_base + offset)
			goto finish;
		error = PhysHeapPagesMap(heap, &handles[i], 4096, &dev[i], &mapped[i]);
		pr_info("MT_MMU_SMOKE map[%u]=%u cpu=%llx\n", i, error,
			(unsigned long long)cpu.uiAddr);
		result = -EIO;
		if (error != PVRSRV_OK || !mapped[i])
			goto finish;
		mapped_count++;
		memcpy_fromio(saved + i * 4096, mapped[i], 4096);
	}
	for (i = 0; i < 2; i++) {
		for (j = 0; j < 4096; j++)
			pattern[j] = (u8)(j * 37 + i * 113 + 0x5a);
		memcpy_toio(mapped[i], pattern, 4096);
		written++;
		wmb();
		memcpy_fromio(readback, mapped[i], 4096);
		if (memcmp(pattern, readback, 4096))
			goto finish;
	}
	/* Verify both distinct patterns remain after both writes, catching aliases. */
	for (i = 0; i < 2; i++) {
		memcpy_fromio(readback, mapped[i], 4096);
		for (j = 0; j < 4096; j++)
			if (readback[j] != (u8)(j * 37 + i * 113 + 0x5a))
				goto finish;
	}
	result = 0;
finish:
	for (i = 0; i < written; i++) {
		memcpy_toio(mapped[i], saved + i * 4096, 4096);
		wmb();
		memcpy_fromio(readback, mapped[i], 4096);
		if (memcmp(readback, saved + i * 4096, 4096))
			restored = false;
	}
	for (i = 0; i < mapped_count; i++)
		PhysHeapPagesUnMap(heap, &handles[i], mapped[i]);
	for (i = 0; i < allocated; i++)
		PhysHeapPagesFree(heap, &handles[i]);
	if (heap)
		PhysHeapDestroy(heap);
	kfree(readback);
	kfree(pattern);
	kfree(saved);
	pr_info("MT_MMU_SMOKE result=%d allocated=%u mapped=%u written=%u restored=%u cleanup_complete=1\n",
		result, allocated, mapped_count, written, restored);
}
