/* SPDX-License-Identifier: GPL-2.0 */
#include "mt_pvr_heap_layout.h"
#include "pvr/services/mmu_common.h"
#include "pvr/services/rgxdevice.h"
#include "mtgpu_guest_mmu_mapping_audit.h"

static bool guest_mmu_context_smoke;
module_param(guest_mmu_context_smoke, bool, 0400);
MODULE_PARM_DESC(guest_mmu_context_smoke, "Rebuild V2 heaps, create/destroy one MMU context and stop before firmware publication");
static bool guest_mmu_context_attempted;
static bool guest_fw_context_smoke;
module_param(guest_fw_context_smoke, bool, 0400);
MODULE_PARM_DESC(guest_fw_context_smoke, "Create/destroy the Guest FW virtual heaps and stop before RGXWinFWInit");
static bool guest_fw_context_attempted, guest_fw_context_active;

static PVRSRV_ERROR mtgpu_guest_rebuild_heaps(PVRSRV_DEVICE_NODE *node,
					     PVRSRV_DEVICE_CONFIG *config)
{
	PHYS_HEAP_CONFIG mmu, replacement[MT_PVR_COUNT];
	struct mt_pvr_heap_layout plan;
	struct mtgpu_platform_data *pdata;
	struct pci_dev *pdev;
	u64 unused_host;
	u32 i;
	int err;
	static const u32 usage[MT_PVR_COUNT] = {
		PHYS_HEAP_USAGE_FW_MAIN, PHYS_HEAP_USAGE_MMU_TABLE,
		PHYS_HEAP_USAGE_VPU_GROUP1 | PHYS_HEAP_USAGE_VPU_GROUP2,
		PHYS_HEAP_USAGE_GPU_LOCAL,
	};
	err = mtgpu_guest_mmu_heap_config(config, &mmu, &unused_host);
	if (err)
		return PVRSRV_ERROR_INIT_FAILURE;
	pdata = *(struct mtgpu_platform_data **)((u8 *)config->hSysData + 8);
	pdev = to_pci_dev((struct device *)OSGetPcieDeviceFromOSDevice(config->pvOSDevice));
	err = mt_pvr_heap_parse(pdata->vz_data.vgpu_info, 4096, pdata->pcie_memory_base,
				pci_resource_len(pdev, 2), &plan);
	if (err)
		return PVRSRV_ERROR_INIT_FAILURE;
	for (i = 0; i < MT_PVR_COUNT; i++) {
		replacement[i] = mmu;
		replacement[i].sStartAddr.uiAddr = plan.range[i].cpu;
		replacement[i].sCardBase.uiAddr = plan.range[i].device;
		replacement[i].uiSize = plan.range[i].size;
		replacement[i].ui32UsageFlags = usage[i];
		pr_info("MT_HEAP_REBUILD index=%u cpu=%llx relative=%llx size=%llx usage=%x\n",
			i, plan.range[i].cpu, plan.range[i].device, plan.range[i].size, usage[i]);
	}
	/* Reuse SysDevInit's allocation and lifetime; no duplicated heap arrays. */
	memcpy(config->pasPhysHeaps, replacement, sizeof(replacement));
	pdata->gpu_memory_base = plan.range[MT_PVR_GPU].cpu;
	pdata->gpu_memory_size = plan.range[MT_PVR_GPU].size;
	pdata->vz_data.fw_heap_card_base = plan.range[MT_PVR_FW].device;
	return PVRSRVPhysMemHeapsInit(node, config);
}

PVRSRV_ERROR mtgpu_guest_mmu_context_audit(struct _CONNECTION_DATA_ *connection,
	PVRSRV_DEVICE_NODE *node, MMU_CONTEXT **out, MMU_DEVICEATTRIBS *attrs);
