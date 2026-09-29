#!/usr/bin/env python3
"""Verify heap domains with original selectors/subtraction and packed streams."""
import ctypes as C
from datetime import datetime, timezone
import json
from pathlib import Path
import random
import struct
import subprocess
from reference_oracle import ReferenceOracle
from tqx_stream_reference import StreamOracle
from unicorn.x86_const import UC_X86_REG_RAX, UC_X86_REG_RIP
ROOT=Path(__file__).resolve().parents[1]
class Copy(C.Structure):
    _fields_=[(n,C.c_uint64) for n in ('src','dst','bytes')]
class Placement(C.Structure):
    _fields_=[('copy',Copy),('va',C.c_uint64*5)]
class Heap(C.Structure):
    _fields_=[(n,C.c_uint64) for n in ('base','size','reserved_base','reserved_size')]
class Resource(C.Structure):
    _fields_=[('va',C.c_uint64),('size',C.c_uint32),('heap',C.c_uint32)]
class Plan(C.Structure):
    _fields_=[('heaps',Heap*22),('resources',Resource*13)]
class Addresses(C.Structure):
    _fields_=[('constants_va',C.c_uint64)]+[(n,C.c_uint32) for n in
        ('source_descriptor_index','pds_execution_state','pds_constant_state')]
class Request(C.Structure):
    _fields_=[('copy',Copy),('command_va',C.c_uint64)]+[(n,C.c_uint32) for n in
        ('shader_heap_base','pds_code_heap_base','pds_initial_state')]+[('addresses',Addresses*4)]
class Program(C.Structure):
    _fields_=[('constants',C.c_ubyte*20),('pds_constants',C.c_ubyte*16),('pds_execution',C.c_ubyte*36),
              ('meta',C.c_uint32*6)]
class Image(C.Structure):
    _fields_=[('count',C.c_uint32),('command_bytes',C.c_uint32),('commands',C.c_ubyte*320),
              ('sources',(C.c_ubyte*64)*4),('programs',Program*4),('pds_initial',C.c_ubyte*16),
              ('record',C.c_ubyte*296),('page_record',C.c_ubyte*16),('root_export',C.c_ubyte*16)]
