"""Execute per-process shared-resource mapping enumeration in isolated RAM."""
import json
from pathlib import Path
from reference_oracle import ReferenceOracle
from unicorn.x86_const import UC_X86_REG_RAX

ROOT = Path(__file__).resolve().parents[1]

class ProcessResourcesOracle:
    A, PROCESS, LIST = 0x400000, 0x404000, 0x40c000

    def __init__(self):
        self.x = x = ReferenceOracle()
        x.hooks[0x140130c50] = lambda: x.ret(x.uc.reg_read(UC_X86_REG_RAX))
        x.put64(0x1401380d0, 0x2f8030)
        x.hooks[0x2f8030] = lambda: x.ret(x.allocate(x.arg(1)))
        for iat, stub in ((0x1401383b0,0x2f8000), (0x140138250,0x2f8010), (0x140138258,0x2f8020)):
            x.import_noop(iat, stub)
        x.uc.mem_write(0x200000,b'\1')
        assert x.run(0x14002a34c,[0x201000,0],[(0x14002a0d0,0x14002a4ec)]) == 0
        assert x.run(0x14001ccd8,[0x200000,0x201000,0x1ef9,0x202000],
            [(0x14001ccd8,0x14001d443),(0x140040760,0x140041cd0)]) == 0
        self.heap_image = bytes(x.uc.mem_read(0x202000,0x9d0))
        entries = sorted(int(e['address'],16) for e in map(json.loads,
            (ROOT/'decompiled/mtkm64.sys/functions.jsonl').read_text().splitlines()) if not e.get('external'))
        self.ranges = [(a,next(e for e in entries if e>a)) for a in
            (0x140014d58,0x14000a630,0x140009258,0x14001a558,
             0x140019c5c,0x140019cc4,0x14001e1e4,0x14001dafc,0x14001d98c,
             0x14001d3c8,0x1400083b0)] + [(0x140040760,0x140041cc4)]
        self.dynamic_cursor = x.cursor
        x.hooks[0x140022218] = lambda: self.allocate_resource(False)
        x.hooks[0x140022f38] = lambda: self.allocate_resource(True)
        x.hooks[0x1400230b4] = lambda: x.ret(0)
        x.hooks[0x140018bd0] = self.page

    def allocate_resource(self, alternative):
        x = self.x
        if self.allocating_pools:
            index = (1,2,10)[len(self.pool_allocations)]
            size = x.arg(1)
            assert not alternative and size == (0x100000 if index == 2 else 0x200000)
            assert x.arg(2) == 0x4c4f4f50
            descriptor,data = x.allocate(0x40),x.allocate(size)
            physical,pa = 0x700000+index*0x10000,0x680000000+index*0x800000
            x.put64(descriptor,data);x.put32(descriptor+8,size);x.put64(descriptor+0x10,physical)
            for off in range(0,size,4096):x.put64(physical+off//4096*8,pa+off)
            self.pool_allocations[index]=(descriptor,data,size,pa)
            x.ret(descriptor)
            return
        index = (4, 9, 10, 11, 6, 3)[len(self.allocations)]
        size, tag, flags = (x.arg(i) for i in (1, 2, 3))
        expected_size = int.from_bytes(x.uc.mem_read(self.A+0x798+index*0x20,4),'little')
        assert size == expected_size
        assert (tag, flags) == ((0x53585443,1) if index in (4,9,10,11) else
                                (0x54485741,0) if index == 6 else (0x49474150,1))
        assert alternative == (index == 3 and self.alternative)
        descriptor, data = x.allocate(0x40), x.allocate(size)
        x.uc.mem_write(data, b'\x5a'*size)
        physical = 0x500000+index*0x10000
        pa = 0x610000000+index*0x800000
        x.put64(descriptor,data);x.put32(descriptor+8,size);x.put64(descriptor+0x10,physical)
        for off in range(0,size,4096):x.put64(physical+(off//4096)*8,pa+off)
        self.allocations[index] = (descriptor,data,size)
        x.ret(descriptor)

    def page(self):
        x = self.x
        assert x.arg(0) == 0x412000
        self.pages.append((x.arg(1),x.arg(2),x.arg(4)&0xffffffff))
        x.ret()

    def run(self, optional_mask=0, alternative=True, dynamic_pools=False):
        x = self.x
        assert optional_mask in (0,1,4,5)
        if dynamic_pools:
            assert not getattr(self, "pools_initialized", False), "Use fresh original allocator state"
            self.pools_initialized = True
        x.uc.mem_write(self.A,bytes(0x2000))
        x.uc.mem_write(self.A+0x578,self.heap_image)
        x.put64(self.A+0x448,x.get64(0x201008))
        x.put64(self.A+0x1060,self.LIST)
        x.uc.mem_write(self.LIST,bytes(0x1000))
        x.put64(self.PROCESS,self.A); x.put64(self.PROCESS+0x28,0x412000)
        self.pages = []
        self.resources = []
        self.allocations = {}
        self.pool_allocations = {}
        self.allocating_pools = False
        self.alternative = alternative
        x.cursor = self.dynamic_cursor
        x.uc.mem_write(0x414000, bytes(0x100))
        x.uc.mem_write(0x414000, bytes([int(alternative)]))
        if dynamic_pools:
            assert not optional_mask & 2  # No synthetic USC-list fixture.
            self.allocating_pools = True
            assert x.run(0x14001dafc,[self.A+0x1060,self.A+0xe98,self.A+0xc88,0,0x200000],self.ranges)==0
            self.allocating_pools = False
            assert len(self.pool_allocations)==3
        # 00de94 passes adapter+0x790 as the resource plan to 01a558.
        assert x.run(0x14001a558,[self.A+0x790,0,0x414000,self.A+0x78,
                     self.A+0x578,self.A+0x1058],self.ranges) == 0
        self.PLATFORM = x.get64(self.A+0x1058)
        assert x.get64(self.PLATFORM+0x10) == self.A+0x578
        assert len(self.allocations) == 6
        assert x.get64(self.PLATFORM+0x138) == self.allocations[3][0]
        assert x.get64(self.PLATFORM+0x140) == x.get64(self.A+0x800)
        for index in (4,9,6,3):
            _,data,size = self.allocations[index]
            assert bytes(x.uc.mem_read(data,size)) == b'\x5a'*size


        def resource(index, parent_offset=None):
            # Real heap plan supplies VAs; ordinary RAM models physical page lists.
            va = x.get64(self.A+0x7a0+index*0x20)
            size = int.from_bytes(x.uc.mem_read(self.A+0x798+index*0x20,4),'little')
            assert va  # Fixed resources have immediate addresses.
            descriptor = self.allocations[index][0] if index in self.allocations else 0x420000+index*0x100
            physical = 0x500000+index*0x10000
            pa = 0x610000000+index*0x800000
            if index in self.allocations:
                assert self.allocations[index][2] == size
                assert x.get64(descriptor+0x10) == physical
            else:  # Optional PB creation is outside this oracle.
                x.put32(descriptor+8,size);x.put64(descriptor+0x10,physical)
                for offset in range(0,size,4096):
                    x.put64(physical+(offset//4096)*8,pa+offset)
            if parent_offset is not None:
                assert x.get64(self.PLATFORM+parent_offset) == descriptor
            self.resources.append((index,va,pa,size))
            return descriptor,va

        # Ordering follows 014d58: optional PB, resource list, fixed resources,
        # optional fence, then the 4 MiB paging command allocation.
        if optional_mask & 1:
            descriptor,va = resource(0)
            x.put64(self.A+0x68,0x416000);x.put64(0x416008,descriptor)
        if dynamic_pools:
            manager=x.get64(self.A+0x1060)
            assert int.from_bytes(x.uc.mem_read(manager,4),'little')==22
            for index,(descriptor,data,size,pa) in self.pool_allocations.items():
                slot=manager+8+index*0x38
                assert x.get64(slot)==descriptor and descriptor!=self.allocations[9][0]
                assert x.get64(slot+16)==size
                self.resources.append((100+index,x.get64(slot+8),pa,size))
        for index,offset in ((4,0x60),(10,0xe0),(11,0x120)):
            resource(index,offset)
        if optional_mask & 4:resource(6,0x130)
        else:x.put64(self.PLATFORM+0x130,0)  # Exercise optional enumeration branch.
        descriptor,va = resource(3,0x138)
        assert x.get64(self.PLATFORM+0x140) == va
        x.run(0x140014d58,[self.A,self.PROCESS],self.ranges)
        expected = [(va+off,pa+off,0) for _,va,pa,size in self.resources for off in range(0,size,4096)]
        assert self.pages == expected
        return self.resources,self.pages

if __name__ == '__main__':
    for mask in (0,1,4,5):
        r,p=ProcessResourcesOracle().run(mask,dynamic_pools=True)
        print(mask,r,len(p))
