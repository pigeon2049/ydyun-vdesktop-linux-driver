"""Context-state allocation and InitContextResource, with explicit OS models.

Executes local reference instructions only. Windows callbacks and allocation
metadata construction are modeled; no OS or device code is called.
"""
import json
import struct
from pathlib import Path
from reference_oracle import ReferenceOracle
from unicorn import UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_RAX

ROOT = Path(__file__).resolve().parents[1]


class StateInitOracle:
    SIM, DEVICE, CONTEXT, ADAPTER = 0x200000, 0x204000, 0x208000, 0x20c000
    ALLOCATION, INTERNAL, PRIVATE = 0x210000, 0x214000, 0x218000
    PAGING, OPAQUE, PARENT, OUT = 0x220000, 0x224000, 0x228000, 0x22c000
    CPU, GPU, HANDLE = 0x600000, 0x40020000, 0x12345678
    ALLOC_STUB, FREE_STUB, CREATE_STUB, MAP_STUB = range(0x230000, 0x230040, 16)

    def __init__(self):
        self.x = x = ReferenceOracle()
        entries = sorted(int(e['address'], 16) for e in map(json.loads,
            (ROOT/'decompiled/mtkm64.sys/functions.jsonl').read_text().splitlines())
            if not e.get('external'))
        names = ('00405c 01e000 01df28 006470 0036b4 00700c '
                 '006648 01e240 0061c4 00a27c 00a730 0219d0 041cc4 130c10')
        starts = [0x140000000+int(n, 16) for n in names.split()] + [0x1411c93d4]
        self.ranges = [(a, next(e for e in entries if e > a)) for a in starts]
        x.hooks[0x140130c50] = lambda: x.ret(x.uc.reg_read(UC_X86_REG_RAX))
        x.put64(0x1401380d0, self.ALLOC_STUB)
        x.put64(0x1401380d8, self.FREE_STUB)
        x.hooks[self.ALLOC_STUB] = self.allocate
        x.hooks[self.FREE_STUB] = self.free
        x.hooks[0x1411c8214] = self.metadata
        x.hooks[0x1411c85dc] = self.destroy_metadata
        x.hooks[self.CREATE_STUB] = self.create
        x.hooks[self.MAP_STUB] = self.map
        x.uc.hook_add(UC_HOOK_MEM_WRITE, self.state_write,
                      begin=self.CPU, end=self.CPU+0x5000-1)

    def state_write(self, uc, access, address, size, value, data):
        self.state_writes.append((address, size))

    def allocate(self):
        x = self.x
        assert (x.arg(0), x.arg(1), x.arg(2)) == (0x200, 0x40, 0x4b4d3344)
        p = x.allocate(0x40)
        self.descriptor = p
        x.uc.mem_write(p, b'\xcc'*0x40)  # Actual function must initialize it.
        x.ret(p)

    def free(self):
        assert self.x.arg(0) == self.descriptor
        self.frees += 1
        self.x.ret()

    def metadata(self):
        # Model 1411c8214 -> 011510 -> 1411cb100 bookkeeping only.
        x = self.x
        assert x.arg(0) == self.ADAPTER
        request = x.arg(1)
        info = x.get64(request+0x10)
        private = x.get64(info)
        self.private = bytes(x.uc.mem_read(private, 0x130))
        assert x.get64(private+0x40) == self.descriptor
        x.put64(info+0x38, self.ALLOCATION)
        x.put64(self.ALLOCATION+8, self.INTERNAL)
        x.put64(self.INTERNAL+0xa0, self.descriptor)
        x.put64(self.descriptor+0x28, self.INTERNAL)
        x.put64(self.INTERNAL+0xa8, self.ALLOCATION)
        x.ret(0)

    def destroy_metadata(self):
        self.metadata_frees += 1
        self.x.ret(0)

    def create(self):
        x = self.x
        self.create_request = bytes(x.uc.mem_read(x.arg(0), 0x58))
        if not self.failure:
            x.put64(x.arg(0)+0x48, self.HANDLE)
        x.ret(self.failure)

    def map(self):
        x = self.x
        assert x.arg(0) == 0x5678
        self.map_request = bytes(x.uc.mem_read(x.arg(1), 0x40))
        x.ret(self.GPU)

    def run(self, pattern=0xa5, flags=10, failure=0, suppressed=False):
        x = self.x
        self.frees = self.metadata_frees = 0
        self.failure = failure
        self.state_writes = []
        self.create_request = self.map_request = self.private = None
        for p in (self.SIM, self.DEVICE, self.CONTEXT, self.ADAPTER,
                  self.ALLOCATION, self.INTERNAL, self.PRIVATE,
                  self.PAGING, self.OPAQUE, self.PARENT, self.OUT):
            x.uc.mem_write(p, bytes(0x2000))
        x.put64(self.DEVICE, self.SIM)
        x.put64(self.DEVICE+8, self.PRIVATE)
        x.put64(self.PRIVATE+8, 0x9876)
        x.put64(self.CONTEXT, self.DEVICE)
        x.put64(self.CONTEXT+0x70, 0xabc)
        x.put64(self.SIM+8, self.ADAPTER)
        x.put32(self.SIM+0x190, 1)
        x.put32(self.SIM+0x14, 0)
        x.put64(self.SIM+0x580, 0x40000000)
        x.put64(self.SIM+0x588, 0x8000000000)
        # Execute actual callback table construction and copy as 00db50 does.
        x.run(0x14000405c, [self.OUT], self.ranges)
        x.uc.mem_write(self.SIM+0x1d0, bytes(x.uc.mem_read(self.OUT, 0xc0)))
        assert x.get64(self.SIM+0x260) == 0x140006470
        assert x.get64(self.SIM+0x270) == 0x14000700c
        x.put32(self.ADAPTER, 0xbacd09b8)
        x.put64(self.ADAPTER+0x268, 0x5678)
        x.put64(self.ADAPTER+0x318, self.CREATE_STUB)
        x.put64(self.ADAPTER+0x368, self.MAP_STUB)
        before = bytes([pattern])*0x5000
        x.uc.mem_write(self.CPU, before)
        x.put64(self.OUT, 0)
        result = x.run(0x14001e000,
            [self.DEVICE, self.CONTEXT, 0, 0x4b4d3344, 0x5000, 0x80, flags, 0, self.OUT], self.ranges)
        assert bytes(x.uc.mem_read(self.CPU, 0x5000)) == before
        if failure:
            assert result == failure and x.get64(self.OUT) == 0
            assert self.frees == self.metadata_frees == 1 and self.map_request is None
            return dict(result=result, create=self.create_request, private=self.private)
        assert result == 0 and x.get64(self.OUT) == self.descriptor
        descriptor_before = bytes(x.uc.mem_read(self.descriptor, 0x40))
        assert x.get64(self.descriptor+0x10) == 0
        # OS paging notification: real dispatch ignores the general suppression
        # flag for operation 10. Model the SIM optional diagnostic backend absent.
        x.put64(self.ADAPTER+0x248, self.SIM)
        x.uc.mem_write(self.ADAPTER+0x6a0, bytes([suppressed]))
        x.put64(self.SIM+0x1020, self.PARENT)
        x.put32(self.PAGING+0x1c, 10)
        x.put64(self.PAGING+0x28, self.ALLOCATION)
        x.put64(self.PAGING+0x40, self.CPU)
        result = x.run(0x1411c93d4, [self.ADAPTER, self.PAGING], self.ranges)
        assert result == 0
        descriptor_after = bytes(x.uc.mem_read(self.descriptor, 0x40))
        expected = bytearray(descriptor_before)
        struct.pack_into('<I', expected, 0, struct.unpack_from('<I', expected)[0] | 2)
        struct.pack_into('<Q', expected, 0x10, self.CPU)
        assert descriptor_after == expected
        assert bytes(x.uc.mem_read(self.CPU, 0x5000)) == before
        x.put64(self.OPAQUE, self.CONTEXT)
        x.put64(self.OPAQUE+8, self.PARENT)
        x.put64(self.OPAQUE+0xf8, self.descriptor)
        x.uc.mem_write(self.OUT, struct.pack('<4Q', 0, 0, 384, 128))
        x.run(0x1400219d0, [2, self.OPAQUE, self.OUT], self.ranges)
        view = struct.unpack('<4Q', x.uc.mem_read(self.OUT, 32))
        assert view == (self.GPU, self.CPU, 0x5000, 128)
        assert bytes(x.uc.mem_read(self.CPU, 0x5000)) == before
        assert not self.state_writes
        return dict(result=result, create=self.create_request, private=self.private,
                    mapping=self.map_request, before=descriptor_before,
                    after=descriptor_after, view=view)


if __name__ == '__main__':
    r = StateInitOracle().run()
    print({k: v.hex() if isinstance(v, bytes) else v for k, v in r.items()})