class HeapOracle:
    DEVICE,RESOURCE=0x210000,0x230000
    def __init__(self):
        x=self.x=ReferenceOracle()
        x.hooks[0x140130c50]=lambda:x.ret(x.uc.reg_read(UC_X86_REG_RAX))
        x.hooks[0x140044efc]=lambda:(_ for _ in ()).throw(AssertionError('reference assertion'))
        x.put64(0x140138420,0x2f8810)
        x.hooks[0x2f8810]=lambda:x.uc.reg_write(UC_X86_REG_RIP,x.uc.reg_read(UC_X86_REG_RAX))
        x.put64(self.DEVICE,0x1403c4d40)
        entries=sorted(int(e['address'],16) for e in map(json.loads,
            (ROOT/'decompiled/mtkm64.sys/functions.jsonl').read_text().splitlines()) if not e.get('external'))
        self.ranges=[(a,next(e for e in entries if e>a)) for a in
            (int('140'+s,16) for s in '042000 093494 092938 0a21f8 0a22b4 055090 04b2bc 0931b8 045374 0d30f0 092b50 08ce98 0a1bd8'.split())]
        # Execute the already traced mode-0 heap initializer, including tail
        # reservation rounding. Only OS RAM allocation and locks are modeled.
        x.uc.mem_write(0x200000,b'\1')
        x.put64(0x1401380d0,0x2f8030)
        x.hooks[0x2f8030]=lambda:x.ret(x.allocate(x.arg(1)))
        assert x.run(0x14002a34c,[0x201000,0],[(0x14002a0d0,0x14002a4ec)])==0
        for iat,stub in ((0x1401383b0,0x2f8000),(0x140138250,0x2f8010),(0x140138258,0x2f8020)):
            x.import_noop(iat,stub)
        assert x.run(0x14001ccd8,[0x200000,0x201000,0x1ef9,0x202000],
            [(0x14001ccd8,0x14001d443),(0x140040760,0x140041cd0)])==0
        # 01a558 stores adapter+578 (the full plan) at platform+10.
        # Execute the SDK getter rather than feeding the user-allocatable
        # descriptor copy at plan+4f8, which excludes these internal pools.
        x.put64(0x240008,0x241000);x.put64(0x241010,0x202000)
        x.hooks[0x1400228b4]=lambda:x.ret() # Device ID query only.
        x.run(0x140042000,[0x240000,0x242000,0x242008,0x242010,0x242018,0x242020],self.ranges)
        assert x.get64(0x242000)==0x202000 and x.get64(0x242010)==0x241030
        assert struct.unpack('<I',x.uc.mem_read(0x242008,4))[0]==22
        self.descriptors=bytes(x.uc.mem_read(x.get64(0x242000),22*24))
        x.put64(self.DEVICE+0x3f60,x.get64(0x242000));x.put32(self.DEVICE+0x3f68,22)
        self.available={i:(struct.unpack('<Q',x.uc.mem_read(0x2024f8+i*24+8,8))[0],
                          sum(struct.unpack('<QQ',x.uc.mem_read(0x2024f8+i*24+8,16))))
                        for i in (0,1,2,10)}
        x.run(0x140093494,[self.DEVICE],self.ranges)
        self.heaps={}
        for external,internal in ((0,0),(1,4),(2,3),(10,8)):
            x.run(0x140092938,[self.DEVICE,internal,0x231000,0x231008],self.ranges)
            self.heaps[external]=(x.get64(0x231000),x.get64(0x231008))
        leaf=struct.unpack('<Q',x.pe.read(0x141021938+0x28,8))[0]
        assert leaf==0x140045374 and x.run(leaf,[],self.ranges)==0
    def relative(self,external,va,offset=0):
        x=self.x;x.put64(self.RESOURCE+0x78,self.DEVICE)
        usage={0:0,1:2,2:1,10:9}[external]
        x.uc.mem_write(0x233000,struct.pack('<IIQQQ',usage,0,va,4096,0x250000))
        assert x.run(0x1400d30f0,[self.RESOURCE,0x233000],self.ranges)==0
        assert struct.unpack('<I',x.uc.mem_read(self.RESOURCE+0x80,4))[0]=={0:0,1:4,2:3,10:8}[external]
        return x.run(0x1400a21f8,[self.RESOURCE,offset],self.ranges)
class PackedOracle(StreamOracle):
    def __init__(self,heaps):
        self.heaps=heaps
        super().__init__()
    def allocate_state(self):
        x=self.x;words,align,va_out,kind,rel_out=[x.arg(i) for i in (1,2,3,4,5)]
        words&=0xffffffff;align&=0xffffffff;kind&=0xffffffff
        assert (kind,words,align) in ((4,4,4),(4,9,4),(5,16,8),(4,5,1))
        n=len(self.allocations);cpu=0x500000+n*4096;heap=10 if kind==5 else 1
        index=4 if kind==5 else 3
        cursor=(self.cursors[index]+align*4-1)&~(align*4-1)
        va=self.placement.va[index]+cursor;self.cursors[index]=cursor+words*4
        relative=self.heaps.relative(heap,va)
        x.uc.mem_write(cpu,bytes(words*4));x.put64(va_out,va)
        if rel_out:x.put64(rel_out,relative)
        self.allocations.append((cpu,words,kind,relative));x.ret(cpu)

path=ROOT/'build/firmware/tqx-heaps.so'
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-shared','-fPIC',
    ROOT/'tests/tqx_heap_oracle_wrapper.c','-o',path],check=True)
