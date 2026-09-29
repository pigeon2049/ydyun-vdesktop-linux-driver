#!/usr/bin/env python3
"""Reconstruct Guest heap reservations and state from original CPU instructions."""
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import random
import struct
import subprocess
from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
CONFIG, MMU, HEAPS = 0x200000, 0x201000, 0x202000
DEVICE, GPU, PB, PAIRS, WORKER, CONTEXT, ALLOC = (
    0x210000, 0x214000, 0x21a000, 0x21b000, 0x21c000, 0x21d000, 0x21e000)
LAYOUT, IMAGE = 0x220000, 0x400000


class Heap(ctypes.Structure):
    _fields_ = [(x, ctypes.c_uint64) for x in ('base', 'size', 'reserved_base', 'reserved_size')]


class Resource(ctypes.Structure):
    _fields_ = [('va', ctypes.c_uint64), ('size', ctypes.c_uint32), ('heap', ctypes.c_uint32)]


class Plan(ctypes.Structure):
    _fields_ = [('heaps', Heap * 22), ('resources', Resource * 13)]


class StateResources(ctypes.Structure):
    _fields_ = [('pb_va', ctypes.c_uint64 * 2)] + [
        (x, ctypes.c_uint64) for x in ('fence_gpu_pa', 'yuv_va', 'heap1_va', 'heap2_va',
                                      'heap7_va', 'heap8_va', 'heap10_va', 'heap9_va')
    ] + [('dm_kill_offset', ctypes.c_uint32)]


