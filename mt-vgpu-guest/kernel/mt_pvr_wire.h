/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_PVR_WIRE_H
#define MT_PVR_WIRE_H

/* On-the-wire layouts the legacy MUSA user-mode driver speaks, for the 19
 * commands Stage B phase 1 implements.
 *
 * Source of truth: the 5.2.0 KMD generated headers (kept in
 * reference/kmd-5.2.0-server-generated/, the generation this UMD was built
 * against) cross-checked against the sizes the driver actually puts on the
 * wire, captured in reports/stage-b-bridge-requirements.json. Every
 * _Static_assert below cites the command it belongs to;
 * tests/test_pvr_wire_sizes.py diffs these sizes against that JSON, so a
 * header refresh that moves a field fails the build instead of corrupting
 * user memory.
 *
 * All structures are packed: the wire is not aligned, and the driver reads
 * these out of its own heap buffers. Use get_unaligned/put_user when the
 * buffer lives in user space.
 *
 * Three commands carry MORE bytes than the 5.2 header struct declares
 * (0x6:0x9 in/out, 0x6:0x13 in). Those tails are modelled as reserved so the
 * kernel accepts the driver's sizes instead of dropping what it set.
 */

/* The wire is unaligned and the driver reads these out of its own heap
 * buffers. __attribute__ rather than the kernel __packed macro so the same
 * header compiles in the userspace RAM harness.
 *
 * Integer types follow kernel/mt_mmu.h: pull them from the kernel when built
 * as a module, define them here when the header is compiled by a user-space
 * test, so nothing outside the kernel tree has to supply a stub.
 */
#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <assert.h>	/* static_assert */
#include <stdint.h>
#include <string.h>
#include <errno.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#endif

#define MT_PVR_PACKED __attribute__((packed))

typedef u32 mt_handle;
typedef u64 mt_gpuvaddr;

/* 0xc0206440 dispatch packet. */
struct MT_PVR_PACKED mt_pvr_cmd {
	u32 bridge_id;
	u32 function_id;
	u64 in_ptr;
	u64 out_ptr;
	u32 in_size;
	u32 out_size;
};

/* 0x1:0x0 SRVCORE:Connect -- 16-byte IN, 17-byte OUT. */
struct MT_PVR_PACKED mt_pvr_connect_in {
	u32 client_build_options;
	u32 client_ddk_build;
	u32 client_ddk_version;
	u32 flags;
};

struct MT_PVR_PACKED mt_pvr_connect_out {
	u64 packed_bvnc;
	u32 error;
	u32 capability_flags;
	u8 kernel_arch;
};

/* 0x1:0x2 AcquireGlobalEventObject, 0x1:0xf AcquireInfoPage -- no IN, 12-byte OUT. */
struct MT_PVR_PACKED mt_pvr_handle_out {
	u64 handle;
	u32 error;
};

/* 0x1:0x4 EventObjectOpen -- 8-byte IN, 12-byte OUT. */
struct MT_PVR_PACKED mt_pvr_event_open_in {
	u64 event_object;
};

struct MT_PVR_PACKED mt_pvr_event_open_out {
	u64 os_event;
	u32 error;
};

/* 0x1:0xc BridgeGetMultiCoreInfo. Request/reply layouts follow the 5.2 UMD
 * wrapper and its observed 12-byte IN / 16-byte OUT call (r150). */
struct MT_PVR_PACKED mt_pvr_multicore_info_in {
	u64 caps;
	u32 query_flags;
};

struct MT_PVR_PACKED mt_pvr_multicore_info_out {
	u64 caps;
	u32 error;
	u32 num_cores;
};

/* 0x86:0x4 MUSA:MUSAAcquireHWPerfSettings -- no IN, 12-byte OUT.
 *
 * The 5.2.0 generated header declares
 *   typedef struct MTGPU_BRIDGE_OUT_MUSAACQUIREHWPERFSETTING_TAG {
 *       MTGPU_ERROR eError; MT_HANDLE hPMR;
 *   } __packed ...;
 * which is 8 bytes, but the UMD sends out_size=12 for this command. That is the
 * same widening that MT_HANDLE's 64-bit form implies everywhere else: an S1 run
 * proved the UMD reads a handle from offset 0 of these 12-byte outs, because the
 * handle it then passed to PmrLocalImportPmr matched a real PMR. So hPMR is 8
 * bytes and the trailing 4 are the error, i.e. exactly mt_pvr_handle_out.
 */
struct MT_PVR_PACKED mt_pvr_hwperf_release_in {
	u64 pmr;
};

/* 0x86:0x5 MUSA:MUSAReleaseHWPerfSettings -- 8-byte IN, 4-byte OUT. */
struct MT_PVR_PACKED mt_pvr_hwperf_release_out {
	u32 error;
};

/* 0x89:0x5 RGXTDMGetSharedMemory -- no IN, 20-byte OUT.
 * 0x89:0x6 RGXTDMReleaseSharedMemory -- 8-byte IN, 4-byte OUT.
 *
 * Transfer (2D/blit) shared memory: the UMD's RGXTDMCreateStaticMem calls
 * the 0x89:0x5 wrapper with no input and expects two u64s back, which it
 * stores at client+0x30/+0x38 and feeds to TQPMR_MapMem / TQPMR_MapUSCMem
 * (r87). eError rides LAST here ({u64, u64, u32}), unlike the 0x88 family.
 *
 * Initial spike semantics (r87) aliased both u64s to one PMR. Live r150
 * falsified that: the UMD imports and releases them independently, so the
 * bridge now returns distinct CLI and USC PMR handles.
 */
