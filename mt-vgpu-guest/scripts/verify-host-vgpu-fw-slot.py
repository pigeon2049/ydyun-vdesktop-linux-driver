#!/usr/bin/env python3
"""Verify saved Host slots and Guest info-page-to-PVR heap field flow.

This is a static check of saved disassembly only. It does not access PCI or
hardware, and it deliberately does not claim a Linux FW_PREMAP-to-BAR2 map.
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASM = ROOT / "reports/host-2.3-core.asm"
VGPU_HEADER = ROOT / "src/official-vgpu-2.3.0/usr/src/mtgpu-1.0.0/inc/mtgpu/vgpu/mtgpu_mdev.h"
PLATFORM_HEADER = ROOT / "src/official-vgpu-2.3.0/usr/src/mtgpu-1.0.0/inc/mtgpu/mtgpu_drv.h"
PHYS_HEAP_HEADER = ROOT / "src/official-vgpu-2.3.0/usr/src/mtgpu-1.0.0/inc/pvr/services/physheap_config.h"
PHYS_HEAP_ID_HEADER = ROOT / "src/official-vgpu-2.3.0/usr/src/mtgpu-1.0.0/inc/pvr/include/pvrsrv_memalloc_physheap.h"
FW_UTILS_HEADER = ROOT / "src/official-vgpu-2.3.0/usr/src/mtgpu-1.0.0/inc/pvr/services/rgxfwutils.h"


def function_body(text, name):
    marker = f"<{name}>:"
    start = text.find(marker)
    if start < 0:
        raise ValueError(f"{name} is absent from the saved Host disassembly")
    start = text.rfind("\n", 0, start) + 1
    end = text.find("\n000000000", start + 1)
    return text[start:] if end < 0 else text[start:end]


def require_order(body, fragments, description):
    cursor = 0
    for fragment in fragments:
        found = body.find(fragment, cursor)
        if found < 0:
            raise ValueError(f"{description}: missing or out-of-order {fragment}")
        cursor = found + len(fragment)


def vgpu_info_layout_offsets(vgpu_header):
    maximum = re.search(r"^#define\s+MAX_VM_MEM_SEGMENT\s+(\d+)\s*$",
                        vgpu_header, re.MULTILINE)
    enum = re.search(r"typedef enum \{([^}]+)\}\s*DEVICE_TYPE;", vgpu_header, re.DOTALL)
    if not maximum or not enum:
        raise ValueError("cannot derive the legacy vgpu_info layout from its header")
    enum_items = re.findall(r"\bDEVICE_TYPE_[A-Z]+\b", enum.group(1))
    if enum_items != ["DEVICE_TYPE_VGPU", "DEVICE_TYPE_VVPU", "DEVICE_TYPE_MAX"]:
        raise ValueError("unexpected vgpu_info device-type array layout")
    max_segments = int(maximum.group(1))
    if max_segments != 64:
        raise ValueError("unexpected vgpu_info segment-array length")
    start = vgpu_header.find("struct vgpu_info {")
    end = vgpu_header.find("\n};", start)
    if start < 0 or end < 0:
        raise ValueError("legacy struct vgpu_info is absent")
    info = vgpu_header[start:end]
    fields = ["volatile u32 magic;", "volatile u32 version;", "volatile u32 osid;",
              "volatile u64 flag;", "volatile u64 vm_mem_size;",
              "volatile u64 vm_bar2_actual_mem_size;",
              "struct vm_segment_info segment_info[DEVICE_TYPE_MAX];",
              "volatile u64 fw_heap_base;", "volatile u32 fw_heap_size;"]
    cursor = 0
    for field in fields:
        found = info.find(field, cursor)
        if found < 0:
            raise ValueError(f"unexpected vgpu_info field order: {field}")
        cursor = found + len(field)

    # C x86-64 layout: the leading three u32s align the u64 flag to 0x10;
    # each segment entry is 8+4+4 padding+64*16 bytes.
    vm_segment_info_size = 8 + 4 + 4 + max_segments * 16
    vvpu_segment_size_offset = 0x28 + vm_segment_info_size
    fw_heap_base_offset = 0x28 + len(enum_items[:-1]) * vm_segment_info_size
    fw_heap_base_offset = (fw_heap_base_offset + 7) & ~7
    return {
        "vvpu_segment_size": vvpu_segment_size_offset,
        "fw_heap_base": fw_heap_base_offset,
        "fw_heap_size": fw_heap_base_offset + 8,
    }


def phys_heap_scard_base_offset(header):
    start = header.find("typedef struct _PHYS_HEAP_CONFIG_")
    end = header.find("} PHYS_HEAP_CONFIG;", start)
    if start < 0 or end < 0:
        raise ValueError("public PHYS_HEAP_CONFIG definition is absent")
    config = header[start:end]
    fields = ["PHYS_HEAP_TYPE eType;", "IMG_CHAR* pszPDumpMemspaceName;",
              "PHYS_HEAP_FUNCTIONS* psMemFuncs;", "IMG_CPU_PHYADDR sStartAddr;",
              "IMG_DEV_PHYADDR sCardBase;", "IMG_UINT64 uiSize;"]
    cursor = 0
    for field in fields:
        found = re.search(r"\s+".join(re.escape(part) for part in field.split()),
                          config[cursor:])
        if not found:
            raise ValueError(f"unexpected PHYS_HEAP_CONFIG field order: {field}")
        cursor += found.end()

    # x86-64 ABI: 32-bit enum, then aligned pointers/addresses of 8 bytes.
    return 0x20


def phys_heap_fw_main_usage_value(usage_header, id_header):
    heap_id = re.search(r"^#define\s+PVRSRV_PHYS_HEAP_FW_MAIN\s+(\d+)U\b",
                        id_header, re.MULTILINE)
    usage = re.search(r"^#define\s+PHYS_HEAP_USAGE_FW_MAIN\s+\(1<<PVRSRV_PHYS_HEAP_FW_MAIN\)\s*$",
                      usage_header, re.MULTILINE)
    if not heap_id or not usage:
        raise ValueError("public FW_MAIN physical-heap usage definition is absent")
    return 1 << int(heap_id.group(1))


def phys_heap_fw_config_relationship(usage_header, id_header, fw_utils_header):
    """Verify FW_CONFIG is a FW_MAIN child routed to its config subheap."""
    heap_id = re.search(
        r"^#define\s+PVRSRV_PHYS_HEAP_FW_CONFIG\s+(\d+)U\s+/\*\s*subheap of FW_MAIN,",
        id_header, re.MULTILINE)
    usage = re.search(
        r"^#define\s+PHYS_HEAP_USAGE_FW_CONFIG\s+\(1<<PVRSRV_PHYS_HEAP_FW_CONFIG\)\s*$",
        usage_header, re.MULTILINE)
    if not heap_id or not usage:
        raise ValueError("public FW_CONFIG heap is no longer declared as a subheap of FW_MAIN")
    selector = fw_utils_header.find("static INLINE PVRSRV_ERROR _SelectDevMemHeap")
    selector_end = fw_utils_header.find("\n}", selector)
    if selector < 0 or selector_end < 0:
        raise ValueError("_SelectDevMemHeap is absent from the saved Guest headers")
    selector_body = fw_utils_header[selector:selector_end]
    if not re.search(
            r"case\s+PVRSRV_PHYS_HEAP_FW_CONFIG\s*:\s*\{[^}]*"
            r"\*ppsFwHeap\s*=\s*psDevInfo->psFirmwareConfigHeap\s*;",
            selector_body, re.DOTALL):
        raise ValueError("FW_CONFIG no longer routes allocations to psFirmwareConfigHeap")
    heap_index = int(heap_id.group(1))
    return {
        "heap_index": heap_index,
        "usage_value": 1 << heap_index,
        "routes_to_config_heap": True,
    }


def platform_data_fw_heap_card_base_offset(header):
    """Derive the x86-64 public platform-data offset from declared fields."""
    vz_start = header.find("struct mtgpu_vz_data {")
    vz_end = header.find("\n};", vz_start)
    pdata_start = header.find("struct mtgpu_platform_data {")
    pdata_end = header.find("\n};", pdata_start)
    if min(vz_start, pdata_start) < 0 or min(vz_end, pdata_end) < 0:
        raise ValueError("public MTGPU VZ/platform-data definitions are absent")

    def struct_body(start, end):
        body = header[start:end]
        body = re.sub(r"/\*.*?\*/", "", body, flags=re.DOTALL)
        return re.sub(r"//[^\n]*", "", body)

    def declarations(body):
        found = re.findall(
            r"^\s*((?:struct\s+\w+|\w+))\s*(\*+)?\s*(\w+)"
            r"\s*(?:\[[^\]]+\])?\s*;", body, re.MULTILINE)
        return [(" ".join(type_name.split()) + (" *" if pointers else ""), name)
                for type_name, pointers, name in found]

    vz = declarations(struct_body(vz_start, vz_end))
    expected_vz = [("resource_size_t", "mmu_heap_base"),
                   ("resource_size_t", "mmu_heap_size"),
                   ("resource_size_t", "mmu_heap_card_base"),
                   ("resource_size_t", "fw_heap_card_base")]
    if vz[:4] != expected_vz:
        raise ValueError("unexpected mtgpu_vz_data field order/types")

    pdata = declarations(struct_body(pdata_start, pdata_end))
    expected_prefix = [
        ("u32", "primary_core_id"), ("int", "mem_mode"),
        ("resource_size_t", "pcie_memory_base"),
        ("resource_size_t", "gpu_memory_base"),
        ("resource_size_t", "gpu_memory_size"),
        ("char *", "mtgpu_dma_tx_chan_name"),
        ("char *", "mtgpu_dma_rx_chan_name"),
        ("struct mtgpu_segment_info *", "segment_info"),
        ("struct mtgpu_vz_data", "vz_data"),
    ]
    if pdata[:len(expected_prefix)] != expected_prefix:
        raise ValueError("unexpected mtgpu_platform_data prefix field order/types")

    # Linux x86-64 ABI: u32/int align to 4, resource_size_t and pointers to 8.
    sizes = {"u32": (4, 4), "int": (4, 4), "resource_size_t": (8, 8),
             "char *": (8, 8), "struct mtgpu_segment_info *": (8, 8)}
    offset = 0
    for type_name, _name in expected_prefix[:-1]:
        size, alignment = sizes[type_name]
        offset = (offset + alignment - 1) & ~(alignment - 1)
        offset += size
    vz_data_offset = offset
    fw_heap_card_base_nested_offset = 3 * 8
    return vz_data_offset + fw_heap_card_base_nested_offset


def verify(text):
    alloc = "\n".join(" ".join(line.split()) for line in
                      function_body(text, "vgpu_alloc_osid").splitlines())
    info = "\n".join(" ".join(line.split()) for line in
                     function_body(text, "vgpu_device_info_init").splitlines())
    vpu_alloc = "\n".join(" ".join(line.split()) for line in
                          function_body(text, "vgpu_alloc_vram").splitlines())
    vpu_size = "\n".join(" ".join(line.split()) for line in
                         function_body(text, "vgpu_calculate_vpu_mem_size").splitlines())
    ring = "\n".join(" ".join(line.split()) for line in
                     function_body(text, "mtgpu_mdev_init_fw_heap_ring_buffer").splitlines())
    translator = "\n".join(" ".join(line.split()) for line in
                            function_body(text, "mtgpu_mdev_gpa_to_hpa.part.0").splitlines())
    info_page = "\n".join(" ".join(line.split()) for line in
                           function_body(text, "vgpu_access_pci_bar1_region").splitlines())
    guest_fixup = "\n".join(" ".join(line.split()) for line in
                             function_body(text, "mtgpu_device_memory_fixup").splitlines())
    vz_init = "\n".join(" ".join(line.split()) for line in
                         function_body(text, "mtgpu_platform_data_vz_init").splitlines())
    sysdev_init = "\n".join(" ".join(line.split()) for line in
                         function_body(text, "SysDevInit").splitlines())
    mpc_card_base = "\n".join(" ".join(line.split()) for line in
                              function_body(text, "mtgpu_vgpu_get_mpc_mem_card_base").splitlines())

    require_order(alloc, [
        "mov edi,0xf0",
        "R_X86_64_PLT32 os_kzalloc-0x4",
        "mov QWORD PTR [r8+0xa90],rax",
    ], "per-vGPU record-table allocation")

    require_order(info, [
        "mov rax,QWORD PTR [rbx+0x48]",
        "add eax,0x1",
        "mov edx,DWORD PTR [r8+0x1130]",
        "sub eax,DWORD PTR [r8+0x112c]",
        "shl edx,0x17",
        "shl eax,0x17",
        "add rax,QWORD PTR [r8+0x10e0]",
        "add edx,0x100000",
        "add rdx,0x900000",
        "imul rdx,r9",
        "add rdx,rax",
        "mov QWORD PTR [rsi+0x30],rdx",
        "mov QWORD PTR [rax+0x50],0x800000",
        "mov BYTE PTR [rax+0x5c],0x1",
    ], "per-vGPU firmware record")
    require_order(mpc_card_base, [
        "mov eax,DWORD PTR [rdi+0x1130]",
        "shl eax,0x17",
        "add eax,0x100000",
        "add rax,0x900000",
        "imul rax,rsi",
        "add rax,QWORD PTR [rdi+0x10e0]",
    ], "GPU card physical base plus per-MPC stride")

    require_order(ring, [
        "mov esi,0x2",
        "R_X86_64_PLT32 os_pci_resource_start-0x4",
        "mov esi,0x800000",
        "add rax,r12",
        "add eax,ebx",
        "shl eax,0x17",
        "add rdi,rax",
        "R_X86_64_PLT32 os_ioremap-0x4",
    ], "BAR2 firmware-slot initialization")

    require_order(translator, [
        "add r12,0x30",
        "cmp eax,0x5",
    ], "per-vGPU record walk")
    if "R_X86_64_PLT32 os_pci_resource_start-0x4" not in ring:
        raise ValueError("BAR2 firmware-slot initialization does not query PCI resource 2")

    require_order(info_page, [
        "mov edi,0x8a0",
        "mov DWORD PTR [rax],0xaa557491",
        "mov DWORD PTR [rax+0x4],0x1",
        "mov DWORD PTR [r13+0x8],eax",
    ], "Linux Host version-1 vGPU info-page response")
    require_order(info_page, [
        "mov rax,QWORD PTR [r15+0xa90]",
        "mov rax,QWORD PTR [rax+0x30]",
        "mov QWORD PTR [r13+0x848],rax",
        "mov rax,QWORD PTR [r15+0xa90]",
        "mov rax,QWORD PTR [rax+0x50]",
        "mov DWORD PTR [r13+0x850],eax",
    ], "Linux Host version-1 firmware heap fields")
    require_order(info_page, [
        "cmp BYTE PTR [rax+0x2c],0x0",
        "mov rax,QWORD PTR [r13+0x10]",
        "or rax,0x1",
        "mov QWORD PTR [r13+0x10],rax",
        "mov eax,DWORD PTR [r15+0xa80]",
        "mov DWORD PTR [r13+0x440],eax",
        "mov rax,QWORD PTR [r15+0xa60]",
        "mov QWORD PTR [r13+0x438],rax",
    ], "Linux Host VPU-flagged VVPU segment fields")
    require_order(info, [
        "mov rsi,QWORD PTR [rbx+0xa78]",
        "mov QWORD PTR [rdx+0x18],rsi",
        "mov rsi,QWORD PTR [rbx+0xa60]",
        "mov QWORD PTR [rdx+0x20],rsi",
    ], "Linux Host VM/BAR2 information fields")
    require_order(vpu_alloc, [
        "mov esi,DWORD PTR [rbx+0x20e50]",
        "lea rcx,[r12+0xa60]",
        "lea rdx,[r12+0xa88]",
        "call 4b340 <vgpu_calculate_vpu_mem_size>",
    ], "Linux Host VPU-size calculation outputs")
    require_order(vpu_size, [
        "mov r14,rdx",
        "mov rbx,rcx",
        "mov QWORD PTR [r14],rdx",
        "mov QWORD PTR [rbx],r12",
    ], "Linux Host VPU-size calculation output stores")

    info_offsets = vgpu_info_layout_offsets(VGPU_HEADER.read_text())
    vvpu_segment_size_offset = info_offsets["vvpu_segment_size"]
    fw_heap_base_offset = info_offsets["fw_heap_base"]
    fw_heap_size_offset = info_offsets["fw_heap_size"]
    if fw_heap_base_offset != 0x848:
        raise ValueError(f"legacy vgpu_info fw_heap_base moved to {fw_heap_base_offset:#x}")
    if fw_heap_size_offset != 0x850:
        raise ValueError(f"legacy vgpu_info fw_heap_size moved to {fw_heap_size_offset:#x}")
    if vvpu_segment_size_offset != 0x438:
        raise ValueError(f"legacy vgpu_info VVPU segment size moved to {vvpu_segment_size_offset:#x}")
    require_order(guest_fixup, [
        "mov esi,0x2",
        "R_X86_64_PLT32 os_pci_resource_start-0x4",
        "mov QWORD PTR [r12+0x78],rax",
    ], "Guest BAR2 resource-base setup")
    require_order(guest_fixup, [
        "mov rsi,QWORD PTR [r12+0x78]",
        "mov rax,QWORD PTR [rdx+0x10]",
        "test al,0x1",
        "mov rcx,QWORD PTR [r12+0xa0]",
        "mov eax,DWORD PTR [rdx+0x850]",
        "add rax,rsi",
        "add rax,rcx",
        "mov QWORD PTR [r12+0xd8],rax",
        "mov rax,QWORD PTR [rdx+0x20]",
        "mov edx,DWORD PTR [rdx+0x850]",
        "sub rax,rdx",
        "sub rax,rcx",
    ], "Guest legacy information-page memory-layout fixup")
    require_order(guest_fixup, [
        "test al,0x1",
        "jne 38d57",
    ], "Guest VPU-flag conditional")
    require_order(guest_fixup, [
        f"mov rcx,QWORD PTR [rdx+{vvpu_segment_size_offset:#x}]",
        "mov QWORD PTR [r12+0xa0],rcx",
        "jmp 38bbd",
    ], "Guest VVPU-segment branch for fw_heap_card_base")
    require_order(guest_fixup, [
        "mov rax,QWORD PTR [rdx+0x20]",
        "mov edx,DWORD PTR [rdx+0x850]",
        "sub rax,rdx",
        "sub rax,rcx",
    ], "Guest VVPU size remains an input to VPU memory-range math")
    require_order(guest_fixup, [
        "mov edx,DWORD PTR [rax+0x850]",
        "mov rax,QWORD PTR [r12+0xd8]",
        "sub rax,QWORD PTR [r12+0x78]",
        "sub rax,rdx",
        "mov QWORD PTR [r12+0x10e0],rax",
    ], "Guest fw_heap_card_base expression")
    require_order(vz_init, [
        "R_X86_64_PLT32 mtgpu_get_driver_mode-0x4",
        "test eax,eax",
        "je 380de",
        "mov rax,QWORD PTR [r12+0x1138]",
        "mov QWORD PTR [rbx+0x70],rax",
        "mov rax,QWORD PTR [r12+0x10e0]",
        "mov QWORD PTR [rbx+0x50],rax",
    ], "Guest fw_heap_card_base platform-data copy")
    platform_header = PLATFORM_HEADER.read_text()
    platform_fw_heap_offset = platform_data_fw_heap_card_base_offset(platform_header)
    if platform_fw_heap_offset != 0x50:
        raise ValueError(f"mtgpu_platform_data.vz_data.fw_heap_card_base moved to {platform_fw_heap_offset:#x}")
    phys_heap_header = PHYS_HEAP_HEADER.read_text()
    scard_base_offset = phys_heap_scard_base_offset(phys_heap_header)
    fw_main_usage = phys_heap_fw_main_usage_value(
        phys_heap_header, PHYS_HEAP_ID_HEADER.read_text())
    fw_config = phys_heap_fw_config_relationship(
        phys_heap_header, PHYS_HEAP_ID_HEADER.read_text(), FW_UTILS_HEADER.read_text())
    if scard_base_offset != 0x20:
        raise ValueError(f"PHYS_HEAP_CONFIG.sCardBase moved to {scard_base_offset:#x}")
    if fw_main_usage != 0x10:
        raise ValueError(f"PHYS_HEAP_USAGE_FW_MAIN changed to {fw_main_usage:#x}")
    if fw_config["heap_index"] != 8 or fw_config["usage_value"] != 0x100:
        raise ValueError("PHYS_HEAP_USAGE_FW_CONFIG changed from the expected subheap ABI")
    require_order(sysdev_init, [
        "mov rcx,QWORD PTR [rax+0x50]",
        "mov QWORD PTR [rbp-0x48],rcx",
        "mov QWORD PTR [r12+0x28],rax",
        "mov rcx,QWORD PTR [rbp-0x48]",
        "mov QWORD PTR [r12+0x20],rcx",
        "mov DWORD PTR [r12+0x38],0x10",
    ], "Guest VZ fw_heap_card_base to PVR PhysHeap config")

    return {
        "host_vgpu_slot_formula": "BAR2-relative base + mpc_id*(osid_count*8MiB + 10MiB) + (guest_osid + 1 - osid_start)*8MiB",
        "host_gpu_card_base_formula": "gpu_mem_card_base + mpc_id*(osid_count*8MiB + 10MiB) + (guest_osid + 1 - osid_start)*8MiB",
        "host_fw_heap_base_is_gpu_card_address": True,
        "private_record_table_allocation_size": "0xf0",
        "private_record_offset": "0x30 (record 1 in a 0x30-byte-stride table)",
        "private_record_size": "0x800000",
        "private_record_marked_valid": True,
        "host_ring_init_maps_each_slot_size": "0x800000",
        "host_ring_init_uses_pci_bar": 2,
        "host_translation_walks_five_records": True,
        "linux_host_vgpu_info_page_version": 1,
        "linux_host_vgpu_info_page_magic": "0xaa557491",
        "linux_host_fw_heap_base_offset": "0x848",
        "linux_host_fw_heap_size_offset": "0x850",
        "linux_host_vvpu_segment_size_source": "vgpu_calculate_vpu_mem_size output → per-vGPU state+0xa60",
        "linux_host_vm_bar2_actual_size_source": "vgpu_calculate_vpu_mem_size output → per-vGPU state+0xa60",
        "linux_host_vvpu_segment_size_is_size_field": True,
        "guest_vpu_flag_uses_vvpu_size_as_fw_heap_card_base": True,
        "candidate_guest_fw_heap_base_offset": "0x848",
        "candidate_guest_fw_heap_base_patch_site": "mtgpu_platform_data_vz_init+0x8d, after VPU range calculations",
        "guest_vgpu_info_pointer_loaded_before_fw_heap_card_base_copy": True,
        "guest_vvpu_size_remains_used_for_vpu_range_math": True,
        "linux_host_vvpu_segment_count_offset": "0x440",
        "guest_info_page_fw_heap_size_offset": hex(fw_heap_size_offset),
        "guest_info_page_fw_heap_base_offset": hex(fw_heap_base_offset),
        "guest_vgpu_vvpu_segment_size_offset": f"{vvpu_segment_size_offset:#x} (segment_info[DEVICE_TYPE_VVPU].size)",
        "guest_fw_heap_size_cancels_from_fw_heap_card_base_expression": True,
        "guest_vz_fw_heap_card_base_uses_conditional_segment_value": True,
        "guest_fixup_reads_fw_heap_size_before_writing_vz_fw_heap_card_base": True,
        "platform_data_fw_heap_card_base_offset": f"{platform_fw_heap_offset:#x} (mtgpu_platform_data.vz_data.fw_heap_card_base)",
        "guest_vz_fw_heap_card_base_feeds_phys_heap_config_scardbase": True,
        "pvr_phys_heap_config_scard_base_offset": hex(scard_base_offset),
        "pvr_phys_heap_config_usage": "FW_MAIN (PHYS_HEAP_USAGE_FW_MAIN=0x10)",
        "pvr_fw_main_phys_heap_size": "0x800000",
        "pvr_fw_config_usage": "FW_CONFIG (PHYS_HEAP_USAGE_FW_CONFIG=0x100)",
        "pvr_fw_config_declared_as_subheap_of_fw_main": True,
        "pvr_fw_config_routes_to_config_child_heap": fw_config["routes_to_config_heap"],
        "linux_fw_premap_to_host_bar2_translation_proven": False,
        "scope": "Host slot metadata plus Guest legacy-info-page field flow; no Guest PVR-to-BAR2 translation claim",
        "hardware_accessed": False,
    }


def main():
    try:
        print(json.dumps(verify(ASM.read_text()), ensure_ascii=False, indent=2))
    except (OSError, ValueError) as exc:
        raise SystemExit(str(exc))


if __name__ == "__main__":
    main()
