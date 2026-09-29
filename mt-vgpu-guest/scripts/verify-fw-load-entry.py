#!/usr/bin/env python3
"""Execute the complete reference Guest firmware load up to connection.

Only whitelisted CPU instructions execute in Unicorn RAM. GPU allocations use
the retained Linux trial's physical addresses. MMIO is recorded, never issued.
The real connection/disconnection instructions see a modeled stalled firmware;
this checks the submitted data and cannot prove actual firmware startup.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
CONFIG, DEVICE, GPU = 0x200000, 0x210000, 0x214000
PB, PAIRS, WORKER = 0x21a000, 0x21b000, 0x21c000
LAYOUT, INFO = 0x220000, 0x224000
MMU, HEAPS = DEVICE + 0x440, DEVICE + 0x578
FW_PA, TABLE_PA, AUX_PA = 0x771fef000, 0x605800000, 0x605806000
FW_VA, BAR2 = 0xe1c0000000, 0x800000000


class LoadOracle(ReferenceOracle):
    def __init__(self):
        super().__init__()
        self.allocations = []
        self.table_bytes = 0
        self.writes = []
        self.connect_calls = 0
        self.preconnect_firmware = None
        self.register_writes = []
        self.firmware_reads = 0
        self.delays = 0
        self.put64(0x1401380d0, 0x2f8030)
        self.hooks[0x2f8030] = lambda: self.ret(self.allocate(self.arg(1)))
        self.put64(0x1401381b8, 0x2f8040)
        self.hooks[0x2f8040] = self.cpu_physical
        self.hooks[0x140022218] = self.gpu_allocate
        self.hooks[0x140027780] = self.mmio_write
        self.hooks[0x1400168b4] = self.connection
        self.hooks[0x140027740] = self.register_write
        self.hooks[0x1400275a4] = self.register_read
        self.hooks[0x140008404] = self.delay
        self.put64(0x1401380f0, 0x2f8050)
        self.hooks[0x2f8050] = lambda: self.ret(275)  # PsGetCurrentProcessId.
        for i, iat in enumerate((0x1401383b0, 0x140138250, 0x140138258)):
            self.import_noop(iat, 0x2f8000 + i * 16)
        for i, iat in enumerate((0x1401380c0, 0x1401380c8)):
            self.import_noop(iat, 0x2f8060 + i * 16)

    def gpu_allocate(self):
        size, tag, flags = [self.arg(i) & 0xffffffff for i in (1, 2, 3)]
        if flags == 0x40:
            assert size == 0x800000 and tag == 0x46575447
            kind, pa, offset = 'firmware', FW_PA, 0x3f000000
        elif tag == 0x46575447:
            assert size == 4096 and flags == 1
            kind, pa, offset = 'auxiliary', AUX_PA, 0xa06000
        else:
            assert size == 4096 and self.table_bytes < 0x6000
            kind, pa, offset = 'table', TABLE_PA + self.table_bytes, 0xa00000 + self.table_bytes
            self.table_bytes += size
        self.cursor = (self.cursor + 4095) & ~4095
        cpu = self.allocate(size)
        descriptor = self.allocate(0x40)
        physical = self.allocate(size // 4096 * 8)
        self.put64(descriptor, cpu)
        self.put32(descriptor + 8, size)
        self.put32(descriptor + 12, tag)
        self.put64(descriptor + 16, physical)
        for i in range(size // 4096):
            self.put64(physical + i * 8, pa + i * 4096)
        self.allocations.append(dict(kind=kind, cpu=cpu, size=size, pa=pa,
                                     offset=offset, flags=flags, tag=tag))
        self.ret(descriptor)

    def cpu_physical(self):
        address = self.arg(0)
        for a in self.allocations:
            if a['cpu'] <= address < a['cpu'] + a['size']:
                self.ret(BAR2 + a['offset'] + address - a['cpu'])
                return
        raise AssertionError(f'Unmodeled CPU physical address: {address:#x}')

    def mmio_write(self):
        assert self.arg(0) == GPU + 8
        self.writes.append((self.arg(1) & 0xffffffff, self.arg(2)))
        self.ret()

    def connection(self):
        assert self.arg(0) == DEVICE
        self.connect_calls += 1
        self.preconnect_firmware = self.bytes_for('firmware')
        # Leave RIP untouched: execute this function's original instructions.

    def register_write(self):
        assert self.arg(0) == GPU + 8 and self.arg(1) & 255 == 0
        offset, value = self.arg(2) & 0xffffffff, self.arg(3) & 0xffffffff
        assert (offset, value) in ((0x890, 1), (0xb00, 0))
        self.register_writes.append((offset, value))
        self.ret()

    def register_read(self):
        assert self.arg(0) == GPU + 8 and self.arg(1) & 255 == 0
        assert self.arg(2) & 0xffffffff == 0x898
        self.firmware_reads += 1
        self.ret(1)

    def delay(self):
        assert self.arg(0) == 25000
        self.delays += 1
        self.ret()

    def bytes_for(self, kind):
        return b''.join(bytes(self.uc.mem_read(a['cpu'], a['size']))
                        for a in self.allocations if a['kind'] == kind)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--live', action='store_true',
                        help='also read the bound module snapshots using sudo -n cat; no device writes')
    args = parser.parse_args()
    o = LoadOracle()
    o.uc.mem_write(CONFIG, b'\1')
    o.put64(DEVICE, CONFIG)
    o.put64(DEVICE + 0x68, PB)
    o.put64(DEVICE + 0x80, 0x141107100)
    o.put64(DEVICE + 0x418, GPU)
    o.put64(DEVICE + 0x420, LAYOUT)
    o.put64(DEVICE + 0x550, GPU)
    o.put64(DEVICE + 0x1020, WORKER)
    o.uc.mem_write(GPU + 0x4c, b'\1')
    o.put64(GPU + 0x9f8, 0x14002db90)
    o.put64(GPU + 0x1cb8, INFO)
    o.put64(GPU + 0x408, BAR2)
    o.put64(GPU + 0x410, 0x400000000)
    o.put32(GPU + 0x1e18, 0x890)
    o.put32(GPU + 0x1e1c, 0x898)
    o.uc.mem_write(INFO, (ROOT / 'reports/device-info.bin').read_bytes())
    o.put64(PB + 0x10, PAIRS)
    o.put64(PAIRS, 0xa000e00000)
    o.put64(PAIRS + 8, 0xa000e00100)
    assert o.run(0x14002a34c, [MMU, GPU], [(0x14002a0d0, 0x14002a4ec)]) == 0
    assert o.run(0x14001ccd8, [CONFIG, MMU, 0x1ef9, HEAPS], [
        (0x14001ccd8, 0x14001d443), (0x140040760, 0x140041cd0),
        (0x140130c50, 0x140130c65)]) == 0
    assert o.get64(DEVICE + 0x610) == FW_VA
    o.uc.mem_write(LAYOUT, (ROOT / 'build/firmware/guest-layout.bin').read_bytes())
    o.run(0x14001f064, [0], [(0x14001f058, 0x14001f0b0),
                            (0x14001ee48, 0x14001ee94)])
    ranges = [
        (0x140016218, 0x14001669c), (0x1400159cc, 0x140015b38),
        (0x140015dc8, 0x140016215), (0x14000823c, 0x140008402),
        (0x14000c198, 0x14000c1a5), (0x1400184e4, 0x14001864f),
        (0x14001edf0, 0x14001f028), (0x14001f0b0, 0x14001f268),
        (0x14000807c, 0x140008084), (0x14001e32c, 0x14001e347),
        (0x1400227e4, 0x140022825), (0x14002db90, 0x14002de95),
        (0x1400180ec, 0x1400183f3), (0x140018bd0, 0x140018e0b),
        (0x140018f68, 0x140019549), (0x1400197b4, 0x140019986),
        (0x14002429c, 0x1400242c2), (0x14002a2dc, 0x14002a34c),
        (0x14002a5f8, 0x14002a600), (0x14002a810, 0x14002a913),
        (0x140130c10, 0x140130c12), (0x1400224b0, 0x1400224eb),
        (0x140021d90, 0x140021ede), (0x140027ab4, 0x140027bec),
        (0x140130c50, 0x140130c65),
        (0x1400168b4, 0x140016b4b), (0x140016b4c, 0x140016d0e),
        (0x140023afc, 0x140023b83), (0x14002249c, 0x1400224b0),
        (0x14000bf20, 0x14000c150), (0x14001b750, 0x14001b763),
        (0x140022504, 0x14002250d),
        (0x14000bc9c, 0x14000bce8), (0x14000bdc4, 0x14000be03),
    ]
    assert o.run(0x140016218, [DEVICE, 0, 0], ranges, count=5000000) == 0
    assert o.connect_calls == 1
    assert o.writes == [(0x30, FW_PA), (0x38, 0x800000)] + [(0x148, 1)] * 4, o.writes
    assert o.register_writes == [(0x890, 1)] + [(0xb00, 0)] * 4
    assert o.delays == 400 and o.firmware_reads == 800
    firmware = o.preconnect_firmware
    tables = o.bytes_for('table')
    assert firmware == (ROOT / 'build/firmware/guest-state-stage.bin').read_bytes(), 'Firmware differs'
    assert len(tables) == 0x6000
    assert tables == (ROOT / 'build/firmware/bootstrap-stage-live.bin').read_bytes()[:0x6000], 'Tables differ'
    assert o.bytes_for('auxiliary') == b'\xba' * 4096
    fw_cpu = next(a['cpu'] for a in o.allocations if a['kind'] == 'firmware')
    assert o.get64(LAYOUT + 0xc8) == fw_cpu
    assert o.get64(LAYOUT + 0xe0) == fw_cpu + 0x6ddd0
    assert bytes(o.uc.mem_read(LAYOUT + 0x4c8, 1)) == b'\1'
    assert o.run(0x140016b4c, [DEVICE], ranges) & 255 == 0
    assert o.register_writes == [(0x890, 1)] + [(0xb00, 0)] * 5
    # Match the retained failed trial after connect retries and attempted
    # disconnect. PIDs are the only intentionally different runtime input.
    after = bytearray(o.bytes_for('firmware'))
    retained = bytearray((ROOT / 'build/firmware/trial-after-package.bin').read_bytes())
    recorded_pids = []
    for i in range(5):
        offset = 0x6ddd0 + i * 80
        assert struct.unpack_from('<I', after, offset + 12)[0] == (0x46 if i < 4 else 0x47)
        recorded_pids.append(struct.unpack_from('<I', retained, offset + 76)[0])
        after[offset + 76:offset + 80] = bytes(4)
        retained[offset + 76:offset + 80] = bytes(4)
    assert after == retained, 'Retained failed trial differs beyond process IDs'
    report = {
        'utc': datetime.now(timezone.utc).isoformat(),
        'reference_sha256': o.pe.sha256, 'entry': '0x140016218',
        'hardware_written': False, 'hardware_connection_verified': False,
        'entry_return': False, 'connection_calls': o.connect_calls,
        'firmware_model': 'state=READY(1), started=0, never consumes queues',
        'connection_delay_count': o.delays,
        'register_writes': [{'offset': hex(a), 'value': hex(b)} for a, b in o.register_writes],
        'retained_trial_bytes_compared': len(after),
        'retained_trial_sha256': hashlib.sha256((ROOT / 'build/firmware/trial-after-package.bin').read_bytes()).hexdigest(),
        'normalized_command_pids': recorded_pids,
        'comparison_normalizes_only_five_command_pid_fields': True,
        'firmware_bytes_compared': len(firmware),
        'firmware_sha256': hashlib.sha256(firmware).hexdigest(),
        'table_bytes_compared': len(tables),
        'table_sha256': hashlib.sha256(tables).hexdigest(),
        'auxiliary_bytes_compared': 4096,
        'publications': [{'offset': hex(a), 'value': hex(b)} for a, b in o.writes],
        'allocations': o.allocations,
        'modeled_helpers': ['CPU allocation and memory primitives', 'single-threaded locks',
                            'GPU allocation descriptors at retained trial addresses',
                            'MmGetPhysicalAddress using corresponding Guest BAR2 GPA',
                            'MMIO recording and stalled firmware state', '25 ms delay counter',
                            'deterministic process ID'],
        'limits': ['Host mappings, firmware execution and allocation lifecycle not emulated',
                   'Guest setup/layout inputs are the current verified fixtures',
                   'One current-device cold-load path and stalled disconnect, not general error/reset coverage'],
    }
    name = 'firmware-load-entry-validation.json'
    if args.live:
        sysfs = '/sys/bus/pci/devices/0000:00:0e.0/'
        snapshots = {}
        for attribute in ('firmware_image', 'trial_firmware', 'bootstrap_raw'):
            snapshots[attribute] = subprocess.run(
                ['sudo', '-n', 'cat', sysfs + 'mt_guest/' + attribute],
                check=True, capture_output=True).stdout
        assert snapshots['firmware_image'] == firmware
        assert snapshots['trial_firmware'] == (ROOT / 'build/firmware/trial-after-package.bin').read_bytes()
        assert snapshots['bootstrap_raw'][:len(tables)] == tables
        report['live_read_only_comparisons'] = {
            key: {'bytes_read': len(value), 'sha256': hashlib.sha256(value).hexdigest()}
            for key, value in snapshots.items()}
        report['live_status'] = {
            key: Path(sysfs + key).read_text()
            for key in ('mt_guest/trial', 'mt_guest/connection', 'mt_live/status')}
        name = 'firmware-load-entry-live-validation.json'
    (ROOT / 'reports' / name).write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