lib=C.CDLL(str(path));lib.bind_heap.argtypes=[C.c_void_p,C.c_uint32,C.POINTER(Placement)]
lib.build_heap.argtypes=lib.bind_heap.argtypes
lib.upload_pages.argtypes=lib.bind_heap.argtypes
lib.plan_heaps.argtypes=[C.POINTER(Plan)]
lib.relative.argtypes=[C.POINTER(Heap),C.c_uint64,C.c_uint32,C.c_uint32,C.POINTER(C.c_uint64)]
plan=Plan();lib.plan_heaps(C.byref(plan));ref=HeapOracle();x=ref.x
for external,(base,end) in ref.heaps.items():
    h=plan.heaps[external];assert (h.base,h.base+h.size)==(base,end)
# Both PE layouts share the relevant base/size descriptors (before reservation).
for idx in (0,1,2,10):
    assert x.pe.read(0x141030d90+idx*24,24)==x.pe.read(0x141030fa0+idx*24,24)
selectors=0
for usage in range(11):
    for flags in (0,1<<18,1<<8,(1<<8)|(1<<16),(1<<8)|(1<<4),1<<17):
        x.put32(0x232000,usage);x.put32(0x232010,flags)
        expected={1:6,2:5 if flags&(1<<17) else 4,9:3}.get(usage,8)
        if usage==0:expected=1 if flags&(1<<18) else (2 if flags&(1<<8) and flags&(1<<16) else (7 if flags&(1<<8) and not flags&(1<<4) else 0))
        assert x.run(0x1400931b8,[0x232000,0x232010],ref.ranges)==expected;selectors+=1
rng=random.Random(0x932938);relative_cases=0
for external,(base,end) in ref.available.items():
    h=plan.heaps[external]
    for offset in [0,1,15,4096,end-base-1]+[rng.randrange(end-base) for _ in range(64)]:
        out=C.c_uint64(0xdead)
        assert lib.relative(C.byref(h),base+offset,1,1,C.byref(out))==0
        assert out.value==ref.relative(external,base,offset);relative_cases+=1