struct MT_PVR_PACKED mt_pvr_tdm_shmem_out {
	u64 ptr1;
	u64 ptr2;
	u32 error;
};

struct MT_PVR_PACKED mt_pvr_tdm_release_in {
	u64 handle;
};

struct MT_PVR_PACKED mt_pvr_tdm_release_out {
	u32 error;
};

/* 0x89:0x8 RGXTDMCreateTransferContext2 -- 12B IN/OUT.
 * Context lifecycle only: SubmitTransfer3 now has an ABI shape below, but its
 * nested-pointer/PMR validation and CCB completion contract remain unresolved. */
struct MT_PVR_PACKED mt_pvr_tdm_context2_create_in {
	u64 device_mem_context;
	u32 context_type;
};

struct MT_PVR_PACKED mt_pvr_tdm_context2_create_out {
	u64 transfer_context;
	u32 error;
};

/* 0x89:0x9 DestroyTransferContext2 -- 8B IN/4B OUT. */
struct MT_PVR_PACKED mt_pvr_tdm_context2_destroy_in {
	u64 transfer_context;
};

struct MT_PVR_PACKED mt_pvr_tdm_context2_destroy_out {
	u32 error;
};

/* 0x89:0xa RGXTDMSubmitTransfer3 -- 108B IN/4B OUT, recovered from the
 * 5.2.0 Linux UMD wrapper/callsite, not a generated KMD header. These fields
 * mirror its packed scalar/pointer slots. The caller builds up to 32 check
 * syncs and 32 update syncs as parallel handle/offset/value arrays, and up to
 * 17 PMR syncs as handle/access-flag arrays. `ccb_data` points to bytes from
 * SubmissionCmdGenerate; `ccb_bytes` is that generated buffer size.
 * `submit_opaque` is passed through by the UMD but its meaning is not established.
 * This describes the input ABI only; the bridge accept-and-logs the submission
 * (reports the CCB window, returns 0) without executing it or reading nested
 * pointers, until pointer bounds, PMR/CCB ownership and completion semantics
 * are safe enough to translate.
 */
struct MT_PVR_PACKED mt_pvr_tdm_submit3_in {
	u64 transfer_context;
	u32 check_count;
	u64 check_handles;
	u64 check_offsets;
	u64 check_values;
	u32 update_count;
	u64 update_handles;
	u64 update_offsets;
	u64 update_values;
	u32 pmr_sync_count;
	u64 pmr_sync_access_flags;
	u64 pmr_sync_handles;
	u32 submit_flags;
	u64 ccb_data;
	u64 submit_opaque;
	u32 ccb_bytes;
};

struct MT_PVR_PACKED mt_pvr_tdm_submit3_out {
	u32 error;
};

/* 0x6:0x3 MM:PmrMakeLocalImportHandle -- 8-byte IN, 12-byte OUT.
 *
 * This was sharing the 0x6:0x6 handler, whose 28-byte OUT no longer fits the
 * 12 bytes the UMD supplies here. The undersized OUT buffer made pvr_out()
 * reject a valid request with -EINVAL, which CreateSyncPrim reports as error
 * 37.
 */
struct MT_PVR_PACKED mt_pvr_make_import_in {
	u64 buffer;
};

struct MT_PVR_PACKED mt_pvr_make_import_out {
	u64 ext_mem;
	u32 error;
};

/* 0x81:0x0 RGXCreateComputeContext -- 60-byte IN, 12-byte OUT.
 * 0x81:0x1 RGXDestroyComputeContext -- 8-byte IN, 4-byte OUT.
 *
 * Only the handle and eError cross the boundary; the framework command and
 * static state blobs are UMD-side inputs the bridge does not consume at this
 * stage. Like the kick-sync context, this is object lifecycle only -- submits
 * go through 0x81:0x5 RGXKICKSYNC2 and friends, which stay refused (S4).
 */
struct MT_PVR_PACKED mt_pvr_compute_create_in {
	u64 robustness_address;
	u64 priv_data;
	u64 framework_cmd;
	u64 static_state;
	u32 context_flags;
	u32 framework_cmd_size;
	u64 max_deadline_ms;
	u32 packed_ccb_size;
	u32 priority;
	u32 static_state_size;
};

struct MT_PVR_PACKED mt_pvr_compute_create_out {
	u64 compute_context;
	u32 error;
};

struct MT_PVR_PACKED mt_pvr_compute_destroy_in {
	u64 compute_context;
};

struct MT_PVR_PACKED mt_pvr_compute_destroy_out {
	u32 error;
};

/* 0x82:0x2 RGXCreateZSBuffer -- 24-byte IN, 12-byte OUT.
 * 0x82:0x3 RGXDestroyZSBuffer -- 8-byte IN, 4-byte OUT.
 *
 * IN 0x82:0x2 = { hPMR, hReservation }; the driver maps the already-allocated
 * PMR into the reservation and returns the kernel mapping handle.
 * Pure object lifecycle, like the render/compute/kicksync contexts.
 */
struct MT_PVR_PACKED mt_pvr_zs_create_in {
	u64 pmr;
	u64 reservation;
	/* PVRSRV_MEMALLOCFLAGS_T uiMapFlags: the 2.7.1 header declares it and
	 * the UMD sends all 24 bytes. The requirements table only names the
	 * two handles; the size gate below pins the full width.
	 */
	u64 map_flags;
};

struct MT_PVR_PACKED mt_pvr_zs_create_out {
	u64 zs_buffer_km;
	u32 error;
};

