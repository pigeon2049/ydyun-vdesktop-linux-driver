/*
 * Runtime-derived Guest VPU heap alias for the 5c6c275 Linux PVR core.
 * The core asks for VPU_GROUP1 and VPU_GROUP2, while SysDevInit emits only
 * VPU_GROUP2 for this Guest ring. Keep the original GROUP2 descriptor and
 * add a GROUP1 alias for the same Guest ring range. The observed 5c6c275
 * config uses uiSize=UINT64_MAX-0x7fff for the range beginning at 0x8000.
 */

#include "mtgpu/mtgpu_drv.h"
#include "mtgpu/mtgpu_device.h"
#include "pvr/services/allocmem.h"
#include "pvr/services/osfunc.h"
#include "pvr/services/pvrsrv.h"
#include "pvr/services/physheap_config.h"
#include <linux/errno.h>
#include <linux/printk.h>

PVRSRV_ERROR mtgpu_guest_vpu_heap_alias_init(PVRSRV_DEVICE_NODE *psDeviceNode,
					     PVRSRV_DEVICE_CONFIG *psDevConfig);

PVRSRV_ERROR
mtgpu_guest_vpu_heap_alias_init(PVRSRV_DEVICE_NODE *psDeviceNode,
				PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	PHYS_HEAP_CONFIG *psOldHeaps;
	PHYS_HEAP_CONFIG *psNewHeaps;
	IMG_UINT32 ui32OldCount;
	IMG_UINT32 ui32Group1Index = IMG_UINT32_MAX;
	IMG_UINT32 ui32Group2Index = IMG_UINT32_MAX;
	IMG_UINT32 i;
	PVRSRV_ERROR eError;

	if (mtgpu_get_driver_mode() == MTGPU_DRIVER_MODE_GUEST &&
	    psDevConfig != NULL && psDevConfig->pasPhysHeaps != NULL &&
	    psDevConfig->ui32PhysHeapCount == 4U) {
		psOldHeaps = psDevConfig->pasPhysHeaps;
		ui32OldCount = psDevConfig->ui32PhysHeapCount;

		for (i = 0; i < ui32OldCount; i++) {
			if (psOldHeaps[i].ui32UsageFlags == PHYS_HEAP_USAGE_VPU_GROUP1)
				ui32Group1Index = i;
			if (psOldHeaps[i].ui32UsageFlags == PHYS_HEAP_USAGE_VPU_GROUP2)
				ui32Group2Index = i;
		}

		if (ui32Group1Index == IMG_UINT32_MAX &&
		    ui32Group2Index != IMG_UINT32_MAX &&
		    psOldHeaps[ui32Group2Index].uiSize == 0xffffffffffff8000ULL) {
			psNewHeaps = OSAllocMem((ui32OldCount + 1U) * sizeof(*psNewHeaps));
			if (psNewHeaps == NULL)
				return PVRSRV_ERROR_OUT_OF_MEMORY;

			OSMemCopy(psNewHeaps, psOldHeaps,
				  ui32OldCount * sizeof(*psNewHeaps));
			psNewHeaps[ui32OldCount] = psNewHeaps[ui32Group2Index];
			psNewHeaps[ui32OldCount].ui32UsageFlags = PHYS_HEAP_USAGE_VPU_GROUP1;

			psDevConfig->pasPhysHeaps = psNewHeaps;
			psDevConfig->ui32PhysHeapCount = ui32OldCount + 1U;
			/*
			 * The vendor core does not document who owns the original config
			 * array. Keep it alive: it may be static storage owned by SysDevInit.
			 * The replacement is tiny and is installed once per device init.
			 */
		}
	}

	eError = PVRSRVPhysMemHeapsInit(psDeviceNode, psDevConfig);
	return eError;
}

