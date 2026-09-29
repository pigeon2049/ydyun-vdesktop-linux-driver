#!/usr/bin/env python3
"""Execute restricted reference routines in emulated RAM; no device I/O."""
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess

from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_R8,
                              UC_X86_REG_R9, UC_X86_REG_RSP, UC_X86_REG_RIP,
                              UC_X86_REG_RAX)
from pe_image import ReferencePE

ROOT = Path(__file__).resolve().parents[1]
PLATFORM, INFO, OUTPUT = 0x200000, 0x202000, 0x204000
CONFIG, HEAPS, ALLOCATION = 0x206000, 0x207000, 0x208000
STACK, STOP, ALLOC_STUB = 0x21f000, 0x220000, 0x221000


def main():
    pe = ReferencePE('/opt/MTT-driver-only/mtkm64.sys')
    machine = Uc(UC_ARCH_X86, UC_MODE_64)
    end = max(va + length for va, length, _ in pe.sections)
    machine.mem_map(pe.base, (end - pe.base + 4095) & ~4095)
    for va, length, raw in pe.sections:
        machine.mem_write(va, pe.data[raw:raw + length])
    machine.mem_map(PLATFORM, 0x30000)
    firmware_buffer = 0x400000
    machine.mem_map(firmware_buffer, 0x800000)
    machine.mem_write(0x1401380d0, struct.pack('<Q', ALLOC_STUB))
    allowed = []

    def return_value(value):
        rsp = machine.reg_read(UC_X86_REG_RSP)
        target = struct.unpack('<Q', machine.mem_read(rsp, 8))[0]
        machine.reg_write(UC_X86_REG_RAX, value)
        machine.reg_write(UC_X86_REG_RSP, rsp + 8)
        machine.reg_write(UC_X86_REG_RIP, target)

    def instruction(uc, address, size, _):
        # Model only the allocation and standard memory primitives. Firmware
        # metadata validation and address arithmetic run as original code.
        if address == ALLOC_STUB:
            assert uc.reg_read(UC_X86_REG_RDX) == 0x1610
            return_value(ALLOCATION)
        elif address in (0x140130c80, 0x140130f80):
            dest = uc.reg_read(UC_X86_REG_RCX)
            source = uc.reg_read(UC_X86_REG_RDX)
            length = uc.reg_read(UC_X86_REG_R8)
            assert length <= 0x800000
            data = (bytes([source & 255]) * length if address == 0x140130c80
                    else bytes(uc.mem_read(source, length)))
            uc.mem_write(dest, data)
            return_value(dest)
        elif not any(start <= address < finish for start, finish in allowed):
            raise AssertionError(f'Unexpected instruction at {address:#x}')

    machine.hook_add(UC_HOOK_CODE, instruction)

    def run(address, args, ranges, stop=STOP):
        allowed[:] = ranges
        machine.mem_write(STACK - 0x1000, b'\0' * 0x2000)
        machine.mem_write(STACK, struct.pack('<Q', STOP))
        for reg, value in zip((UC_X86_REG_RCX, UC_X86_REG_RDX,
                               UC_X86_REG_R8, UC_X86_REG_R9), args):
            machine.reg_write(reg, value)
        for i, value in enumerate(args[4:]):
            machine.mem_write(STACK + 0x28 + i * 8, struct.pack('<Q', value))
        machine.reg_write(UC_X86_REG_RSP, STACK)
        machine.emu_start(address, stop, timeout=1000000, count=200000)
        assert machine.reg_read(UC_X86_REG_RIP) == stop

    # Run the actual PCI dispatch until the selected initializer is reached.
    machine.mem_write(INFO, struct.pack('<HH', 0x1ed5, 0x0222))
    machine.mem_write(CONFIG, struct.pack('<QQ', 0, INFO))
    run(0x140025b88, [PLATFORM, CONFIG], [(0x140025b88, 0x140025c60)],
        stop=0x14002d73c)
    dispatch_target = machine.reg_read(UC_X86_REG_RIP)

    # Exercise every byte value used by the features callback, all lower
    # three configuration bits, and both values of the host PB capability.
    machine.mem_write(PLATFORM, b'\0' * 0x2000)
    machine.mem_write(PLATFORM + 0x44, b'\1')
    machine.mem_write(PLATFORM + 0x1cb0, struct.pack('<Q', INFO))
    cases = 0
    for pb in (0, 0x10):
        machine.mem_write(INFO + 0x10, struct.pack('<Q', pb))
        for feature in range(256):
            machine.mem_write(PLATFORM + 0x5e, bytes([feature]))
            for options in range(8):
                machine.mem_write(PLATFORM + 0x60, struct.pack('<I', options))
                machine.mem_write(OUTPUT, b'\xa5' * 40)
                run(0x14002de98, [PLATFORM, OUTPUT],
                    [(0x14002de98, 0x14002df81), (0x140026310, 0x140026322)])
                words = struct.unpack('<10I', machine.mem_read(OUTPUT, 40))
                assert words[0] == (3 if pb else 2)
                assert words[4:6] == (2 if pb else 0x48, 0x600)
                assert words[6] == ((feature & 1) << 7) | (options << 2)
                assert words[7:] == (0, 0, 0)
                assert words[8] & 7 == 0       # firmware selector
                assert (words[6] >> 9) & 1 == 0  # MMU dispatch mode
                cases += 1

    out = ROOT / 'build/firmware'
    out.mkdir(parents=True, exist_ok=True)
    library = out / 'layout.so'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                    '-shared', '-fPIC', str(ROOT / 'tests/fw_layout_oracle_wrapper.c'),
                    '-o', str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    lib.build_guest_layout.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
    lib.build_guest_layout.restype = ctypes.c_int
    image = (ctypes.c_ubyte * 0x1610)()
    machine.mem_write(CONFIG, b'\1' + b'\0' * 0xff)
    layout_cases = 0
    for va in (0, 0x1000, 0xe1c0000000, (1 << 40) - 0x800000):
        machine.mem_write(HEAPS, b'\0' * 0x1000)
        machine.mem_write(HEAPS + 0x98, struct.pack('<Q', va))
        # Guest layouts are independent of the five valid selector values.
        for selector in range(5):
            machine.mem_write(ALLOCATION, b'\xa5' * 0x1610)
            run(0x14001728c, [0, CONFIG, HEAPS, selector, OUTPUT],
                [(0x14001728c, 0x1400174f9), (0x14001ee48, 0x14001ee94),
                 (0x14001f058, 0x14001f0b0)])
            assert machine.reg_read(UC_X86_REG_RAX) & 255 == 1
            assert struct.unpack('<Q', machine.mem_read(OUTPUT, 8))[0] == ALLOCATION
            assert lib.build_guest_layout(image, va) == 0
            expected = bytes(machine.mem_read(ALLOCATION, 0x1610))
            assert bytes(image) == expected, (hex(va), selector)
            layout_cases += 1
    assert lib.build_guest_layout(image, 0xe1c0000000) == 0
    data = bytes(image)
    for va in (1, (1 << 40) - 0x1000, 1 << 40, (1 << 64) - 1):
        assert lib.build_guest_layout(image, va) < 0
        assert bytes(image) == data
    (out / 'guest-layout.bin').write_bytes(data)
    spec = importlib.util.spec_from_file_location('loader', ROOT / 'scripts/extract-firmware.py')
    loader = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loader)
    loader_results = []
    for selector, (name, start, metadata) in enumerate(loader.IMAGES):
        header = struct.unpack('<4I', pe.read(metadata, 16))
        segments = [struct.unpack('<6I', pe.read(metadata + 16 + i * 24, 24))
                    for i in range(header[2])]
        blob = pe.read(start, metadata - start)
        for mode in (0, 1):
            run(0x14001f064, [selector], [(0x14001ee48, 0x14001ee94),
                                         (0x14001f064, 0x14001f0b0)])
            assert machine.reg_read(UC_X86_REG_RAX) & 255 == 1
            machine.mem_write(firmware_buffer, bytes(0x800000))
            run(0x14001eee0,
                [firmware_buffer, 0xe1c0000000, 0x90000000,
                 firmware_buffer, mode, selector],
                [(0x14001edf0, 0x14001f028), (0x14001f0b0, 0x14001f268),
                 (0x14000807c, 0x140008084), (0x1400083b0, 0x140008402)])
            assert machine.reg_read(UC_X86_REG_RAX) & 255 == 1
            expected = bytes(machine.mem_read(firmware_buffer, 0x800000))
            candidate = loader.build_guest_loader_image(blob, segments, 0xe1c0000000, mode)
            if candidate != expected:
                first = next(i for i, (a, b) in enumerate(zip(candidate, expected)) if a != b)
                raise AssertionError((name, mode, hex(first), candidate[first], expected[first]))
            loader_results.append({'selector': selector, 'name': name, 'mmu_mode': mode,
                                   'bytes_compared': len(candidate),
                                   'sha256': hashlib.sha256(candidate).hexdigest()})
            if selector == 0 and mode == 0:
                (out / 'guest-loader-stage.bin').write_bytes(candidate)
    report = {
        'reference_sha256': pe.sha256, 'hardware_written': False,
        'pci_id': '1ed5:0222', 'dispatch_target': hex(dispatch_target),
        'features_callback': '0x14002de98', 'features_cases_passed': cases,
        'reference_firmware_selector': 0, 'reference_firmware_image': 'gen1',
        'reference_mmu_mode': 0, 'reference_mmu_levels': 3,
        'layout_function': '0x14001728c', 'layout_cases_passed': layout_cases,
        'layout_bytes_compared_per_case': 0x1610, 'invalid_inputs_rejected': 4,
        'layout_sha256': hashlib.sha256(data).hexdigest(),
        'firmware_allocation_bytes': 0x800000, 'state_bytes': 0x6ddd0,
        'queue_offset': 0x6ddd0, 'queue_gpu_va': hex(0xe1c0000000 + 0x6ddd0),
        'hardware_compatibility_verified': False,
        'loader_stage_cases': loader_results,
        'modeled_helpers': ['ExAllocatePoolWithTag', 'memset', 'memcpy'],
    }
    (ROOT / 'reports/firmware-layout-validation.json').write_text(
        json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