struct MT_PVR_PACKED mt_pvr_zs_destroy_in {
	u64 zs_buffer;
};

struct MT_PVR_PACKED mt_pvr_zs_destroy_out {
	u32 error;
};

/* 0x82:0x12 BridgeRGXCreateRenderContext2 (DDK2; r141) -- 12-byte IN, 12-byte OUT.
 * 0x82:0x13 BridgeRGXDestroyRenderContext2 (DDK2) -- 8-byte IN, 4-byte OUT.
 *
 * IN 0x82:0x12 = { hPrivData, ui32Priority } per the 2.7.1 generated header;
 * the decompiled UMD stub (FUN_00137b20) passes {u64, u32=0} the same way.
 * OUT 0x82:0x12 = { hRenderContext, eError } per the header AND the live wire
 * (r142: the render-phase callsite passes out_size=12; an early stub reading
 * suggested 4, but execution shows 12 -- execution wins).
 * Like legacy 0x82:0x8 the bridge mints a context without interpreting the
 * IN payload; only the sizes are validated.
 */
struct MT_PVR_PACKED mt_pvr_render2_create_in {
	u64 priv_data;
	u32 priority;
};

struct MT_PVR_PACKED mt_pvr_render2_create_out {
	u64 handle;
	u32 error;
};

/* 0x82:0x14 RGXKICKTA3D5 / 5.2 MUSA:MUSAKICKGFX5 -- 108-byte IN, 4-byte OUT.
 * Wire layout follows the hash-verified 5.2.0 Host generated header
 * reference/kmd-5.2.0-server-generated/common_musagfx_bridge.h and the UMD
 * wrapper callsite. The 2.7.1 Native header is 12 bytes shorter: it omits
 * ui32SubmissionFlags and ui64SubmissionId, so it is not this wire layout.
 *
 * This type only describes the packet. The bridge accept-and-logs the
 * submission (reports the CCB window and counts, returns 0) without
 * executing it or reading nested pointers, until pointer bounds, PMR/CCB
 * ownership and completion semantics are safe enough to translate
 * (r215, mirroring the 0x89:0xa observer).
 */
struct MT_PVR_PACKED mt_pvr_rgxkickta3d5_in {
	u64 render_context;
	u64 check_sync_prim_blocks;
	u64 check_sync_offsets;
	u64 check_values;
	u64 update_sync_prim_blocks;
	u64 update_sync_offsets;
	u64 update_values;
	u64 sync_pmr_flags;
	u64 sync_pmrs;
	u32 submission_flags;
	u64 submission_va;
	u32 submission_size;
	u64 submission_id;
	u32 check_count;
	u32 update_count;
	u32 sync_pmr_count;
};

struct MT_PVR_PACKED mt_pvr_rgxkickta3d5_out {
	u32 error;
};

/* 0x88:0x0 RGXCreateKickSyncContext -- 16-byte IN, 12-byte OUT.
 * 0x88:0x1 RGXDestroyKickSyncContext -- 8-byte IN, 4-byte OUT.
 *
 *   IN  0x88:0x0 = { hPrivData, ui32ContextFlags, ui32PackedCCBSizeU88 }
 *   OUT 0x88:0x0 = { hKickSyncContext, eError }
 *
 * Creating the context only mints a kernel object; no work is submitted.
 * 0x88:0x2 RGXKICKSYNC2 is deliberately NOT implemented here: that is a real
 * hardware submission (S4) and needs separate approval.
 */
struct MT_PVR_PACKED mt_pvr_kicksync_create_in {
	u64 priv_data;
	u32 context_flags;
	u32 packed_ccb_size;
};

struct MT_PVR_PACKED mt_pvr_kicksync_create_out {
	u64 kicksync_context;
	u32 error;
};

struct MT_PVR_PACKED mt_pvr_kicksync_destroy_in {
	u64 kicksync_context;
};

struct MT_PVR_PACKED mt_pvr_kicksync_destroy_out {
	u32 error;
};

/* 0x88:0x5 BridgeRGXCreateKickSyncContext2 (DDK2; r141) -- 8-byte IN, 12-byte OUT.
 * 0x88:0x6 BridgeRGXDestroyKickSyncContext2 (DDK2) -- 8-byte IN, 4-byte OUT.
 *
 * IN 0x88:0x5 = { hPrivData } (single u64, per the decompiled stub
 * FUN_00135d90 and the r141/r142 live traces). OUT 0x88:0x5 =
 * { hKickSyncContext, eError }, mirroring legacy 0x88:0x0.
 * 0x88:0x6 reuses the legacy 8-in/4-out destroy shape, no new structs.
 */
struct MT_PVR_PACKED mt_pvr_kicksyncctx2_create_in {
	u64 priv_data;
};

struct MT_PVR_PACKED mt_pvr_kicksyncctx2_create_out {
	u64 kicksync_context;
	u32 error;
};

/* Bridge function IDs compared by name in dispatch (r187/r188).
 * Values from reports/stage-b-bridge-requirements.json (5.2 KMD headers);
 * only IDs the dispatch actually handles are named here. Dispatch case
 * labels carry the same names as comments. 0x89:0xa RGXTDMSUBMITTRANSFER3
 * follows the table's SUBMITTRANSFER2 pattern (the table says CMD_LAST).
 */
