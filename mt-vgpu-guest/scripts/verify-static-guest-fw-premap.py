#!/usr/bin/env python3
"""Check the saved Linux 2.3 core disassembly for the static Guest FW path.

This is a report-only verifier. It does not access the PCI device or hardware.
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASM = ROOT / "reports/host-2.3-core.asm"
FUNCTION = "RGXInitCreateFWKernelMemoryContext"
CONFIG_HEADER = ROOT / "src/official-vgpu-2.3.0/usr/src/mtgpu-1.0.0/inc/config_kernel.h"
HEAP_ID_HEADER = ROOT / "src/official-vgpu-2.3.0/usr/src/mtgpu-1.0.0/inc/pvr/include/pvrsrv_memalloc_physheap.h"
FW_UTILS_HEADER = ROOT / "src/official-vgpu-2.3.0/usr/src/mtgpu-1.0.0/inc/pvr/services/rgxfwutils.h"
PREMAP_BASE = 0xE1C0000000
FW_HEAP_SIZE = 0x800000


def macro_value(text, name):
    match = re.search(rf"^#define\s+{re.escape(name)}\s+(\d+)U?\b", text, re.MULTILINE)
    if not match:
        raise ValueError(f"{name} is absent from the saved Guest headers")
    return int(match.group(1))


def function_body(text, function):
    marker = f"<{function}>:"
    start = text.find(marker)
    if start < 0:
        raise ValueError(f"{function} is absent from the saved core disassembly")
    start = text.rfind("\n", 0, start) + 1
    end = text.find("\n000000000", start + 1)
    return text[start:] if end < 0 else text[start:end]


def verify(text, config_header=None, heap_id_header=None, fw_utils_header=None):
    config_header = CONFIG_HEADER.read_text() if config_header is None else config_header
    heap_id_header = HEAP_ID_HEADER.read_text() if heap_id_header is None else heap_id_header
    fw_utils_header = FW_UTILS_HEADER.read_text() if fw_utils_header is None else fw_utils_header

    marker = f"<{FUNCTION}>:"
    start = text.find(marker)
    if start < 0:
        raise ValueError(f"{FUNCTION} is absent from the saved core disassembly")
    start = text.rfind("\n", 0, start) + 1
    end = text.find("\n000000000", start + 1)
    body = text[start:] if end < 0 else text[start:end]

    def address(line):
        match = re.match(r"\s*([0-9a-f]+):", line)
        return int(match.group(1), 16) if match else None

    lines = body.splitlines()
    map_calls = [address(line) for line in lines
                 if "RGXFwRawHeapAllocMap-0x4" in line]
    guest_branches = []
    for line in lines:
        if "jne" in line and f"<{FUNCTION}+0x2c9>" in line:
            guest_branches.append((address(line), int(line.split("jne", 1)[1].split()[0], 16)))
    premap_calls = [address(line) for line in lines
                    if "DevmemHeapSetPremapStatus-0x4" in line]
    guest_mode_checks = [address(line) for line in lines
                         if "cmp" in line and "0x1" in line and "[rax]" in line]

    if len(map_calls) != 1 or len(guest_branches) != 1 or len(premap_calls) != 2:
        raise ValueError("saved function no longer has the expected FW map/premap structure")
    branch, target = guest_branches[0]
    if not (branch < map_calls[0] < target):
        raise ValueError("Guest-mode branch does not bypass the raw-heap map call")
    if len(guest_mode_checks) != 1 or guest_mode_checks[0] <= target:
        raise ValueError("Guest branch target does not validate Guest mode before premap setup")
    if any(call <= target for call in premap_calls):
        raise ValueError("FW premap status calls are not in the Guest branch")

    register = function_body(text, "RGXRegisterDevice")
    premapped_va = text[text.find("<_GetPremappedVA.isra.0>:"):]
    premapped_va_end = premapped_va.find("\n000000000", 1)
    premapped_va = premapped_va if premapped_va_end < 0 else premapped_va[:premapped_va_end]
    if not register or not premapped_va:
        raise ValueError("saved core is missing FW_PREMAP blueprint or VA helper")
    register = " ".join(register.split())
    premapped_va = " ".join(premapped_va.split())
    if not all(fragment in register for fragment in (
            "shl rsi,0x17", "movabs rax,0xe1c0000000",
            "mov edx,0x800000", "add rsi,rax",
            "R_X86_64_PLT32 HeapCfgBlueprintInit-0x4",
            "add r13,0x1", "cmp r13,0xf")):
        raise ValueError("FW_PREMAP blueprint base/stride/size changed")
    if not all(fragment in premapped_va for fragment in (
            "R_X86_64_PLT32 PhysHeapGetDevPAddr-0x4",
            "R_X86_64_PLT32 PMR_DevPhysAddr-0x4",
            "movabs rax,0xe1c0000000", "sub r12,QWORD PTR [rbp-0x10]",
            "or r12,rax")):
        raise ValueError("premapped VA no longer preserves the PMR offset in the E1C window")

    os_supported = macro_value(config_header, "RGX_NUM_OS_SUPPORTED")
    premap0 = macro_value(heap_id_header, "PVRSRV_PHYS_HEAP_FW_PREMAP0")
    premap7 = macro_value(heap_id_header, "PVRSRV_PHYS_HEAP_FW_PREMAP7")
    guest_osid = premap7 - premap0
    if guest_osid != 7 or guest_osid >= os_supported:
        raise ValueError("FW_PREMAP7 is no longer present in the configured OSID range")
    selector = fw_utils_header.find("static INLINE PVRSRV_ERROR _SelectDevMemHeap")
    selector_end = fw_utils_header.find("\n}", selector)
    if selector < 0 or selector_end < 0:
        raise ValueError("_SelectDevMemHeap is absent from the saved Guest headers")
    selector_body = fw_utils_header[selector:selector_end]
    if not all(fragment in selector_body for fragment in (
            "case PVRSRV_PHYS_HEAP_FW_PREMAP7:",
            "ePhysHeap - PVRSRV_PHYS_HEAP_FW_PREMAP0",
            "psDevInfo->psGuestFirmwareRawHeap[ui32OSID]")):
        raise ValueError("FW_PREMAP7 no longer selects the matching Guest OSID raw heap")
    if f"cmp r13,0x{os_supported:x}" not in register:
        raise ValueError("FW_PREMAP blueprint loop count disagrees with RGX_NUM_OS_SUPPORTED")
    guest_osid_premap_address = PREMAP_BASE + guest_osid * FW_HEAP_SIZE

    return {
        "function": FUNCTION,
        "guest_driver_mode_value": 1,
        "guest_branch_target": hex(target),
        "raw_heap_map_call_in_other_branch": hex(map_calls[0]),
        "guest_branch_bypasses_raw_heap_map": True,
        "guest_branch_sets_premap_status_twice": True,
        "premap_status_calls": [hex(value) for value in premap_calls],
        "pvr_premap_blueprint_base": "0xe1c0000000",
        "pvr_premap_osid_stride_bytes": "0x800000",
        "pvr_premap_blueprint_count": os_supported,
        "fw_premap7_heap_id": premap7,
        "guest_osid7_rawheap_index": guest_osid,
        "guest_osid7_premap_blueprint_address": hex(guest_osid_premap_address),
        "guest_osid7_selects_corresponding_rawheap": True,
        "premapped_va_preserves_offset_within_physheap": True,
        "premapped_va_helper_proves_host_bar2_mapping": False,
        "host_raw_heap_map_not_called_by_guest_branch": True,
        "pvz_client_callback_usage": "not established by this function-level check",
        "scope": "one firmware-kernel-memory-context path; other PVZ client callers are not ruled out",
        "hardware_accessed": False,
    }


def main():
    try:
        print(json.dumps(verify(ASM.read_text()), indent=2))
    except (OSError, ValueError) as exc:
        raise SystemExit(str(exc))


if __name__ == "__main__":
    main()
