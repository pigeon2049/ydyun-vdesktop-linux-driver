"""Execute the TQX queue state allocation policy and VA getter in RAM.

Only the allocator boundary is modeled. This does not establish state contents,
OS allocation semantics, hardware core count, or GPU execution.
"""
import json
import struct
from pathlib import Path
from reference_oracle import ReferenceOracle
from unicorn.x86_const import UC_X86_REG_RAX

ROOT = Path(__file__).resolve().parents[1]

class EngineStateOracle:
    DEVICE, QUEUE, RESOURCE = 0x200000, 0x210000, 0x220000

    def __init__(self):
        self.x = x = ReferenceOracle()
        entries = sorted(int(e['address'], 16) for e in map(json.loads,
            (ROOT/'decompiled/mtkm64.sys/functions.jsonl').read_text().splitlines())
            if not e.get('external'))
        names = '093aa8 093cc8 04a59c 04c2fc 04c2b0 04c3b0 04c490 04c288 04b2bc 0450c8 045138 0931b8 0219d0 041cc4'
        self.ranges = [(a, next(e for e in entries if e > a)) for a in
            (0x140000000 + int(s, 16) for s in names.split())]
        x.hooks[0x140130c50] = lambda: x.ret(x.uc.reg_read(UC_X86_REG_RAX))
        x.hooks[0x140044efc] = lambda: (_ for _ in ()).throw(AssertionError('reference assertion'))
        x.hooks[0x14005aec0] = self.allocate

    def allocate(self):
        x = self.x
        assert x.arg(0) == self.DEVICE + 0x2e0 and x.arg(4) == 0
        self.calls.append((bytes(x.uc.mem_read(x.arg(1), 0xb0)),
                           bytes(x.uc.mem_read(x.arg(2), 0x30))))
        if not self.failure:
            x.put64(x.arg(3), self.RESOURCE)
        x.ret(self.failure)

    def run(self, cores, va, prefer_local=True, failure=0, offset=0):
        x = self.x
        self.calls, self.failure = [], failure
        x.uc.mem_write(self.QUEUE, bytes(0x180))
        x.put64(self.QUEUE + 0x10, self.DEVICE)
        x.put32(self.DEVICE + 0x380, 2)
        x.put32(self.DEVICE + 0x3b4, cores)
        x.put64(self.DEVICE + 0x2100, 0x100000 if prefer_local else 0)
        x.put64(self.RESOURCE + 8, va)
        x.run(0x140093aa8, [self.QUEUE], self.ranges)
        assert len(self.calls) == 1
        wrapper = bytes(x.uc.mem_read(self.QUEUE + 0x148, 32))
        encoded = None
        if not failure:
            # A nonzero offset exercises the getter independently of the
            # allocator, whose actual successful wrapper offset is zero.
            x.put64(self.QUEUE + 0x150, offset)
            encoded = x.run(0x140093cc8, [self.QUEUE], self.ranges)
        return dict(descriptor=self.calls[0][0], properties=self.calls[0][1],
                    wrapper=wrapper, encoded=encoded,
                    extra=x.get64(self.QUEUE + 0x168))

    def platform_state(self, cores, va, external=False, pattern=0xa5):
        """Actual type selection and ctxState callback over existing RAM backing."""
        x = self.x
        r = self.run(cores, va)
        request, properties = 0x240000, 0x241000
        opaque, context, parent, descriptor, mapping, out = range(0x250000, 0x256000, 0x1000)
        cpu, size = 0x600000, 0x5000
        x.uc.mem_write(request, r['descriptor'])
        x.uc.mem_write(properties, r['properties'])
        kind = x.run(0x1400931b8, [request+8, properties], self.ranges)
        assert kind == 2
        x.uc.mem_write(opaque, bytes(0x118))
        x.put64(opaque, context); x.put64(opaque+8, parent)
        if external:
            x.put64(opaque+0x100, mapping); x.put64(mapping, cpu)
            x.put64(context+0x60, descriptor)
            x.put32(descriptor+8, size); x.put64(descriptor+0x10, va)
        else:
            x.put64(opaque+0xf8, descriptor)
            x.put64(descriptor+8, size); x.put64(descriptor+0x10, cpu)
            x.put64(descriptor+0x18, va)
        before = bytes([pattern])*size
        x.uc.mem_write(cpu, before)
        x.uc.mem_write(out, struct.pack('<4Q', 0, 0, cores*384, 128))
        x.run(0x1400219d0, [kind, opaque, out], self.ranges)
        assert bytes(x.uc.mem_read(cpu, size)) == before
        return struct.unpack('<4Q', x.uc.mem_read(out, 32))

if __name__ == '__main__':
    r = EngineStateOracle().run(1, 0x40020000)
    print({k: v.hex() if isinstance(v, bytes) else v for k, v in r.items()})
