#!/usr/bin/env python3
"""Compare C encoders against original Windows instructions in Unicorn; no hardware writes."""
import ctypes
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64
from unicorn.x86_const import UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_R8, UC_X86_REG_RSP
from pe_image import ReferencePE

ROOT = Path(__file__).resolve().parents[1]


def main():
    pe = ReferencePE("/opt/MTT-driver-only/mtkm64.sys")
    out = ROOT / "build/mmu"
    out.mkdir(parents=True, exist_ok=True)
    library = out / "encoders.so"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-shared", "-fPIC",
                    str(ROOT / "tests/mmu_oracle_wrapper.c"), "-o", str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    rng = random.Random(275)
    cases = [(pa, flags) for pa in (0, 0x1000, 0x605000000, 0x771fef000, 0xfffffff000)
             for flags in range(32)]
    cases += [(rng.randrange(1 << 28) << 12, rng.randrange(32)) for _ in range(500)]
    result = {"reference_sha256": pe.sha256, "hardware_written": False, "encoders": {}}
    # Exact leaf routines only. No Windows entry point, import, or I/O is executed.
    for name, address, size, width in [("pc", 0x14002a82c, 44, 4),
                                       ("pd", 0x14002a878, 45, 8),
                                       ("pt", 0x14002a8d8, 59, 8)]:
        fn = getattr(lib, "encode_" + name)
        fn.argtypes = [ctypes.c_uint64, ctypes.c_uint64]
        fn.restype = ctypes.c_uint64
        code = pe.read(address, size)
        machine = Uc(UC_ARCH_X86, UC_MODE_64)
        machine.mem_map(address & ~4095, 8192)
        machine.mem_write(address, code)
        machine.mem_map(0x200000, 0x4000)
        stop = 0x203000
        for pa, flags in cases:
            machine.mem_write(0x200000, struct.pack("<Q", flags))
            machine.mem_write(0x201000, b"\0" * 8)
            machine.mem_write(0x202000, struct.pack("<Q", stop))
            for reg, value in [(UC_X86_REG_RCX, pa), (UC_X86_REG_RDX, 0x200000),
                               (UC_X86_REG_R8, 0x201000), (UC_X86_REG_RSP, 0x202000)]:
                machine.reg_write(reg, value)
            machine.emu_start(address, stop, timeout=100000, count=100)
            expected = int.from_bytes(machine.mem_read(0x201000, width), "little")
            actual = fn(pa, flags)
            if actual != expected:
                raise AssertionError((name, hex(pa), flags, hex(actual), hex(expected)))
        result["encoders"][name] = {"address": hex(address), "cases_passed": len(cases),
                                    "instruction_sha256": hashlib.sha256(code).hexdigest()}

    # Prepare the actual 8 MiB mapping using measured addresses. This is a
    # candidate layout only: its device ranges have NOT been reserved/uploaded.
    info = json.loads((ROOT / "reports/device-info.json").read_text())
    # 140026390/140027bec put normal private allocations in the 24 MiB
    # pool after the first 8 MiB of normal memory. Do not allocate at the
    # start of the raw host segment; that lies outside this private pool.
    tables_pa = int(info["segments"][0]["address"], 16) + 0x800000
    fw_pa = int(info["segments"][2]["address"], 16)
    fw_va = struct.unpack("<Q", pe.read(0x141030d90 + 0x98, 8))[0]
    image = (ctypes.c_uint64 * (0x6000 // 8))()
    lib.build.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint64, ctypes.c_uint64]
    lib.build.restype = ctypes.c_int
    assert lib.build(image, tables_pa, fw_pa, fw_va) == 0
    data = bytes(image)
    # Independently walk every PTE and both directory levels, including unmapped
    # entries, rather than comparing a second copy of the builder algorithm.
    pc = struct.unpack_from("<1024I", data)
    root_index = fw_va // (1 << 30)
    assert sum(bool(v) for v in pc) == 1
    pd_pa = (pc[root_index] & 0xfffffff0) << 8
    assert pd_pa == tables_pa + 4096
    pd = struct.unpack_from("<512Q", data, pd_pa - tables_pa)
    assert sum(bool(v) for v in pd) == 4
    for n in range(2048):
        address = fw_va + n * 4096
        directory = (address // (1 << 21)) % 512
        table_pa = pd[directory] & 0xffffffffe0
        leaf_offset = table_pa - tables_pa + ((address // 4096) % 512) * 8
        entry = struct.unpack_from("<Q", data, leaf_offset)[0]
        assert entry & 0xfffffff000 == fw_pa + n * 4096
        assert entry & (1 << 62) and entry & 1
    original = bytes(image)
    for args in [(tables_pa + 1, fw_pa, fw_va), (tables_pa, fw_pa, fw_va + 4096),
                 (tables_pa, 1 << 40, fw_va), (fw_pa, fw_pa, fw_va),
                 (tables_pa, fw_pa, (1 << 40) - 0x200000)]:
        assert lib.build(image, *args) < 0
        assert bytes(image) == original, "Rejected input modified page tables"
    (out / "firmware-page-tables.bin").write_bytes(data)
    result["firmware_mapping"] = {"gpu_va": hex(fw_va), "gpu_physical": hex(fw_pa),
        "candidate_table_physical": hex(tables_pa), "mapped_pages_verified": 2048,
        "candidate_pool": "normal private pool (24 MiB, after first 8 MiB)",
        "invalid_inputs_rejected": 5, "sha256": hashlib.sha256(data).hexdigest(),
        "uploaded": False, "device_ranges_reserved": False}
    (ROOT / "reports/mmu-validation.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