#define MT_PVR_FN_CONNECT 0x0U
#define MT_PVR_FN_DISCONNECT 0x1U
#define MT_PVR_FN_ACQUIREGLOBALEVENTOBJECT 0x2U
#define MT_PVR_FN_RELEASEGLOBALEVENTOBJECT 0x3U
#define MT_PVR_FN_EVENTOBJECTOPEN 0x4U
#define MT_PVR_FN_EVENTOBJECTWAIT 0x5U
#define MT_PVR_FN_EVENTOBJECTCLOSE 0x6U
#define MT_PVR_FN_ALIGNMENTCHECK 0xaU
#define MT_PVR_FN_EVENTOBJECTWAITTIMEOUT 0xdU
#define MT_PVR_FN_GETMULTICOREINFO 0xcU
#define MT_PVR_FN_ACQUIREINFOPAGE 0xfU
#define MT_PVR_FN_RELEASEINFOPAGE 0x10U
#define MT_PVR_FN_ALLOCSYNCPRIMITIVEBLOCK 0x0U
#define MT_PVR_FN_FREESYNCPRIMITIVEBLOCK 0x1U
#define MT_PVR_FN_SYNCPRIMSET 0x2U
#define MT_PVR_FN_SYNCALLOCEVENT 0x7U
#define MT_PVR_FN_SYNCFREEEVENT 0x8U
#define MT_PVR_FN_PMRMAKELOCALIMPORTHANDLE 0x3U
#define MT_PVR_FN_PMRUNMAKELOCALIMPORTHANDLE 0x4U
#define MT_PVR_FN_PMRLOCALIMPORTPMR 0x6U
#define MT_PVR_FN_PMRUNREFPMR 0x7U
#define MT_PVR_FN_DEVMEMINTCTXDESTROY 0x10U
#define MT_PVR_FN_DEVMEMINTHEAPDESTROY 0x12U
#define MT_PVR_FN_PHYSMEMNEWRAMBACKEDPMR 0x9U
#define MT_PVR_FN_DEVMEMINTCTXCREATE 0xfU
#define MT_PVR_FN_DEVMEMINTHEAPCREATE 0x11U
#define MT_PVR_FN_DEVMEMINTMAPPMR 0x13U
#define MT_PVR_FN_DEVMEMINTUNMAPPMR 0x14U
#define MT_PVR_FN_DEVMEMINTRESERVERANGE 0x15U
#define MT_PVR_FN_DEVMEMINTUNRESERVERANGE 0x16U
#define MT_PVR_FN_HEAPCFGHEAPCOUNT 0x1eU
#define MT_PVR_FN_HEAPCFGHEAPDETAILS 0x20U
#define MT_PVR_FN_MTGPUUPDATEOOMSTATS 0x27U
#define MT_PVR_FN_RGXCREATECOMPUTECONTEXT 0x0U
#define MT_PVR_FN_RGXDESTROYCOMPUTECONTEXT 0x1U
#define MT_PVR_FN_RGXCREATEZSBUFFER 0x2U
#define MT_PVR_FN_RGXDESTROYZSBUFFER 0x3U
#define MT_PVR_FN_RGXCREATERENDERCONTEXT 0x8U
#define MT_PVR_FN_RGXDESTROYRENDERCONTEXT 0x9U
#define MT_PVR_FN_RGXCREATERENDERCONTEXT2 0x12U
#define MT_PVR_FN_RGXDESTROYRENDERCONTEXT2 0x13U
#define MT_PVR_FN_RGXKICKTA3D5 0x14U
#define MT_PVR_FN_RGXCREATEKICKSYNCCONTEXT 0x0U
#define MT_PVR_FN_RGXDESTROYKICKSYNCCONTEXT 0x1U
#define MT_PVR_FN_RGXKICKSYNC2 0x2U
#define MT_PVR_FN_RGXSETKICKSYNCCONTEXTPROPERTY 0x3U
#define MT_PVR_FN_RGXKICKSYNC3 0x4U
#define MT_PVR_FN_RGXCREATEKICKSYNCCONTEXT2 0x5U
#define MT_PVR_FN_RGXDESTROYKICKSYNCCONTEXT2 0x6U
#define MT_PVR_FN_RGXACQUIREHWPERFFSETTINGS 0x4U
#define MT_PVR_FN_RGXRELEASEHWPERFFSETTINGS 0x5U
#define MT_PVR_FN_RGXTDMGETSHAREDMEMORY 0x5U
#define MT_PVR_FN_RGXTDMRELEASESHAREDMEMORY 0x6U
#define MT_PVR_FN_RGXTDMCREATETRANSFERCONTEXT2 0x8U
#define MT_PVR_FN_RGXTDMDESTROYTRANSFERCONTEXT2 0x9U
#define MT_PVR_FN_RGXTDMSUBMITTRANSFER3 0xaU