packed=PackedOracle(ref);counts={}
lengths=[1,15,16,4095,4096,4097,0x8000000,0x8001001,0xffffffff]
cases=[(low,n,False) for low in range(16) for n in lengths]+[(low,n,True) for low in (0,1,7,15) for n in lengths]
for low,length,reserved in cases:
    req=Placement(Copy(0x100000001+low,0x200000003,length))
    for i,heap in enumerate((0,2,1,1,10)):
        base,end=ref.available[heap]
        req.va[i]=base+4096*rng.randrange(1,(end-base)//4096-5)
    if reserved:
        req.va[1]=0x84fff00000+4096*rng.randrange(0,251)
        req.va[2]=0x81ffc00000
        req.va[3]=0x81ffd03000+4096*rng.randrange(0,512)
        req.va[4]=0xf0ffe00000+4096*rng.randrange(0,512)
    if req.va[2]==req.va[3]:req.va[3]+=4096
    out=(C.c_ubyte*(C.sizeof(Image)+16))();C.memset(out,0xa5,C.sizeof(out))
    assert lib.build_heap(out,C.sizeof(out),C.byref(req))==0
    image=Image.from_buffer_copy(out);request=Request()
    assert lib.bind_heap(C.byref(request),C.sizeof(request),C.byref(req))==0
    assert request.command_va==req.va[0]
    packed.placement=req;packed.cursors=[0]*5
    reference=packed.run(req.copy.src,req.copy.dst,1,1,1,command_va=req.va[0],
        shader_base=ref.relative(2,req.va[1]),pds_base=ref.relative(1,req.va[2]),copy_bytes=length)
    alloc=reference['allocations'];assert bytes(image.pds_initial)==alloc[0][3]
    for i in range(image.count):
        group=alloc[1+4*i:5+4*i];program=image.programs[i]
        assert bytes(program.pds_execution)==group[0][3]
        assert bytes(image.sources[i])[:48]==group[1][3][:48]
        assert bytes(program.constants)==group[2][3] and bytes(program.pds_constants)==group[3][3]
        a=request.addresses[i]
        assert a.pds_execution_state==group[0][2] and a.source_descriptor_index==group[1][2]>>4
        assert a.constants_va==req.va[3]+52+i*80 and a.pds_constant_state==group[3][2]
    assert bytes(image.commands)[:image.command_bytes]==reference['command']
    for field in ('record','page_record','root_export'):assert bytes(getattr(image,field))==reference[field]
    assert packed.cursors[3]==16+80*image.count and packed.cursors[4]==64*image.count
    # Independently place original allocation bytes at their actual relative
    # addresses, plus both banks from the original 119250 constructor.
    pages=(C.c_ubyte*(36864+16))();C.memset(pages,0xa5,C.sizeof(pages))
    assert lib.upload_pages(pages,C.sizeof(pages),C.byref(req))==0
    expected=bytearray(36864)
    expected[:len(reference['command'])]=reference['command']
    bank=bytes(packed.x.uc.mem_read(0x400000,20272))
    expected[4096:4096+20096]=bank[:20096]
    expected[24576:24576+176]=bank[20096:]
    for words,kind,relative,data in alloc:
        region=32768 if kind==5 else 28672
        placement=req.va[4 if kind==5 else 3]
        heap=10 if kind==5 else 1
        at=region+relative-(placement-ref.heaps[heap][0])
        expected[at:at+words*4]=data
    assert bytes(pages)==bytes(expected)+b'\xa5'*16
    assert bytes(out)[C.sizeof(Image):]==b'\xa5'*16
    counts[image.count]=counts.get(image.count,0)+1
saved=bytes(out);bad=[]
for i,heap in enumerate((0,2,1,1,10)):
    base,end=ref.available[heap]
    for va in (base-4096,base+plan.heaps[heap].size if heap==10 else end,
               base+plan.heaps[heap].size+4096 if heap==10 else end-4096 if i==1 else end+4096,req.va[i]+1,1<<40):
        r=Placement.from_buffer_copy(bytes(req));r.va[i]=va;bad.append(r)
r=Placement.from_buffer_copy(bytes(req));r.va[3]=r.va[2];bad.append(r)
for r in bad:assert lib.build_heap(out,C.sizeof(out),C.byref(r))<0 and bytes(out)==saved
# exact available end, full resource extent fits; shader is five pages.
for i,heap in enumerate((0,2,1,1,10)):
    r=Placement.from_buffer_copy(bytes(req));r.va[i]=ref.available[heap][1]-(20480 if i==1 else 4096)
    assert lib.build_heap(out,C.sizeof(out),C.byref(r))==0
report=dict(utc=datetime.now(timezone.utc).isoformat(),passed=True,reference_sha256=x.pe.sha256,
    heap_ranges={str(k):[hex(v) for v in vals] for k,vals in ref.heaps.items()},
    ordinary_allocatable_ranges={str(k):[hex(v) for v in vals] for k,vals in ref.available.items()},
    selector_cases=selectors,relative_cases=relative_cases,stream_cases=len(cases),reserved_pool_cases=36,chunk_counts=counts,
    rejected_placements=len(bad),heap_endpoint_cases=5,
    packed_upload_cases=len(cases),packed_upload_bytes=36864,
    executed=['02a34c/01ccd8 MMU configuration and reserved heap table','042000 SDK full heap descriptor getter','093494/092938 external to internal heaps',
              '0d30f0/092b50/08ce98/0a1bd8 usage to resource heap and VA initialization',
              '0a21f8 resource VA plus offset to heap relative address','0931b8 platform allocation selector',
              '045374 command pool selector','full 10f034 copy stream through 11d194/11d6bc'],
    modeled=['OS allocation/locks, CPU memory helpers','state suballocation uses Linux page packing',
             'command page allocation and final ownership transfer'],
    hardware_access=False,gpu_execution=False,
    limits='Heap address conversion and packed state are verified in RAM. No resources uploaded, no firmware submission or GPU execution.')
(ROOT/'reports/tqx-heap-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
