"""Bounded Windows UMD QY1 graphics register/descriptor construction in RAM.

Raw render/vertex states are fixtures, not proof of valid GPU resources or
draw commands. No PE entry point, Windows OS code, driver ioctl or device is
run. These original functions are distinct from the kernel TQX copy library.
"""
import json
from pathlib import Path
import struct
from reference_oracle import ReferenceOracle
from unicorn.x86_const import UC_X86_REG_RAX
ROOT=Path(__file__).resolve().parents[1]
SHA='3f2c843d3404d37b2beea9f97ebcc1e6207d11948c5da2c4581ccd4d8bc5ba0c'

class GfxOracle(ReferenceOracle):
    DEVICE=0x400000; META=0x405000; TA=0x406000; RT=0x407000
    CONTEXT=0x409000; OUT=0x40a000; OBJ=0x410000; VTABLE=0x411000
    BASES=0x412000; BATCH=0x413000; STATE=0x414000; FEATURES=0x417000
    LIST=0x418000; REGION=0x419000; SCRATCH=0x41a000; PARAM=0x41e000
    MODULE=0x430000; DEVVT=0x431000; SHAPE=0x432000
    LAYOUT=0x433000; OUTER=0x434000; DMA=0x440000
    def __init__(self):
        super().__init__('/opt/MTT-driver-only/mtdxum64.dll',SHA)
        entries=sorted(int(e['address'],16) for e in map(json.loads,
            (ROOT/'decompiled/mtdxum64.dll/functions.jsonl').read_text().splitlines()) if not e.get('external'))
        self.permitted=[]
        for address in (0x180158f90,0x180159520,0x18015b880,0x180158cc0,
                        0x180181ec0,0x18015afa0):
            self.permitted.append((address,next(e for e in entries if e>address)))
        # Leaf not identified as a separate Ghidra function; bounded using
        # disassembly from its entry through the return, excluding padding.
        self.permitted.append((0x180157d80,0x180157eee))
        self.hooks[0x1802df520]=self._memset
        self.hooks[0x1802ded00]=self._memcpy
        def cookie():
            assert self.arg(0)==self.get64(0x1818a90c0)
            self.ret(self.uc.reg_read(UC_X86_REG_RAX))
        self.hooks[0x1802a9d50]=cookie
        self.uc.mem_write(self.DEVICE,bytes(0x20000))
        self.put32(self.DEVICE+0x710,2)
        self.put64(self.OBJ,self.VTABLE)
        self.put64(self.VTABLE+0x4c0,0x2f8800)
        self.hooks[0x2f8800]=lambda:self.ret(self.BASES)
    def vertex_registers(self,meta,ta):
        assert len(meta)==0xe8 and len(ta)==0x78
        self.uc.mem_write(self.META,meta);self.uc.mem_write(self.TA,ta)
        self.uc.mem_write(self.OUT,b'\xa5'*0xc0)
        end=self.run(0x180158f90,[self.DEVICE,self.META,self.TA,self.PARAM,self.OUT],self.permitted)
        assert end==self.OUT+0xb0 and bytes(self.uc.mem_read(end,16))==b'\xa5'*16
        return bytes(self.uc.mem_read(self.OUT,0xb0))
    def fragment_registers(self,meta,rt,context,base):
        assert len(meta)==0xe8 and len(rt)==0x400 and len(context)==0x40
        assert struct.unpack_from('<I',rt,0x148)[0]<=8
        self.uc.mem_write(self.META,meta);self.uc.mem_write(self.RT,rt)
        self.uc.mem_write(self.CONTEXT,context);self.put64(self.BASES,base)
        self.uc.mem_write(self.OUT,b'\xa5'*0x170)
        end=self.run(0x180159520,[self.DEVICE,self.OBJ,self.RT,0,self.META,self.CONTEXT,0,self.OUT],self.permitted)
        assert end==self.OUT+0x160 and bytes(self.uc.mem_read(end,16))==b'\xa5'*16
        return bytes(self.uc.mem_read(self.OUT,0x160))
    def record(self,meta,ta,rt,batch,base=0,context_word=0,rt_word=0):
        """Original family-2 combined TA/3D record, one command-list entry.

        The callbacks only expose supplied CPU-side state. They do not claim
        to generate shaders, render-target state, or valid GPU addresses.
        Optional fence/indirect lists are excluded from this initial fixture.
        """
        assert len(meta)==0xe8 and len(ta)==0x78 and len(rt)==0x400 and len(batch)==0x798
        assert all(struct.unpack_from('<I',batch,o)[0]==0 for o in (0x618,0x670,0x750,0x760,0x770,0x780))
        assert batch[0x61c]==0
        assert struct.unpack_from('<I',meta,0x80)[0]==0
        assert struct.unpack_from('<I',rt,0x148)[0]<=8
        self.uc.mem_write(self.META,meta);self.uc.mem_write(self.TA,ta)
        self.uc.mem_write(self.RT,rt);self.uc.mem_write(self.BATCH,batch)
        self.put64(self.BASES,base)
        self.uc.mem_write(self.STATE,bytes(0x3000));self.put64(self.STATE+0x1e68,self.BATCH)
        self.put32(self.STATE+0x1e70,1)
        self.put64(self.DEVICE+0x2d80,self.FEATURES);self.put32(self.DEVICE+0x2cc0,1)
        self.uc.mem_write(self.PARAM,bytes(0x100));self.put32(self.PARAM,1);self.put32(self.PARAM+4,1)
        self.put64(self.PARAM+8,self.LIST);self.put64(self.LIST,self.OBJ)
        self.put64(self.PARAM+0x20,self.SCRATCH);self.put64(self.PARAM+0x28,context_word)
        self.uc.mem_write(self.REGION,bytes(0x40));self.put32(self.REGION+8,0x3580)
        self.put32(self.REGION+0x20,0x80);self.put32(self.REGION+0x24,0x3580)
        def metadata():
            assert self.arg(0)==self.OBJ and self.arg(1)==0
            self.uc.mem_write(self.arg(2),meta);self.ret()
        callbacks={0x4b8:lambda:self.ret(self.STATE),0x3f8:lambda:self.ret(0),0x540:metadata,
            0x568:lambda:self.ret(rt_word),0x538:lambda:self.ret(self.RT),0x530:lambda:self.ret(self.TA)}
        for i,(off,callback) in enumerate(callbacks.items()):
            stub=0x2f8810+16*i;self.put64(self.VTABLE+off,stub);self.hooks[stub]=callback
        self.uc.mem_write(self.OUT,b'\xa5'*(0x3580+0x80+16))
        assert self.run(0x18015b880,[self.DEVICE,0,self.PARAM,self.REGION,self.OUT,0x50000000,0],self.permitted)==0
        assert bytes(self.uc.mem_read(self.OUT+0x3600,16))==b'\xa5'*16
        return dict(region_header=bytes(self.uc.mem_read(self.OUT+0x68,0x18)),
            record=bytes(self.uc.mem_read(self.OUT+0x80,0x3580)))

    def packet(self,meta,ta,rt,batch,base=0,context_word=0,rt_word=0,
               dma_va=0x50000000,frame=0,job_ref=0,pid=1,state_va=0):
        """Original layout calculation plus complete single-region serializer.

        The RAM layout object is populated from the calculated shape, without
        invoking the Windows resource allocator. GetCurrentProcessId and stack
        probing use bounded RAM models. Optional regions/lists remain disabled.
        The zero-initialized CSW is a fixture, not an initialized GPU context.
        """
        reference=self.record(meta,ta,rt,batch,base,context_word,rt_word)
        self.uc.mem_write(self.MODULE,bytes(0x200))
        self.put64(self.MODULE+8,self.DEVICE)
        self.put64(self.MODULE+0x10,self.MODULE+0x100)
        self.put64(self.MODULE+0x108,self.DEVICE)
        self.put64(self.DEVICE,self.DEVVT)
        self.put64(self.DEVVT+0x18,0x2f88c0)
        self.hooks[0x2f88c0]=lambda:self.ret(self.FEATURES)
        self.run(0x180157d80,[self.DEVICE],self.permitted)
        self.uc.mem_write(self.OUTER,bytes(0x200))
        self.put32(self.OUTER,1)
        self.uc.mem_write(self.OUTER+0x18,bytes(self.uc.mem_read(self.PARAM,0x88)))
        self.put64(self.OUTER+0x1d8,state_va)
        self.uc.mem_write(self.SHAPE,bytes(0x128))
        self.put32(self.SHAPE+8,1)
        self.run(0x180181ec0,[self.MODULE,self.OUTER,self.SHAPE],self.permitted)
        shape=bytes(self.uc.mem_read(self.SHAPE,0x128))
        total=self.get32(self.SHAPE+0x118)
        assert total==0x46c0 and self.get32(self.SHAPE+0x11c)==2
        self.uc.mem_write(self.LAYOUT,bytes(0x140))
        self.put64(self.LAYOUT+8,self.DMA);self.put64(self.LAYOUT+0x10,dma_va)
        self.uc.mem_write(self.LAYOUT+0x18,shape)
        self.put64(self.OUTER+0x10,self.LAYOUT)
        self.put32(self.DEVICE+0x3200,frame)
        self.put64(0x1802e2328,0x2f88d0)
        self.hooks[0x2f88d0]=lambda:self.ret(pid)
        def stack_probe():
            n=self.uc.reg_read(UC_X86_REG_RAX)
            assert n==0x1128
            self.ret(n)
        self.hooks[0x1802aaa70]=stack_probe
        self.uc.mem_write(self.DMA,bytes(total)+b'\xa5'*16)
        assert self.run(0x18015afa0,[self.DEVICE,0,self.OUTER,job_ref],self.permitted)==0
        assert bytes(self.uc.mem_read(self.DMA+total,16))==b'\xa5'*16
        packet=bytes(self.uc.mem_read(self.DMA,total))
        record_offset=self.get32(self.LAYOUT+0x40)
        assert packet[record_offset:record_offset+0x3580]==reference['record']
        assert packet[record_offset-0x18:record_offset]==reference['region_header']
        return dict(packet=packet,shape=shape,record=reference['record'],
                    record_offset=record_offset,csw_offset=self.get32(self.LAYOUT+0x138))

if __name__=='__main__':
    x=GfxOracle()
    print('vertex',x.vertex_registers(bytes(0xe8),bytes(0x78)).hex())
    print('fragment',x.fragment_registers(bytes(0xe8),bytes(0x400),bytes(0x40),0).hex())