/* 0x88:0x2 RGXKickSync2 -- 56-byte IN, 8-byte OUT.
 * 0x88:0x3 RGXSetKickSyncContextProperty -- 20-byte IN, 12-byte OUT.
 * 0x88:0x4 RGXKickSync3 (the TA-submit entry point) -- 84-byte IN, 8-byte OUT.
 *
 * IN 0x88:0x2 = { hKickSyncContext, pUpdateDevVarOffset, pUpdateValue,
 *                  pFenceName, phUFOBlock, hCheckFenceFD, hTimelineFenceFD,
 *                  ui32ClientUpdateCount, ui32ExtJobRef }
 * IN 0x88:0x4 = { hKickSyncContext, pCheckDevVarOffset, pCheckValue,
 *                  phCheckUFOBlock, ui32ClientCheckCount,
 *                  pUpdateDevVarOffset, pUpdateValue, phUpdateUFOBlock,
 *                  ui32ClientUpdateCount, pUpdateFenceName,
 *                  hCheckFenceFD, hTimelineFenceFD, ui32ExtJobRef }
 * OUT 0x88:0x2 = { eError, hUpdateFenceFD }
 * IN 0x88:0x3 = { ui64Input, hKickSyncContext, ui32Property }
 * OUT 0x88:0x3 = { ui64Output, eError }
 * OUT 0x88:0x4 = { eError, hUpdateFenceFD }
 *
 * Field map matches MTGPU_BRIDGE_IN_MUSAKICKSYNC3 in the 5.2.0 generated
 * headers (reference/kmd-5.2.0-server-generated/common_musakicksync_bridge.h).
 * Verified against two live 5.2 UMD captures (reports/r53): handle first,
 * all check/update pointers NULL with zero counts on the synthetic kick,
 * fence-name pointer canonical-userspace, timeline FD -1, extJobRef 0.
 * The packet carries sync bookkeeping only -- no GPU command bytes. The
 * IN buffers carry UMD-side pointers the bridge must not dereference;
 * only sizes and the context handle are read. Fences are eventfds the bridge
 * signals immediately: this is bridge-stage completion, NOT GPU execution.
 * There is no firmware channel yet, so nothing here touches hardware.
 */
struct MT_PVR_PACKED mt_pvr_kicksync2_in {
	u64 kicksync_context;
	u64 update_devvar_offset;
	u64 update_value;
	u64 fence_name;
	u64 ufo_block;
	u32 check_fence_fd;
	u32 timeline_fence_fd;
	u32 client_update_count;
	u32 ext_job_ref;
};

struct MT_PVR_PACKED mt_pvr_kicksync2_out {
	u32 error;
	int update_fence_fd;
};

struct MT_PVR_PACKED mt_pvr_kicksync_prop_in {
	u64 input;
	u64 kicksync_context;
	u32 property;
};

/* 0x88:0x4 IN, documented above. The bridge still reads only the handle:
 * every other field is a UMD-side pointer, count or fd. */
struct MT_PVR_PACKED mt_pvr_kicksync3_in {
	u64 kicksync_context;
	u64 check_devvar_offset;
	u64 check_value;
	u64 check_ufo_block;
	u32 client_check_count;
	u64 update_devvar_offset;
	u64 update_value;
	u64 update_ufo_block;
	u32 client_update_count;
	u64 update_fence_name;
	u32 check_fence_fd;
	u32 timeline_fence_fd;
	u32 ext_job_ref;
};

struct MT_PVR_PACKED mt_pvr_kicksync_prop_out {
	u64 output;
	u32 error;
};

struct MT_PVR_PACKED mt_pvr_kicksync3_out {
	u32 error;
	int update_fence_fd;
};

/* 0x6:0x4 MM:PmrUnmakeLocalImportHandle -- 8-byte IN, 4-byte OUT.
 *
 * The bridge does not mint a separate object for a local import: the exported
 * handle is the PMR's own handle. Unmaking therefore only validates the handle;
 * the PMR itself stays alive until 0x6:0x7 releases it.
 */
struct MT_PVR_PACKED mt_pvr_unmake_import_in {
	u64 ext_mem;
};

struct MT_PVR_PACKED mt_pvr_unmake_import_out {
	u32 error;
};

/* 0x6:0x6 MM:PmrLocalImportPmr -- 8-byte IN, 28-byte OUT. */
struct MT_PVR_PACKED mt_pvr_import_in {
	u64 ext_handle;
};

struct MT_PVR_PACKED mt_pvr_import_out {
	u64 align;
	u64 size;
	u64 pmr;
	u32 error;
};

/* 0x6:1e MM:HeapCfgHeapCount -- no IN, 8-byte OUT.
 *
 * The order matters and was wrong for several rounds:
 *
 *   MTGPU_BRIDGE_OUT_HEAPCFGHEAPCOUNT = { MTGPU_ERROR eError;
 *                                         MT_UINT32 ui32NumHeaps; }
 *
 * eError is FIRST. The bridge used to write a bare count at offset 0, so the
 * UMD read our count (11) as eError and ui32NumHeaps as 0 -- it then cached
 * "no heaps" at device-connect time and every later heap lookup failed, which
 * is why RGXCreateDeviceMemContext stopped with FLIP_CHAIN_EXISTS before it
 * ever issued a single allocation.
 */
struct MT_PVR_PACKED mt_pvr_heap_count_out {
	u32 error;
	u32 num_heaps;
};

/* 0x6:0x14 MM:DevmemIntUnmapPMR -- 8-byte IN, 4-byte OUT.
 * 0x6:0x16 MM:DevmemIntUnreserveRange -- 8-byte IN, 4-byte OUT.
 *
 * Both are a single widened handle, matching their Create counterparts
 * (0x6:0x13 DevmemIntMapPMR and 0x6:0x15 DevmemIntReserveRange).
 *
 * Both were refused with -ENOTTY, and the UMD issues one of each per mapping
 * it drops. During RGXCreateRenderContext that happened eight times over, and
 * the first refusal surfaced as 38 = MTSRV_ERROR_IOCTL_CALL_FAILED.
 */
struct MT_PVR_PACKED mt_pvr_unmap_pmr_in {
	u64 mapping;
};

struct MT_PVR_PACKED mt_pvr_unreserve_in {
	u64 reservation;
};

struct MT_PVR_PACKED mt_pvr_unmap_out {
	u32 error;
};

