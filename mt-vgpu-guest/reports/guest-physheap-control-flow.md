# Guest PhysHeap count control-flow audit

Date: 2026-09-29. Scope: static analysis of the official Linux 2.3.0 core
(`5c6c275`) and the matching platform-data initializer. No module was loaded
for this audit.

## What the core counts

The public source header defines `MTGPU_MEMORY_LOCAL=1`, `HOST=2`, and
`HYBRID=3` (`inc/mtgpu/mtgpu_defs.h:89-92`). In `SysDevInit`, the core starts
the expected count at `6 + (mem_mode == HYBRID)`. In Guest driver mode it
subtracts one (`host-2.3-core.asm`, `SysDevInit+0x85..+0xb6`, offsets
`0xe9275..0xe92a6`). Therefore the unmodified expectations are 5 for Local and
Host Guest, and 6 for Hybrid Guest.

The Guest path is selected after `mtgpu_get_driver_mode` at `+0x259`; a
nonzero result branches to `+0x310`. It constructs the extra Guest descriptor
and advances the running count to four (`+0x310..+0x383`, `0xe9500..0xe9573`).
The common heap path then reaches the mode check at `+0x3fc`:

| Guest memory mode | Core expected count | Descriptors/count from static path | Assessment |
|---|---:|---:|---|
| Local (1) | 5 | 4 | Matches the failure seen on this machine; reducing the base by one yields 4. |
| Host (2) | 5 | 4 | Same non-Hybrid Guest control flow; reducing the base by one is statically consistent. No runtime observation. |
| Hybrid (3) | 6 | `platform_data[0x68] + 2` | The branch writes one additional descriptor, then derives the count from a platform field. Its value was not captured, so the corrected count cannot be asserted. |

For Hybrid, `platform_data_vz_init` copies the field at `pdev_data+0x1130` to
`platform_data+0x68` (`0x38041..0x38049`). By the public structure layout,
`platform_data+0x68` is `mtgpu_vz_data.osid_count`: `mtgpu_platform_data` begins
`vz_data` at offset `0x38`, and `osid_count` is at offset `0x30` within it.
The device field is set in the VZ registration path from an OSID-per-core
callback (`0x3a7a7..0x3a7c7`, with a separate built-in calculation at
`0x3a8c9..0x3a8e9`). Thus Hybrid's emitted count is `osid_count + 2`, and it
depends on the configured virtualization topology rather than a fixed
constant. The header documents QY1 SR-IOV examples with `osid_count=5`
(`MC8*1`) and `osid_count=3` (`MC1*8`): those values imply 7 or 5 emitted
heaps, respectively. The one-byte patch changes Hybrid's expected count to 5,
so it only matches the latter example. This machine's Hybrid `osid_count` was
not captured; do not use the Local/Host correction as a Hybrid fix.

The first candidate changed the immediate in the shared `add $6,%r13d` to `$5`,
which also changed the non-Guest path. The current patch leaves that instruction
unchanged and redirects only the `get_driver_mode != 0` branch through a
10-byte alignment NOP at `SysDevInit+0x8b6`. The trampoline subtracts one from
the expected count and jumps to the original Guest block. It therefore gives
Local Guest the observed 4-vs-4 count while preserving non-Guest behavior. The
same adjustment is statically consistent for HOST-memory Guest; Hybrid still
depends on the configured OSID topology and has no runtime validation. The
patcher verifies the original branch target, exact NOP bytes, rel32 targets and
unchanged shared count instruction. Tests are in
`tests/test_guest_physheap_count_patch.py`; the candidate remains unloaded.

## 后续初始化风险：Guest 静态 premap 与条件性 PVZ 依赖

Getting past the count check would not establish working acceleration. The
same Linux core still wires Guest `pfnMapDevPhysHeap` and
`pfnUnmapDevPhysHeap` to stubs returning `PVRSRV_ERROR_NOT_IMPLEMENTED`; the
server-side map callbacks return `PVRSRV_ERROR_INVALID_PVZ_CONFIG`. A real
Guest PVZ provider matching this VM's Host ABI would be required if the runtime
takes the dynamic Guest heap-map path. A later control-flow audit of
`RGXInitCreateFWKernelMemoryContext` found that driver mode Guest (1) instead
marks FW_MAIN and FW_CONFIG premap and branches past the function's
`RGXFwRawHeapAllocMap` loop. Thus the PVZ stub is not proven to block the
configured static Guest path; other dynamic callers and the actual Host BAR2
backing remain unverified. See `static-guest-fw-premap.md` and
`pvz-map-abi-and-host-half.md`.
