# Guest MMU heap BAR2 fallback — withdrawn

**Do not load r15b.** The proposed mapping was arithmetic only and did not establish page-table backing. The new r17 version-2 snapshot maps BAR2 offset zero to the PB free-list allocation, so using it as a private MMU heap would overlap an existing purpose. The fallback source has been removed and its build flag now refuses the build. The observations below are historical; the subsequent Guest reboot cleared the old blocked unbind. See `guest-info-negotiation-live.md` for current results.

Date: 2026-09-29. This is the next offline adaptation after the live r14 diagnostic.

## Live evidence

The r14 diagnostic wrapper observed four heaps in Guest/Windows-FW mode:

| Heap | Start (CPU) | Card base | Size | Usage |
|---|---:|---:|---:|---:|
| 0 | `0x843000000` | `0` | `0x800000` | `0x10` (FW_MAIN) |
| 1 | `0` | `0x400000000` | `0x800000` | `0x08000000` (MMU_TABLE) |
| 2 | `0x800008000` | `0x8000` | `0xffffffffffff8000` | `0x20000000` (VPU_GROUP2) |
| 3 | `0x843800000` | `0x800000` | `0xffffffffff800000` | `0x2` (GPU_LOCAL) |

The VPU GROUP2 descriptor satisfied the existing GROUP1 alias condition. Initialization passed the earlier `PVRSRV_ERROR_PHYSHEAP_ID_INVALID` and entered `MMU_ContextCreate`. It then reported that `GuestLocalDevPAddrToCpuPAddr` rejected `0x400000000`. The platform setup had an empty BAR4 CPU window (`mmu_heap_base=0`, `mmu_heap_size=0`) while the MMU heap's card base was the BAR2 window size (`0x400000000`). Mapping continued with an invalid address and the kernel raised an Oops. Full evidence is in `guest-vpu-heap-alias-r14-live-kernel.log` and `guest-vpu-heap-alias-r14-live-validation.json`.

The Guest address translator's disassembly checks the MMU heap card-base range and translates an in-range device address by the CPU-base minus card-base delta. On this VM, BAR2 is assigned at GPA `0x800000000` with size `0x400000000`, while BAR4 is absent. The candidate therefore sets the CPU window to BAR2 only when BAR4 is empty, the card base equals the BAR2 size, and all range arithmetic is valid. For the observed layout, device PA `0x400000000` maps to BAR2 GPA `0x800000000`.

## Candidate

`build/official-vgpu-2.3.0-guest-vgpu-info-v2-addr-sharedmem-vpumap-audit13-r15b-mmu-bar2-fallback-20260929/mtgpu.ko` builds for the running kernel. SHA-256 is `194d88a0e9f964d689f6f188c66e82b79c16b1614e3627883c248fb7ba64938d`; Build ID is `56c6ac6237c620f5359fdcacfa007b6005eb62ce`.

Static relocation validation redirects the three audited platform-data entry points through the fallback wrapper. The wrapper retains the original platform initialization and changes the MMU window only for the guarded Guest/no-BAR4 layout. The build did not install or load this candidate; live behavior and hardware acceleration remain unverified.

The r14 Oops left the device bound, and the PCI unbind operation is still blocked in D state. A fresh Guest boot is required before another live bind attempt. No host-side operation was performed. The diagnostic wrapper itself did not issue MMIO writes; the driver's normal initialization path did access the device before the Oops.