def main():
    oracle = ReferenceOracle()
    oracle.uc.mem_write(CONFIG, b'\1')
    oracle.put64(0x1401380d0, 0x2f8030)  # ExAllocatePoolWithTag
    oracle.hooks[0x2f8030] = lambda: oracle.ret(oracle.allocate(oracle.arg(1)))
    assert oracle.run(0x14002a34c, [MMU, 0], [(0x14002a0d0, 0x14002a4ec)]) == 0
    descriptors = oracle.get64(MMU + 8)
    table = oracle.get64(descriptors + 8)
    reservation_granularity = oracle.get64(table + 0x10)
    assert reservation_granularity == 0x200000
    oracle.import_noop(0x1401383b0, 0x2f8000)  # KeInitializeSpinLock
    oracle.import_noop(0x140138250, 0x2f8010)  # KeAcquireInStackQueuedSpinLock
    oracle.import_noop(0x140138258, 0x2f8020)  # KeReleaseInStackQueuedSpinLock
    ranges = [(0x14001ccd8, 0x14001d443), (0x140040760, 0x140041cd0),
              (0x140130c50, 0x140130c65)]
    result = oracle.run(0x14001ccd8, [CONFIG, MMU, 0x1ef9, HEAPS], ranges)
    assert result == 0, hex(result)
    data = bytes(oracle.uc.mem_read(HEAPS, 0x9d0))
    resources = []
    for index in range(13):
        name, size, requested, va, extra, kind = struct.unpack_from('<QIIQII', data, 0x218 + index * 32)
        resources.append({'index': index, 'size': size, 'gpu_va': hex(va), 'heap': kind})
    out = ROOT / 'build/firmware'
    out.mkdir(parents=True, exist_ok=True)
    library = out / 'state.so'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                    '-shared', '-fPIC', str(ROOT / 'tests/fw_state_oracle_wrapper.c'),
                    '-o', str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    lib.plan_heaps.argtypes = [ctypes.POINTER(Plan)]
    lib.apply_guest_state.argtypes = [ctypes.c_void_p, ctypes.c_uint32,
                                     ctypes.POINTER(StateResources)]
    lib.apply_guest_state.restype = ctypes.c_int
    lib.build_layout.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
    plan = Plan()
    lib.plan_heaps(ctypes.byref(plan))
    for i, heap in enumerate(plan.heaps):
        _, base, size = struct.unpack_from('<3Q', data, i * 24)
        assert (heap.base, heap.size) == (base, size)
        _, available_base, available_size = struct.unpack_from('<3Q', data, 0x4f8 + i * 24)
        assert available_base == base
        assert heap.reserved_size == size - available_size
        if heap.reserved_size:
            assert heap.reserved_base == struct.unpack_from('<Q', data, 0x720 + i * 24)[0]
    for i, resource in enumerate(plan.resources):
        assert (resource.size, resource.va, resource.heap) == (
            resources[i]['size'], int(resources[i]['gpu_va'], 16), resources[i]['heap'])
    info = (ROOT / 'reports/device-info.bin').read_bytes()
    assert struct.unpack_from('<Q', info, 0x10)[0] & 0x10
    host_pb_va = struct.unpack_from('<Q', info, 0xc88)[0]
    assert plan.resources[0].va == host_pb_va
    assert plan.resources[0].size == struct.unpack_from('<I', info, 0xc98)[0]

    # Execute the actual host-info -> private-memory-pool transformation.
    platform, host_info, os_config = GPU + 8, 0x224000, 0x226000
    oracle.uc.mem_write(host_info, info)
    oracle.put64(platform + 0x1cb0, host_info)
    oracle.put64(platform + 0x10, os_config)
    oracle.put64(os_config + 0x28, 0x40000000)  # Affects CPU heap, not private pools.
    oracle.put64(platform + 0x400, 0x800000000)
    oracle.put64(platform + 0x408, 0x400000000)
    assert oracle.run(0x140026390, [platform], [(0x140026390, 0x1400265cb)]) == 0
    private_pools = {}
    for name, offset in [('normal', 0x490), ('pb', 0x500), ('firmware', 0x4e8)]:
        bar_offset, size, gpu_pa = struct.unpack('<3Q', oracle.uc.mem_read(platform + offset, 24))
        # Independently check the guest CPU BAR -> GPU translation routine.
        translated = oracle.run(0x140027ab4, [platform, 0x800000000 + bar_offset],
                                [(0x140027ab4, 0x140027bec)])
        assert translated == gpu_pa
        private_pools[name] = {'bar_offset': hex(bar_offset), 'size': size, 'gpu_pa': hex(gpu_pa)}

    layout = (ctypes.c_ubyte * 0x1610)()
    assert lib.build_layout(layout, plan.heaps[6].base) == 0
    oracle.uc.mem_write(LAYOUT, bytes(layout))
    oracle.put64(DEVICE, CONFIG)
    oracle.put64(DEVICE + 0x68, PB)
    oracle.put64(DEVICE + 0x80, 0x141107100)  # Actual feature table, PB index 0.
    oracle.put64(DEVICE + 0x418, GPU)
    oracle.put64(DEVICE + 0x1020, WORKER)
    oracle.uc.mem_write(GPU + 0x4c, b'\1')
    oracle.put64(GPU + 0x9f8, 0x14002db90)
    oracle.put64(PB + 0x10, PAIRS)
    oracle.put64(CONTEXT, ALLOC)
    oracle.put64(CONTEXT + 8, IMAGE)
    oracle.put64(ALLOC, IMAGE)
    state_ranges = [(0x140015f78, 0x140016215), (0x14001e32c, 0x14001e347),
                    (0x1400227e4, 0x140022825), (0x14002db90, 0x14002de95),
                    (0x140130c10, 0x140130c12)]
    spec = importlib.util.spec_from_file_location('loader', ROOT / 'scripts/extract-firmware.py')
    loader = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loader)
    _, start, metadata = loader.IMAGES[0]
    header = struct.unpack('<4I', oracle.pe.read(metadata, 16))
    segments = [struct.unpack('<6I', oracle.pe.read(metadata + 16 + i * 24, 24))
                for i in range(header[2])]
    baseline = loader.build_guest_loader_image(oracle.pe.read(start, metadata - start),
                                               segments, plan.heaps[6].base)
    assert len(baseline) == 0x800000
    candidate = (ctypes.c_ubyte * len(baseline))()
    rng = random.Random(275)
    inputs = []
    actual = StateResources()
    actual.pb_va[:] = (host_pb_va, host_pb_va + 0x100)
    actual.yuv_va = plan.resources[10].va
    for index in (1, 2, 7, 8, 10, 9):
        setattr(actual, f'heap{index}_va', plan.heaps[index].base)
    actual.dm_kill_offset = plan.resources[11].va - plan.heaps[2].base
    inputs.append(actual)
    for _ in range(32):
        r = StateResources()
        r.pb_va[:] = [rng.randrange(1, 1 << 32) << 8 for _ in range(2)]
        for name, _ in StateResources._fields_[1:-1]:
            setattr(r, name, rng.randrange(1 << 28) << 12)
        r.dm_kill_offset = rng.randrange(1 << 32)
        inputs.append(r)
    state_hash = None
    for i, r in enumerate(inputs):
        oracle.uc.mem_write(IMAGE, baseline)
        oracle.uc.mem_write(GPU + 0x1e38, bytes(0x2000))
        oracle.uc.mem_write(PAIRS, bytes(r.pb_va))
        oracle.put64(DEVICE + 0x1048, r.fence_gpu_pa)
        oracle.put64(HEAPS + 0x368, r.yuv_va)
        for index in (1, 2, 7, 8, 10, 9):
            oracle.put64(HEAPS + index * 24 + 8, getattr(r, f'heap{index}_va'))
        oracle.put64(HEAPS + 0x388, r.heap2_va + r.dm_kill_offset)
        oracle.run(0x140015f78, [DEVICE, LAYOUT, HEAPS, CONTEXT], state_ranges)
        expected = bytes(oracle.uc.mem_read(IMAGE, len(baseline)))
        ctypes.memmove(candidate, baseline, len(baseline))
        assert lib.apply_guest_state(candidate, len(candidate), ctypes.byref(r)) == 0
        actual_data = bytes(candidate)
        assert actual_data == expected, f'State mismatch in case {i}'
        if i == 0:
            state_hash = hashlib.sha256(actual_data).hexdigest()
            (out / 'guest-state-stage.bin').write_bytes(actual_data)
    before = bytes(candidate)
    invalid = StateResources.from_buffer_copy(bytes(actual))
    invalid.pb_va[0] = 1
    assert lib.apply_guest_state(candidate, len(candidate), ctypes.byref(invalid)) < 0
    assert bytes(candidate) == before
    assert lib.apply_guest_state(candidate, len(candidate) - 1, ctypes.byref(actual)) < 0
    assert bytes(candidate) == before
    report = {
        'reference_sha256': oracle.pe.sha256, 'hardware_written': False,
        'mmu_function': '0x14002a34c', 'heap_function': '0x14001ccd8',
        'heap_mask': '0x1ef9', 'reservation_granularity': hex(reservation_granularity),
        'heaps_compared': 22, 'resources_compared': 13, 'resources': resources,
        'private_pools': private_pools,
        'host_pb_va_matches': True, 'state_function': '0x140015f78',
        'platform_callback': '0x14002db90', 'state_cases_passed': len(inputs),
        'bytes_compared_per_case': len(baseline), 'invalid_inputs_rejected': 2,
        'state_sha256': state_hash, 'fence_gpu_pa_candidate': '0x0',
        'device_ranges_reserved': False, 'resource_mappings_installed': False,
        'hardware_compatibility_verified': False,
        'modeled_helpers': ['CPU allocation', 'memset', 'memcpy', 'single-threaded spinlocks'],
    }
    (ROOT / 'reports/firmware-state-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
