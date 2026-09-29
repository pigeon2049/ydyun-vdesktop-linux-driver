/* Diagnostic build only: report cached platform data and stop before heaps/MMU. */
#include "mtgpu/mtgpu_drv.h"
#include "mtgpu/mtgpu_device.h"
#include "pvr/services/pvrsrv.h"
#include "pvr/services/physheap_config.h"
#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/unaligned.h>
#include "mtgpu_guest_mmu_smoke.h"
#ifdef MTGPU_GUEST_MMU_CONTEXT_AUDIT
#include "mtgpu_guest_mmu_context_audit.h"
#endif

int mtgpu_guest_vz_platform_data_init(struct mtgpu_device *mtdev,
				     struct mtgpu_platform_data *pdata);
PVRSRV_ERROR mtgpu_guest_vpu_heap_alias_init(PVRSRV_DEVICE_NODE *node,
					    PVRSRV_DEVICE_CONFIG *config);

int mtgpu_guest_vz_platform_data_init(struct mtgpu_device *mtdev,
				     struct mtgpu_platform_data *pdata)
{
	const u8 *info;
	u32 version;
	int err;

	if (!mtdev || !pdata)
		return -EINVAL;
	err = mtgpu_platform_data_vz_init(mtdev, pdata);
	if (err || mtgpu_get_driver_mode() != MTGPU_DRIVER_MODE_GUEST)
		return err;
	pr_info("MT_HEAP_AUDIT platform pcie=%llx gpu=%llx size=%llx mmu_cpu=%llx mmu_size=%llx mmu_card=%llx fw_card=%llx\n",
		(unsigned long long)pdata->pcie_memory_base,
		(unsigned long long)pdata->gpu_memory_base,
		(unsigned long long)pdata->gpu_memory_size,
		(unsigned long long)pdata->vz_data.mmu_heap_base,
		(unsigned long long)pdata->vz_data.mmu_heap_size,
		(unsigned long long)pdata->vz_data.mmu_heap_card_base,
		(unsigned long long)pdata->vz_data.fw_heap_card_base);
	info = (const u8 *)mtdev->vgpu_info;
	if (!info)
		return err;
	version = get_unaligned_le32(info + 4);
	pr_info("MT_HEAP_AUDIT info magic=%x version=%u\n",
		get_unaligned_le32(info), version);
	/* The build requires the existing audited 4 KiB allocation patch. */
	if (get_unaligned_le32(info) == 0xaa557491 &&
	    (version == 1 || version == 2))
		print_hex_dump(KERN_INFO, "MT_HEAP_INFO ", DUMP_PREFIX_OFFSET,
			16, 1, info, 0x1000, false);
	return err;
}

PVRSRV_ERROR mtgpu_guest_vpu_heap_alias_init(PVRSRV_DEVICE_NODE *node,
					    PVRSRV_DEVICE_CONFIG *config)
{
	u32 i;
	if (mtgpu_get_driver_mode() != MTGPU_DRIVER_MODE_GUEST)
		return PVRSRVPhysMemHeapsInit(node, config);
#ifdef MTGPU_GUEST_MMU_CONTEXT_AUDIT
	if ((guest_mmu_context_smoke && !guest_mmu_context_attempted) ||
	    (guest_fw_context_smoke && !guest_fw_context_attempted))
		return mtgpu_guest_rebuild_heaps(node, config);
#endif
	if (guest_mmu_smoke && !guest_mmu_smoke_done) {
		guest_mmu_smoke_done = true;
		mtgpu_guest_mmu_smoke(node, config);
	}
	if (config && config->pasPhysHeaps && config->ui32PhysHeapCount <= 16) {
		for (i = 0; i < config->ui32PhysHeapCount; i++) {
			const PHYS_HEAP_CONFIG *heap = &config->pasPhysHeaps[i];
			pr_info("MT_HEAP_AUDIT heap=%u type=%u cpu=%llx card=%llx size=%llx usage=%x\n",
				i, heap->eType,
				(unsigned long long)heap->sStartAddr.uiAddr,
				(unsigned long long)heap->sCardBase.uiAddr,
				(unsigned long long)heap->uiSize, heap->ui32UsageFlags);
		}
		/* Arithmetic only: exercise the real config -> sysdata -> pdata chain. */
		for (i = 0; i < 4; i++) {
			static const u64 addresses[] = { 0, 0x200000, 0x3f000000, 0x43000000 };
			pr_info("MT_HEAP_TRANSLATE relative=%llx host=%llx\n",
				(unsigned long long)addresses[i],
				(unsigned long long)GuestDevicePAddrToHostDevicePAddr(config, addresses[i]));
		}
	}
	pr_info("MT_HEAP_AUDIT stopping before PVRSRVPhysMemHeapsInit/MMU allocation\n");
	return PVRSRV_ERROR_INIT_FAILURE;
}