/* 0x6:0x27 MM:MTGPUUpdateOOMStats -- 8-byte IN, 4-byte OUT.
 *
 *   MTGPU_BRIDGE_IN_MTGPUUPDATEOOMSTATS  = { ui32pid, ui32ui32StatType }
 *   MTGPU_BRIDGE_OUT_MTGPUUPDATEOOMSTATS = { eError }
 *
 * The UMD issues this while creating a render context. It only records
 * out-of-memory statistics, so there is nothing for the driver to act on; it
 * was refused with -ENOTTY, which the UMD treats as a hard failure and turns
 * into error 1.
 */
struct MT_PVR_PACKED mt_pvr_oom_stats_in {
	u32 pid;
	u32 stat_type;
};

struct MT_PVR_PACKED mt_pvr_oom_stats_out {
	u32 error;
};

/* 0x6:0x12 MM:DevmemIntHeapDestroy -- 8-byte IN, 4-byte OUT.
 *
 * The handle here is the one DevmemIntHeapCreate returned. This was an empty
 * stub that ignored both, so the UMD's heap objects were never released and
 * it went on to free them itself.
 *
 * The 5.2.0 header declares the in as a 4-byte MT_HANDLE, but the driver
 * actually sends 8: it widens handles to 64 bits, the same widening that
 * MTGPU_BRIDGE_OUT_MUSAACQUIREHWPERFSETTING shows (see mt_pvr_handle_out).
 * The captured size is authoritative here, not the older header.
 */
struct MT_PVR_PACKED mt_pvr_heap_destroy_in {
	u64 devmem_heap;
};

struct MT_PVR_PACKED mt_pvr_heap_destroy_out {
	u32 error;
};

/* 0x6:0x11 MM:DevmemIntHeapCreate -- 28-byte IN, 12-byte OUT.
 *
 *   MTGPU_BRIDGE_IN_DEVMEMINTHEAPCREATE  = { sHeapBaseAddr, uiHeapLength,
 *                                            hDevmemCtx, ui32Log2DataPageSize }
 *   MTGPU_BRIDGE_OUT_DEVMEMINTHEAPCREATE = { hDevmemHeapPtr, eError }
 *
 * This was previously routed to the PMR-map handler, which read a different
 * 28-byte struct and therefore rejected a perfectly good request with -EINVAL.
 * Note the (2) variant differs only in ui32PageSizeBitMask vs
 * ui32Log2DataPageSize; only the non-(2) form is CMD_FIRST+17.
 */
struct MT_PVR_PACKED mt_pvr_heap_create_in {
	u64 heap_base_addr;
	u64 heap_length;
	u64 devmem_ctx;
	u32 log2_data_page_size;
};

struct MT_PVR_PACKED mt_pvr_heap_create_out {
	u64 devmem_heap_ptr;
	u32 error;
};

/* 0x6:0x20 MM:HeapCfgHeapDetails -- 20-byte IN, 44-byte OUT. */
struct MT_PVR_PACKED mt_pvr_heap_details_in {
	u64 heap_name_out;
	u32 heap_config_index;
	u32 heap_index;
	u32 heap_name_buf_size;
};

struct MT_PVR_PACKED mt_pvr_heap_details_out {
	mt_gpuvaddr base;
	u64 length;
	u64 reserved_length;
	u64 heap_name_out;
	u32 error;
	u32 log2_data_page_size;
	u32 log2_import_alignment;
};

/* 0x6:0x9 MM:PhysMemNewRamBackedPmr -- 72-byte IN, 24-byte OUT.
 * The header struct stops at 68/20; the driver sends 4 more bytes each way.
 */
struct MT_PVR_PACKED mt_pvr_pmr_in {
	u64 chunk_size;
	u64 size;
	u64 mapping_table;
	u64 annotation;
	u32 annotation_length;
	u32 log2_page_size;
	u32 num_phys_chunks;
	u32 num_virt_chunks;
	u32 pdump_flags;
	u32 pid;
	u32 flags;
	u64 user_address;
	u32 wire_tail_in;
};

struct MT_PVR_PACKED mt_pvr_pmr_out {
	u64 pmr;
	u32 error;
	u32 out_flags;
	u32 is_system_mem;
	u32 wire_tail_out;
};

/* 0x6:0x13 MM:DevmemIntMapPmr -- 32-byte IN (header declares 28), 12-byte OUT. */
struct MT_PVR_PACKED mt_pvr_map_in {
	u64 server_heap;
	u64 pmr;
	u64 reservation;
	u32 map_flags;
	u32 wire_tail_in;
};

struct MT_PVR_PACKED mt_pvr_map_out {
	u64 mapping;
	u32 error;
};

/* 0x6:0x15 MM:DevmemIntReserveRange -- 24-byte IN, 12-byte OUT. */
struct MT_PVR_PACKED mt_pvr_reserve_in {
	mt_gpuvaddr address;
	u64 length;
	u64 server_heap;
};

struct MT_PVR_PACKED mt_pvr_reserve_out {
	u64 reservation;
	u32 error;
};

/* 0x6:0xf MM:DevmemIntCtxCreate -- 4-byte IN, 24-byte OUT. The driver refcounts
 * contexts per connection and reuses the same object (decompiled.c:13673), so
 * both handles must stay stable across calls.
 */
struct MT_PVR_PACKED mt_pvr_ctx_create_in {
	u32 flags;
};

struct MT_PVR_PACKED mt_pvr_ctx_create_out {
	u64 server_context;
	u64 priv_data;
	u32 error;
	u32 cpu_cache_line_size;
};

