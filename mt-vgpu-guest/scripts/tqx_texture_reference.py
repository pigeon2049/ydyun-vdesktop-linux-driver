"""Restricted original family-2 TQX source descriptor execution in RAM."""
import json
from pathlib import Path
import struct
from reference_oracle import ReferenceOracle
from unicorn.x86_const import UC_X86_REG_RAX, UC_X86_REG_RIP

class TextureOracle:
    DEVICE, MODULE, CMD = 0x200000,0x205000,0x210000
    CHUNK, SURFACE, STATE, VIEW, OUTPUT = 0x214000,0x215000,0x216000,0x217000,0x500000
    def __init__(self):
        self.x=ReferenceOracle();x=self.x
        x.hooks[0x140130c50]=lambda:x.ret(x.uc.reg_read(UC_X86_REG_RAX))
        x.hooks[0x140044efc]=lambda:(_ for _ in ()).throw(AssertionError('reference assertion'))
        x.put64(0x140138420,0x2f8810)
        x.hooks[0x2f8810]=lambda:x.uc.reg_write(UC_X86_REG_RIP,x.uc.reg_read(UC_X86_REG_RAX))
        # Explicit CPU format/geometry/dispatch functions only; no OS or I/O.
        addresses='''0948c8 11bbe4 04b2ac 04a59c 04a2f0 045138 08deb8 0ae5f8
            04c2fc 07b6bc 078ae8 07b0b8 07b94c 05c30c 0acd90 04220c 056124
            05adac 098d48 0b0c98 0b08b8 047680 0475a4 04377c 043754 04b440
            0b0dbc 0b10fc 0b0df4 043658 043700 0437a8 043834 043888 0436ac
            0437e0 0b16b0 0b167c 0b15fc 0421dc 043680 07b9b4 07b96c 05632c
            0949fc 078d18 0aec68 046950 0461bc 046228 046c60 0465d4 0b0cac
            0b0d28 0b0cc4 0acd50 0b08a4 04a2d4 0a5740 0a5ee0 0571e0 056a5c'''
        entries=sorted(int(e['address'],16) for e in map(json.loads,
            (Path(__file__).resolve().parents[1]/'decompiled/mtkm64.sys/functions.jsonl').read_text().splitlines())
            if not e.get('external',False))
        self.ranges=[(a,next(e for e in entries if e>a)) for a in
                     (0x140000000+int(s,16) for s in addresses.split())]
        x.put32(self.DEVICE+0x380,2)
        x.put32(self.DEVICE+0x71c,32);x.put32(self.DEVICE+0x720,16)
        x.put64(self.MODULE+8,self.DEVICE);x.put64(self.CMD+0xb68,self.MODULE)
        assert x.run(0x1400948c8,[1,self.DEVICE+8,self.DEVICE+16,self.DEVICE+24,
                     self.DEVICE+32,self.DEVICE+0x29a0,self.DEVICE+0x29a8,self.DEVICE+0x29b0],self.ranges)==0
        assert x.get64(self.DEVICE+0x29a0)==0x1400ae5f8
        assert x.get64(self.DEVICE+32)==0x1400aec68
        x.put64(self.VIEW,self.SURFACE);x.put64(self.VIEW+8,self.STATE)
        x.hooks[0x1400a354c]=self.allocate
        self.allocations=[]

    def allocate(self):
        x=self.x
        assert (x.arg(1)&0xffffffff,x.arg(2)&0xffffffff,x.arg(4)&0xffffffff)==(16,8,5)
        x.put64(x.arg(3),0x800000)
        x.put64(x.arg(5),self.heap_offset)
        x.uc.mem_write(self.OUTPUT,bytes(64))
        self.allocations.append((16,8,5));x.ret(self.OUTPUT)

    def run(self,src,element,width,height,heap_offset=0x1020):
        x=self.x;self.heap_offset=heap_offset;self.allocations.clear()
        x.uc.mem_write(self.CHUNK,struct.pack('<Q4I',0,element,1,width,height))
        x.run(0x1400a5740,[self.CHUNK,self.SURFACE,self.STATE],self.ranges)
        x.put64(self.VIEW+16,src)
        result=x.run(0x14011bbe4,[self.CMD,self.VIEW,1,0],self.ranges)
        assert self.allocations==[(16,8,5)] and result==heap_offset>>4
        return bytes(x.uc.mem_read(self.OUTPUT,64))