PVRSRV_ERROR mtgpu_guest_mmu_context_audit(struct _CONNECTION_DATA_ *connection,
	PVRSRV_DEVICE_NODE *node, MMU_CONTEXT **out, MMU_DEVICEATTRIBS *attrs)
{
	MMU_CONTEXT *context = NULL;
	IMG_DEV_PHYADDR root = {0};
	PVRSRV_ERROR error, root_error;
	if (guest_fw_context_active && !connection &&
	    mtgpu_get_driver_mode() == MTGPU_DRIVER_MODE_GUEST)
		return MMU_ContextCreate(connection, node, out, attrs);
	if (!out || !guest_mmu_context_smoke || guest_mmu_context_attempted ||
	    mtgpu_get_driver_mode() != MTGPU_DRIVER_MODE_GUEST)
		return PVRSRV_ERROR_INIT_FAILURE;
	guest_mmu_context_attempted = true;
	*out = NULL;
	error = MMU_ContextCreate(connection, node, &context, attrs);
	pr_info("MT_MMU_CONTEXT create=%u\n", error);
	if (error == PVRSRV_OK && context) {
		root_error = MMU_AcquireBaseAddr(context, &root);
		pr_info("MT_MMU_CONTEXT acquire=%u root_host=%llx\n", root_error,
			(unsigned long long)root.uiAddr);
		if (root_error == PVRSRV_OK)
			MMU_ReleaseBaseAddr(context);
		MMU_ContextDestroy(context);
		pr_info("MT_MMU_CONTEXT destroyed=1 publication_blocked=1\n");
	}
	/* DevmemIntCtxCreate's error path only frees its container. It cannot
	 * register/publish this destroyed context or call its firmware callback. */
	return PVRSRV_ERROR_INIT_FAILURE;
}

PVRSRV_ERROR RGXInitCreateFWKernelMemoryContext(PVRSRV_DEVICE_NODE *node);
void RGXDeInitDestroyFWKernelMemoryContext(PVRSRV_DEVICE_NODE *node);
PVRSRV_ERROR mtgpu_guest_fw_context_audit(PVRSRV_DEVICE_NODE *node);
PVRSRV_ERROR mtgpu_guest_fw_context_audit(PVRSRV_DEVICE_NODE *node)
{
	PVRSRV_RGXDEV_INFO *info;
	IMG_DEV_PHYADDR root = {0};
	PVRSRV_ERROR error, root_error;
	BUILD_BUG_ON(offsetof(PVRSRV_RGXDEV_INFO, psKernelDevmemCtx) != 0x128);
	BUILD_BUG_ON(offsetof(PVRSRV_RGXDEV_INFO, psKernelMMUCtx) != 0x140);
	if (!guest_fw_context_smoke) /* Existing MMU-only stop remains effective. */
		return RGXInitCreateFWKernelMemoryContext(node);
	if (!node || !node->pvDevice || guest_fw_context_attempted ||
	    mtgpu_get_driver_mode() != MTGPU_DRIVER_MODE_GUEST)
		return PVRSRV_ERROR_INIT_FAILURE;
	guest_fw_context_attempted = true;
	info = node->pvDevice;
	/* A first kernel context only records its pointer in RGXRegisterMemoryContext.
	 * The later process-context path writes shared firmware memory and is excluded. */
	if (info->psKernelDevmemCtx || info->psKernelMMUCtx)
		return PVRSRV_ERROR_INIT_FAILURE;
	guest_fw_context_active = true;
	error = RGXInitCreateFWKernelMemoryContext(node);
	guest_fw_context_active = false;
	pr_info("MT_FW_CONTEXT create=%u main=%u config=%u mmu=%u\n", error,
		!!info->psFirmwareMainHeap, !!info->psFirmwareConfigHeap, !!info->psKernelMMUCtx);
	if (error == PVRSRV_OK) {
		if (info->psKernelMMUCtx) {
			root_error = MMU_AcquireBaseAddr(info->psKernelMMUCtx, &root);
			pr_info("MT_FW_CONTEXT acquire=%u root_host=%llx\n", root_error,
				(unsigned long long)root.uiAddr);
			if (root_error == PVRSRV_OK)
				MMU_ReleaseBaseAddr(info->psKernelMMUCtx);
			mtgpu_guest_mmu_mapping_audit(node, info->psKernelMMUCtx);
		}
		RGXDeInitDestroyFWKernelMemoryContext(node);
		/* Vendor destroy does not clear these fields; prevent a second cleanup. */
		info->psKernelDevmemCtx = NULL;
		info->psKernelMMUCtx = NULL;
		info->psFirmwareMainHeap = NULL;
		info->psFirmwareConfigHeap = NULL;
		pr_info("MT_FW_CONTEXT destroyed=1 firmware_init_blocked=1\n");
	}
	return PVRSRV_ERROR_INIT_FAILURE;
}