/* 0x2:0x0 SYNC:AllocSyncPrimitiveBlock -- 8-byte IN, 32-byte OUT.
 * memType is 0x100000000, measured live in bA13; the static 0x2 in older notes
 * was the arena class, not this field.
 */
struct MT_PVR_PACKED mt_pvr_sync_block_in {
	u64 mem_type;
};

struct MT_PVR_PACKED mt_pvr_sync_block_out {
	u64 sync_handle;
	u64 sync_pmr;
	u32 error;
	u32 block_size;
	u64 vaddr;
};

#define MT_PVR_SYNC_MEM_TYPE 0x100000000ULL

/* 0x2:0x2 SYNC:SyncPrimSet -- 16-byte IN, 4-byte OUT (r220).
 * Wire layout from the hash-verified 5.2.0 UMD wrapper FUN_00139220:
 * IN = { u64 sync, u32 dword_index, u32 value } backed by a 16-byte stack
 * slot, OUT = single u32 error. The public SyncPrimSet(psSync, value)
 * derives index as (ufo_byte_offset >> 2); the DDK2 CpuSignal path uses
 * a different function (0x2:0xd, still refused) and is out of scope.
 * The bridge writes one u32 into the resolved PMR; concurrent translator
 * waiters poll PMR memory, so no wakeup is needed or added.
 */
struct MT_PVR_PACKED mt_pvr_syncprimset_in {
	u64 sync;
	u32 index;
	u32 value;
};

struct MT_PVR_PACKED mt_pvr_syncprimset_out {
	u32 error;
};

static_assert(sizeof(struct mt_pvr_cmd) == 32, "dispatch packet");
static_assert(sizeof(struct mt_pvr_connect_in) == 16, "0x1:0x0 in");
static_assert(sizeof(struct mt_pvr_connect_out) == 17, "0x1:0x0 out");
static_assert(sizeof(struct mt_pvr_handle_out) == 12, "0x1:0x2/0x1:0xf out");
static_assert(sizeof(struct mt_pvr_event_open_in) == 8, "0x1:0x4 in");
static_assert(sizeof(struct mt_pvr_event_open_out) == 12, "0x1:0x4 out");
static_assert(sizeof(struct mt_pvr_multicore_info_in) == 12, "0x1:0xc in");
static_assert(sizeof(struct mt_pvr_multicore_info_out) == 16, "0x1:0xc out");
static_assert(sizeof(struct mt_pvr_zs_create_in) == 24, "0x82:0x2 in");
static_assert(sizeof(struct mt_pvr_zs_create_out) == 12, "0x82:0x2 out");
static_assert(sizeof(struct mt_pvr_zs_destroy_in) == 8, "0x82:0x3 in");
static_assert(sizeof(struct mt_pvr_zs_destroy_out) == 4, "0x82:0x3 out");
static_assert(sizeof(struct mt_pvr_render2_create_in) == 12, "0x82:0x12 in");
static_assert(sizeof(struct mt_pvr_render2_create_out) == 12, "0x82:0x12 out");
static_assert(sizeof(struct mt_pvr_rgxkickta3d5_in) == 108, "0x82:0x14 in");
static_assert(sizeof(struct mt_pvr_rgxkickta3d5_out) == 4, "0x82:0x14 out");
static_assert(__builtin_offsetof(struct mt_pvr_rgxkickta3d5_in, submission_flags) == 72,
	      "0x82:0x14 submission flags offset");
static_assert(__builtin_offsetof(struct mt_pvr_rgxkickta3d5_in, submission_va) == 76,
	      "0x82:0x14 submission VA offset");
static_assert(__builtin_offsetof(struct mt_pvr_rgxkickta3d5_in, submission_size) == 84,
	      "0x82:0x14 submission size offset");
static_assert(__builtin_offsetof(struct mt_pvr_rgxkickta3d5_in, submission_id) == 88,
	      "0x82:0x14 submission ID offset");
static_assert(__builtin_offsetof(struct mt_pvr_rgxkickta3d5_in, check_count) == 96,
	      "0x82:0x14 check count offset");
static_assert(__builtin_offsetof(struct mt_pvr_rgxkickta3d5_in, update_count) == 100,
	      "0x82:0x14 update count offset");
static_assert(__builtin_offsetof(struct mt_pvr_rgxkickta3d5_in, sync_pmr_count) == 104,
	      "0x82:0x14 PMR count offset");
