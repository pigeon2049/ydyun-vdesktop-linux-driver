#!/usr/bin/env python3
"""Execute original system-RAM allocation/translation and compare Linux SG VM."""
import ctypes as C
from datetime import datetime, timezone
import json
from pathlib import Path
import random
import struct
import subprocess
from reference_oracle import ReferenceOracle

ROOT=Path(__file__).resolve().parents[1]
GPU,INFO,OS=0x400000,0x404000,0x406000

class SystemMemoryOracle(ReferenceOracle):
    def __init__(self,raw,fail=0):
        super().__init__()
        self.allocations=[];self.frees=[];self.physical_calls=[];self.fail=fail;self.attempts=0
        self.uc.mem_write(INFO,raw)
        inner=GPU+8
        self.put64(inner+0x10,OS);self.put64(OS+0x28,16<<30)
        self.put64(inner+0x1cb0,INFO);self.uc.mem_write(inner+0x44,b'\1')
        self.put64(inner+0x9d8,0x8000000000)
        self.put64(inner+0x400,0x800000000);self.put64(inner+0x408,0x400000000)
        assert self.run(0x140026390,[inner],[(0x140026390,0x1400265cb)])==0
        self.windows=bytes(self.uc.mem_read(inner+0x400,0x118))
        for iat,stub,hook in ((0x1401380d0,0x2f8100,self.pool),
                              (0x1401380d8,0x2f8110,self.free),
                              (0x1401381b8,0x2f8120,self.physical)):
            self.put64(iat,stub);self.hooks[stub]=hook
        self.ranges_allowed=[(0x140022f38,0x140023041),(0x140021d90,0x140021ede),
                             (0x140023044,0x14002306b)]
    def pool(self):
        self.attempts+=1
        assert self.arg(0)==0x200 and self.arg(2)==0x49474150
        if self.attempts==self.fail:self.ret(0);return
        size=self.arg(1);self.cursor=(self.cursor+4095)&~4095
        p=self.allocate(size);self.uc.mem_write(p,b'\xa5'*size)
        self.allocations.append((p,size));self.ret(p)
    def free(self):
        self.frees.append(self.arg(0));self.ret()
    def physical(self):
        offset=self.arg(0)-self.allocations[1][0]
        assert not offset%4096
        self.physical_calls.append(offset)
        self.ret(self.gpas[offset//4096])
    def allocate_system(self,gpas,requested=None):
        self.gpas=gpas
        size=len(gpas)*4096 if requested is None else requested
        descriptor=self.run(0x140022f38,[GPU,size,0x49474150,1],self.ranges_allowed)
        if self.fail:
            assert not descriptor and not self.physical_calls
            assert self.frees==([] if self.fail==1 else [self.allocations[0][0]])
            return []
        assert len(self.allocations)==2
        assert self.allocations[0]==(descriptor,0x30+len(gpas)*8)
        assert self.allocations[1][1]==len(gpas)*4096
        assert self.get64(descriptor)==self.allocations[1][0]
        assert struct.unpack('<I',self.uc.mem_read(descriptor+8,4))[0]==size
        assert self.get64(descriptor+0x10)==descriptor+0x30
        assert bytes(self.uc.mem_read(self.allocations[1][0],len(gpas)*4096))==b'\xa5'*(len(gpas)*4096)
        assert self.physical_calls==list(range(0,len(gpas)*4096,4096))
        result=list(struct.unpack('<'+'Q'*len(gpas),self.uc.mem_read(descriptor+0x30,len(gpas)*8)))
        self.run(0x140023044,[descriptor],self.ranges_allowed)
        assert self.frees==[self.allocations[1][0],descriptor]
        return result


def main():
    library=ROOT/'build/firmware/system-memory.so'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-shared','-fPIC',
                    ROOT/'tests/system_memory_oracle_wrapper.c','-o',library],check=True)
    lib=C.CDLL(str(library));ptr=C.POINTER(C.c_uint64)
    lib.system_translate.argtypes=[C.c_void_p,C.c_void_p,ptr,C.c_uint32,ptr,C.c_uint64,C.c_uint64]
    lib.system_vm.argtypes=[ptr,C.c_uint32,C.c_uint32,C.c_uint32,ptr]
    raw=(ROOT/'reports/device-info.bin').read_bytes();rng=random.Random(22938);cases=[]
    for flags in (0x3d1,0x3d3,0x393):
        info=bytearray(raw);struct.pack_into('<Q',info,0x10,flags);info=bytes(info)
        for n in (1,2,17,1024):
            gpas=[0x10000000+i*8192 for i in range(n)];rng.shuffle(gpas)
            x=SystemMemoryOracle(info);expected=x.allocate_system(gpas)
            source=(C.c_uint64*n)(*gpas);out=(C.c_uint64*n)();walked=(C.c_uint64*n)()
            assert lib.system_translate(info,x.windows,source,n,out,0,0)==0
            assert list(out)==expected
            assert lib.system_vm(out,n,0,n,walked)==0 and list(walked)==expected
            if n>2:
                assert lib.system_vm(out,n,1,n-2,walked)==0
                assert list(walked)[:n-2]==expected[1:-1]
            cases.append(dict(flags=hex(flags),pages=n,noncontiguous=n>1,full_page_walk=True))
    for fail in (1,2):SystemMemoryOracle(raw,fail).allocate_system([0x10000000]*2)
    x=SystemMemoryOracle(raw);assert len(x.allocate_system([0x10000000,0x10002000],5001))==2
    # Unsupported Host register translation must fail without touching output.
    info=bytearray(raw);struct.pack_into('<Q',info,0x10,0x391)
    out=(C.c_uint64*1)(0xdeadbeef);gpa=(C.c_uint64*1)(0x10000000)
    assert lib.system_translate(bytes(info),x.windows,gpa,1,out,0,0)<0 and out[0]==0xdeadbeef
    rejected=0
    for pa in (0,1,0x800000000,0xbfffff000,0x7800000000,0xfffffffffffff000):
        gpa[0]=pa;out[0]=0xdeadbeef
        assert lib.system_translate(raw,x.windows,gpa,1,out,0,0)<0 and out[0]==0xdeadbeef
        rejected+=1
    gpa[0]=0x10000000
    assert lib.system_translate(raw,x.windows,gpa,1,out,0x10000000,4096)<0
    # Bad last scatter page, out-of-range PA and table aliases never map.
    for pa in (0x8800000001,1<<40,0x600000000,0x60000f000):
        pages=(C.c_uint64*2)(0x8810000000,pa);walked=(C.c_uint64*2)(0xdeadbeef,0xdeadbeef)
        assert lib.system_vm(pages,2,0,2,walked)<0 and list(walked)==[0xdeadbeef]*2
    report=dict(utc=datetime.now(timezone.utc).isoformat(),passed=True,reference_sha256=x.pe.sha256,
        cases=cases,total_compared_pages=sum(c['pages'] for c in cases),allocator_failures=2,
        rounded_allocation_case=True,invalid_address_cases=rejected+2,invalid_scatter_cases=4,
        executed=['026390 Guest window refresh','022f38 system-pool allocation and page enumeration',
                  '021d90 GPA translation','023044 paired pool free'],
        modeled=['ExAllocatePoolWithTag/ExFreePoolWithTag RAM','MmGetPhysicalAddress noncontiguous GPA lists'],
        live_profile_bias='0x8800000000 (cached device-info, not a fresh hardware query)',
        main_module_loaded=False,hardware_access=False,gpu_execution=False)
    (ROOT/'reports/system-memory-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='cases'},indent=2))

if __name__=='__main__':main()
