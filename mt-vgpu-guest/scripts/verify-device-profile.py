#!/usr/bin/env python3
"""Follow original PCI-ID callback and library version selection in RAM.

Stops original device creation by returning allocation failure after capturing
the actual selected version table; never runs a device constructor or MMIO.
"""
import ctypes as C
import json
import struct
import subprocess
from pathlib import Path
from reference_oracle import ReferenceOracle
from unicorn.x86_const import UC_X86_REG_RIP, UC_X86_REG_RAX

ROOT = Path(__file__).resolve().parents[1]
PLATFORM, OPAQUE, ADAPTER, PCI, DEVICE, OUTPUT, VERSIONS = (
    0x200000, 0x202000, 0x203000, 0x204000, 0x210000, 0x215000, 0x216000)
x = ReferenceOracle()
x.put64(PLATFORM + 0xbd8, OPAQUE)
x.put64(PLATFORM + 0xbe0 + 0x10, 0x1400205ac)
x.put64(OPAQUE + 8, ADAPTER)
x.put64(ADAPTER + 8, PCI)
x.put64(0x140138420, 0x2f8810)
x.hooks[0x2f8810] = lambda: x.uc.reg_write(UC_X86_REG_RIP, x.uc.reg_read(UC_X86_REG_RAX))
x.hooks[0x140130c50] = lambda: x.ret(x.uc.reg_read(UC_X86_REG_RAX))
x.hooks[0x140044efc] = lambda: (_ for _ in ()).throw(AssertionError('reference assertion'))
captured = []
def capture_versions():
    captured.append(bytes(x.uc.mem_read(x.arg(0), 36)))
    x.uc.mem_write(x.arg(1), bytes(72))  # Allocation-size fields, unused after failure.
    x.ret()
x.hooks[0x140047034] = capture_versions
x.hooks[0x140046cd8] = lambda: x.ret(0x220000)  # Modeled allocation request.
x.hooks[0x140046df0] = lambda: x.ret(0)         # Forced failure: no constructor.
ranges = [(0x1400470c8, 0x14004749c), (0x14004749c, 0x1400474c4),
          (0x1400205ac, 0x1400205d4), (0x140042000, 0x14004204f),
          (0x1400228b4, 0x1400228e2), (0x140046e1c, 0x140047034),
          (0x14004d558, 0x14004d708), (0x140094b2c, 0x140094bb1),
          (0x1400b1074, 0x1400b10a5), (0x14005331c, 0x140053395)]
factory_calls = []
for addr in (0x14009cb90, 0x14009e2cc):
    def factory(addr=addr):
        factory_calls.append(addr)
        x.ret(0)
    x.hooks[addr] = factory

class Profile(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in
                ('vendor','device','family','primary_version','ce_version','transfer_version','compute_version')]
libpath = ROOT / 'build/firmware/device-profile.so'
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-shared','-fPIC',
                ROOT/'tests/device_profile_oracle_wrapper.c','-o',libpath], check=True)
lib = C.CDLL(str(libpath))
lib.select_profile.argtypes = [C.POINTER(Profile), C.c_uint32, C.c_uint32]
cases = []
for device in (0, 0xff, 0x100, 0x102, 0x1ff, 0x200, 0x201, 0x202, 0x222, 0x2ff,
               0x300, 0x301, 0x3ff, 0x400, 0x401, 0x4ff, 0x500, 0x5ff,
               0x600, 0x601, 0x6ff, 0x700, 0xffff):
    x.uc.mem_write(PCI + 0x90, struct.pack('<HHI', 0x1ed5, device, 0))
    captured.clear()
    # The real callback 0205ac -> 042000 -> 0228b4 supplies the device ID.
    result = x.run(0x1400470c8, [PLATFORM, 0, 0, OUTPUT, OUTPUT + 8], ranges) & 0xffffffff
    assert result == 0xfffffffc, (hex(device),hex(result),captured)
    p = Profile()
    C.memset(C.byref(p), 0xa5, C.sizeof(p))
    before = bytes(p)
    ret = lib.select_profile(C.byref(p), 0x1ed5, device)
    if not captured:
        assert ret == -19 and bytes(p) == before
        cases.append(dict(device=hex(device), accepted=False))
        continue
    assert len(captured) == 1 and ret == 0
    versions = struct.unpack('<9I', captured[0])
    assert [p.primary_version,p.ce_version,p.transfer_version,p.compute_version] == [versions[1],versions[2],versions[4],versions[3]]
    x.uc.mem_write(VERSIONS, captured[0])
    x.uc.mem_write(DEVICE, bytes(0x4000))
    assert x.run(0x14004d558, [DEVICE, p.family, VERSIONS], ranges) == 0
    installed = struct.unpack('<5I', x.uc.mem_read(DEVICE + 0x38c, 20))
    assert installed == (p.primary_version, p.ce_version, p.transfer_version, 0, p.compute_version)
    # Execute the real CE dispatcher. Constructor bodies are captured, not run.
    factory_calls.clear()
    result = x.run(0x14005331c, [p.ce_version, DEVICE, OUTPUT, OUTPUT + 8], ranges)
    assert result == (0 if p.ce_version else 1)
    assert factory_calls == ([] if not p.ce_version else [0x14009e2cc if p.ce_version == 3 else 0x14009cb90])
    cases.append(dict(device=hex(device), accepted=True, **{n:getattr(p,n) for n,_ in Profile._fields_[2:]},
                      ce_constructor=hex(factory_calls[0]) if factory_calls else None))

for vendor, device in ((0,0x222),(0x1234,0x222),(0x1ed5,0x10222)):
    p = Profile(); C.memset(C.byref(p),0xa5,C.sizeof(p)); before = bytes(p)
    assert lib.select_profile(C.byref(p),vendor,device) == -19 and bytes(p) == before

live = Path('/sys/bus/pci/devices/0000:00:0e.0')
identity = {n: int((live/n).read_text().strip(),16) for n in ('vendor','device','subsystem_vendor','subsystem_device')}
p = Profile()
assert lib.select_profile(C.byref(p),identity['vendor'],identity['device']) == 0
report = dict(passed=True, reference_sha256=x.pe.sha256, selection_cases=len(cases),
              cases=cases, local_pci_identity={k:hex(v) for k,v in identity.items()},
              local_profile={n:getattr(p,n) for n,_ in Profile._fields_},
              functions=['1400470c8','1400205ac','140042000','1400228b4','140046e1c','14004d558','14005331c'],
              hardware_accessed=False, local_identity_source='read-only sysfs PCI identity attributes',
              limits='Allocation sizes/request/failure and CE constructor return values modeled. Selected versions, field installation and dispatch executed from original instructions. No live engine or firmware submission validated.')
(ROOT/'reports/device-profile-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
