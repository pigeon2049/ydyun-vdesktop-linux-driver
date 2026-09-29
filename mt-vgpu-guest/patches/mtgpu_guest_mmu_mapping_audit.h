/* SPDX-License-Identifier: GPL-2.0 */
/* Private, unpublished context: exercise vendor PMR and all three MMU levels. */
#include "pvr/services/pmr.h"

static bool guest_mmu_mapping_smoke;
module_param(guest_mmu_mapping_smoke, bool, 0400);
MODULE_PARM_DESC(guest_mmu_mapping_smoke, "With FW context smoke, map/unmap two GPU-local pages without submitting GPU work");

static int mtgpu_guest_check_table_link(PVRSRV_DEVICE_NODE *node,
	MMU_Levelx_INFO *parent, u32 index, MMU_Levelx_INFO **child)
{
	const MMU_PxE_CONFIG *cfg = parent->psConfig;
	u64 entry = 0, expected, decoded;
	if (!cfg || index >= parent->ui32NumOfEntries ||
	    !parent->sMemDesc.pvCpuVAddr || !parent->apsNextLevel[index] ||
	    (cfg->uiBytesPerEntry != 4 && cfg->uiBytesPerEntry != 8))
		return -EINVAL;
	*child = parent->apsNextLevel[index];
	memcpy_fromio(&entry, (u8 *)parent->sMemDesc.pvCpuVAddr +
		index * cfg->uiBytesPerEntry, cfg->uiBytesPerEntry);
	decoded = ((entry & cfg->uiAddrMask) >> cfg->uiAddrShift) << cfg->uiAddrLog2Align;
	expected = GuestDevicePAddrToHostDevicePAddr(node->psDevConfig,
		(*child)->sMemDesc.sDevPAddr.uiAddr);
	pr_info("MT_MMU_MAP level=%u index=%u entry=%llx decoded=%llx expected=%llx\n",
		parent->eMMULevel, index, entry, decoded, expected);
	return expected && decoded == expected && (entry & cfg->uiValidEnMask) ? 0 : -EIO;
}

static void mtgpu_guest_mmu_mapping_audit(PVRSRV_DEVICE_NODE *node, MMU_CONTEXT *ctx)
{
	IMG_DEV_VIRTADDR va = { .uiAddr = 0x100000000ULL };
	IMG_DEVMEM_SIZE_T actual = 8192;
	IMG_DEV_PHYADDR physical[2] = {{0}};
	IMG_BOOL valid[2] = {0};
	MMU_Levelx_INFO *pd, *pt;
	PMR *pmr = NULL;
	u32 table[2] = {0, 1}, i;
	bool allocated = false, locked = false, mapped = false;
	PVRSRV_ERROR error;
	PVRSRV_MEMALLOCFLAGS_T flags = PVRSRV_MEMALLOCFLAG_GPU_READABLE |
		PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE | PVRSRV_MEMALLOCFLAG_GPU_UNCACHED;
	int result = -EINVAL;
	if (!guest_mmu_mapping_smoke || !ctx || ctx->psDevAttrs->eTopLevel != MMU_LEVEL_3)
		return;
	error = MMU_Alloc(ctx, 8192, &actual, 0, 4096, &va, 12);
	pr_info("MT_MMU_MAP tables=%u va=%llx\n", error, (unsigned long long)va.uiAddr);
	if (error != PVRSRV_OK)
		goto out;
	allocated = true;
	result = mtgpu_guest_check_table_link(node, &ctx->sBaseLevelInfo, mt_mmu_pc_index(va.uiAddr), &pd);
	if (result)
		goto out;
	result = mtgpu_guest_check_table_link(node, pd, mt_mmu_pd_index(va.uiAddr), &pt);
	if (result)
		goto out;
	result = -EIO;
	error = PhysHeapCreatePMR(node->apsPhysHeap[PVRSRV_PHYS_HEAP_GPU_LOCAL], NULL,
		8192, 4096, 2, 2, table, 12, flags, "MTGuestMappingAudit",
		OSGetCurrentClientProcessIDKM(), &pmr, 0);
	pr_info("MT_MMU_MAP pmr=%u\n", error);
	if (error != PVRSRV_OK)
		goto out;
	error = PMRLockSysPhysAddresses(pmr);
	if (error != PVRSRV_OK)
		goto out;
	locked = true;
	error = PMR_DevPhysAddr(pmr, 12, 2, 0, physical, valid);
	if (error != PVRSRV_OK || !valid[0] || !valid[1])
		goto out;
	error = MMU_MapPages(ctx, flags, va, pmr, 0, 2, NULL, 12);
	pr_info("MT_MMU_MAP map=%u\n", error);
	if (error != PVRSRV_OK)
		goto out;
	mapped = true;
	for (i = 0; i < 2; i++) {
		u64 entry, host = GuestDevicePAddrToHostDevicePAddr(node->psDevConfig, physical[i].uiAddr);
		IMG_DEV_VIRTADDR page = { .uiAddr = va.uiAddr + i * 4096 };
		if (!pt->psConfig || pt->psConfig->uiBytesPerEntry != 8 || !pt->sMemDesc.pvCpuVAddr)
			goto out;
		memcpy_fromio(&entry, (u8 *)pt->sMemDesc.pvCpuVAddr + mt_mmu_pt_index(page.uiAddr) * 8, 8);
		pr_info("MT_MMU_MAP page=%u relative=%llx host=%llx pte=%llx valid=%u\n", i,
			(unsigned long long)physical[i].uiAddr, host, entry, MMU_IsVDevAddrValid(ctx, 12, page));
		if (!host || (entry & 0xfffffff000ULL) != host || !(entry & 1) ||
		    !MMU_IsVDevAddrValid(ctx, 12, page))
			goto out;
	}
	result = 0;
out:
	if (mapped) {
		/* Match DevmemIntUnmapPages: zero mapping flags requests invalid PTEs.
		 * Nonzero flags select the sparse replacement/dummy-page semantics. */
		MMU_UnmapPages(ctx, 0, va, 2, NULL, 12, 0);
		for (i = 0; i < 2; i++) {
			IMG_DEV_VIRTADDR page = { .uiAddr = va.uiAddr + i * 4096 };
			IMG_BOOL still_valid = MMU_IsVDevAddrValid(ctx, 12, page);
			pr_info("MT_MMU_MAP unmap_page=%u valid=%u\n", i, still_valid);
			if (still_valid)
				result = -EIO;
		}
	}
	if (locked) {
		error = PMRUnlockSysPhysAddresses(pmr);
		if (error != PVRSRV_OK)
			result = -EIO;
	}
	if (pmr) {
		error = PMRUnrefPMR(pmr);
		if (error != PVRSRV_OK)
			result = -EIO;
	}
	if (allocated)
		MMU_Free(ctx, va, 8192, 12);
	pr_info("MT_MMU_MAP result=%d mapped=%u cleanup_complete=1 submission=0\n", result, mapped);
}