static_assert(sizeof(struct mt_pvr_compute_create_in) == 60, "0x81:0x0 in");
static_assert(sizeof(struct mt_pvr_compute_create_out) == 12, "0x81:0x0 out");
static_assert(sizeof(struct mt_pvr_compute_destroy_in) == 8, "0x81:0x1 in");
static_assert(sizeof(struct mt_pvr_compute_destroy_out) == 4, "0x81:0x1 out");
static_assert(sizeof(struct mt_pvr_kicksync_create_in) == 16, "0x88:0x0 in");
static_assert(sizeof(struct mt_pvr_kicksync_create_out) == 12, "0x88:0x0 out");
static_assert(sizeof(struct mt_pvr_kicksync2_in) == 56, "0x88:0x2 in");
static_assert(sizeof(struct mt_pvr_kicksync2_out) == 8, "0x88:0x2 out");
static_assert(sizeof(struct mt_pvr_kicksync_prop_in) == 20, "0x88:0x3 in");
static_assert(sizeof(struct mt_pvr_kicksync3_in) == 84, "0x88:0x4 in");
static_assert(sizeof(struct mt_pvr_kicksync_prop_out) == 12, "0x88:0x3 out");
static_assert(sizeof(struct mt_pvr_kicksync3_out) == 8, "0x88:0x4 out");
static_assert(sizeof(struct mt_pvr_kicksync_destroy_in) == 8, "0x88:0x1 in");
static_assert(sizeof(struct mt_pvr_kicksync_destroy_out) == 4, "0x88:0x1 out");
static_assert(sizeof(struct mt_pvr_kicksyncctx2_create_in) == 8, "0x88:0x5 in");
static_assert(sizeof(struct mt_pvr_kicksyncctx2_create_out) == 12, "0x88:0x5 out");
static_assert(sizeof(struct mt_pvr_unmake_import_in) == 8, "0x6:0x4 in");
static_assert(sizeof(struct mt_pvr_unmake_import_out) == 4, "0x6:0x4 out");
static_assert(sizeof(struct mt_pvr_make_import_in) == 8, "0x6:0x3 in");
static_assert(sizeof(struct mt_pvr_make_import_out) == 12, "0x6:0x3 out");
static_assert(sizeof(struct mt_pvr_import_in) == 8, "0x6:0x6 in");
static_assert(sizeof(struct mt_pvr_hwperf_release_in) == 8, "0x86:0x5 in");
static_assert(sizeof(struct mt_pvr_hwperf_release_out) == 4, "0x86:0x5 out");
static_assert(sizeof(struct mt_pvr_tdm_shmem_out) == 20, "0x89:0x5 out");
static_assert(sizeof(struct mt_pvr_tdm_release_in) == 8, "0x89:0x6 in");
static_assert(sizeof(struct mt_pvr_tdm_release_out) == 4, "0x89:0x6 out");
static_assert(sizeof(struct mt_pvr_tdm_submit3_in) == 108, "0x89:0xa in");
static_assert(sizeof(struct mt_pvr_tdm_submit3_out) == 4, "0x89:0xa out");
static_assert(__builtin_offsetof(struct mt_pvr_tdm_submit3_in, check_handles) == 12,
	      "0x89:0xa check handles offset");
static_assert(__builtin_offsetof(struct mt_pvr_tdm_submit3_in, pmr_sync_count) == 64,
	      "0x89:0xa PMR sync count offset");
static_assert(__builtin_offsetof(struct mt_pvr_tdm_submit3_in, ccb_data) == 88,
	      "0x89:0xa CCB pointer offset");
static_assert(__builtin_offsetof(struct mt_pvr_tdm_submit3_in, ccb_bytes) == 104,
	      "0x89:0xa CCB size offset");
/* 0x86:0x4 reuses mt_pvr_handle_out; its 12 bytes are the UMD's out_size, and
 * the 8-byte header form would truncate the handle away. */
static_assert(sizeof(struct mt_pvr_import_out) == 28, "0x6:0x6 out");
static_assert(sizeof(struct mt_pvr_heap_details_in) == 20, "0x6:0x20 in");
static_assert(sizeof(struct mt_pvr_heap_count_out) == 8, "0x6:0x1e out");
static_assert(sizeof(struct mt_pvr_heap_create_in) == 28, "0x6:0x11 in");
static_assert(sizeof(struct mt_pvr_heap_create_out) == 12, "0x6:0x11 out");
static_assert(sizeof(struct mt_pvr_heap_destroy_in) == 8, "0x6:0x12 in");
static_assert(sizeof(struct mt_pvr_unmap_pmr_in) == 8, "0x6:0x14 in");
static_assert(sizeof(struct mt_pvr_unreserve_in) == 8, "0x6:0x16 in");
static_assert(sizeof(struct mt_pvr_unmap_out) == 4, "0x6:0x14/0x6:0x16 out");
static_assert(sizeof(struct mt_pvr_oom_stats_in) == 8, "0x6:0x27 in");
static_assert(sizeof(struct mt_pvr_oom_stats_out) == 4, "0x6:0x27 out");
static_assert(sizeof(struct mt_pvr_heap_destroy_out) == 4, "0x6:0x12 out");
static_assert(sizeof(struct mt_pvr_heap_details_out) == 44, "0x6:0x20 out");
static_assert(sizeof(struct mt_pvr_pmr_in) == 72, "0x6:0x9 in (wire 72)");
static_assert(sizeof(struct mt_pvr_pmr_out) == 24, "0x6:0x9 out (wire 24)");
static_assert(sizeof(struct mt_pvr_map_in) == 32, "0x6:0x13 in (wire 32)");
static_assert(sizeof(struct mt_pvr_map_out) == 12, "0x6:0x13 out");
static_assert(sizeof(struct mt_pvr_reserve_in) == 24, "0x6:0x15 in");
static_assert(sizeof(struct mt_pvr_reserve_out) == 12, "0x6:0x15 out");
static_assert(sizeof(struct mt_pvr_ctx_create_in) == 4, "0x6:0xf in");
static_assert(sizeof(struct mt_pvr_ctx_create_out) == 24, "0x6:0xf out");
static_assert(sizeof(struct mt_pvr_sync_block_in) == 8, "0x2:0x0 in");
static_assert(sizeof(struct mt_pvr_sync_block_out) == 32, "0x2:0x0 out");
static_assert(sizeof(struct mt_pvr_syncprimset_in) == 16, "0x2:0x2 in");
static_assert(sizeof(struct mt_pvr_syncprimset_out) == 4, "0x2:0x2 out");

#endif /* MT_PVR_WIRE_H */
