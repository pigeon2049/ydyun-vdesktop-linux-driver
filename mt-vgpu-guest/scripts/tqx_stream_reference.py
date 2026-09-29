"""Original family-2 single-page copy job and record finalization in RAM."""
import json
from pathlib import Path
import struct
from tqx_destination_reference import DestinationOracle

ROOT=Path(__file__).resolve().parents[1]
class StreamOracle(DestinationOracle):
    HANDLER, LAYOUT, VIEWS = 0x220000,0x224000,0x230000
    COMMAND, RECORDS = 0x600000,0x900000
    def __init__(self):
        super().__init__();x=self.x
        names='''0d9340 119250 056a00 113c08 116348 11e5bc 0fbab8 11b7f0
            1169fc 114290 051bf0 051dbc 11ccdc 11d0f8 0fbc0c 11e4c4
            11e6dc 1168e4 115140 1151a8 11e618 11e634 0db49c
            11a480 0a4140 0a4540 0e3398 11a62c 0da520 0db228 0db3c8
            0dbff4 0450c8 093d74 0dad24 11b3cc 0a3e38 0dc894 119cd0
            11e768 11b7c0 0bb914 0dad64 11df28 04528c 046cc0 0dcbf8 0a34e4 0db160 11d194 0dae88 0553b8 0bb278 04d544 11d378 11de48 11d488
            11a320 11ddc4 045458 119d70 04c3c4 04c6e8 04c4e8 04d4a0 04cfd8
            11a1b4 04d518 048064 04d4dc 04d030 0db2c8 0d981c 11a0bc
            04d0e4 04cc38 089a00 091470 04b2e0
            0d9b08 119e88 11964c 0a24f8 0a28bc 0a2440 0a2498 0a445c
            119e24 0fdac8 04b548 04569c 0a2394 0a23dc 0bc878
            10f034 0a624c 0a5314 04b2bc 114418 0a6168 0a8ce0 130e80 11d6bc 11a21c 11a2b4'''
        entries=sorted(int(e['address'],16) for e in map(json.loads,
            (ROOT/'decompiled/mtkm64.sys/functions.jsonl').read_text().splitlines())
            if not e.get('external',False))
        self.ranges += [(a,next(e for e in entries if e>a)) for a in
            (0x140000000+int(s,16) for s in names.split())]
        x.put64(self.HANDLER+8,self.MODULE)
        selected=[]
        x.hooks[0x1400d9518]=x.ret
        def select():selected.append((x.arg(1),x.arg(2)));x.ret()
        x.hooks[0x140113b80]=select
        x.run(0x1400d9340,[self.HANDLER],self.ranges)
        assert selected==[(0x140b9e850,0x140b9ea30)]
        x.hooks[0x140046cd8]=lambda:x.ret(0x207000)
        x.hooks[0x1400541a4]=lambda:x.ret(0x208000)
        x.hooks[0x140046df0]=lambda:x.ret(0x400000)
        assert x.run(0x140119250,[self.HANDLER,*selected[0],self.LAYOUT],self.ranges)==0
        # Actual device/module vtables from 04c518/09bb68. The property
        # initializer sets device+28e6 (feature+246) to zero itself. Only
        # unrelated OS platform configuration at its end uses a RAM getter.
        x.put64(self.MODULE,0x140603ff8);x.put64(self.DEVICE,0x1403c4d40)
        x.put64(self.DEVICE+0x30,0x260000);x.put64(0x260000,0x261000)
        x.put64(0x261000+0x30,0x2f8850);x.hooks[0x2f8850]=lambda:x.ret(0x262000)
        assert x.run(0x140091470,[self.DEVICE],self.ranges)==0
        assert bytes(x.uc.mem_read(self.DEVICE+0x28e6,1))==b'\0'
        x.hooks[0x1401140a4]=lambda:x.ret((self.shader_base if x.arg(1)==0 else self.pds_base)+x.arg(2))
        x.hooks[0x1400a354c]=self.allocate_state
        x.hooks[0x1400fd3a4]=self.reserve_command
        x.hooks[0x1400fc600]=self.commit_command
        self.allocations=[]
        def compare_memory():
            a,b,n=[x.arg(i) for i in range(3)]
            assert n<=0x128
            aa,bb=bytes(x.uc.mem_read(a,n)),bytes(x.uc.mem_read(b,n))
            x.ret(((aa>bb)-(aa<bb)) & 0xffffffffffffffff)
        x.hooks[0x140130e80]=compare_memory
        x.hooks[0x140119914]=x.ret # Allocation ownership finalization, not encoding.
        def command_allocator():
            assert tuple(x.arg(i) for i in (3,4,5,6))==(5,0,1,20)
            x.ret(self.ALLOCATOR)
        x.hooks[0x140119c04]=command_allocator
        def command_gpu_end():
            assert x.arg(0)==self.ALLOCATOR
            x.ret(self.command_va+80)
        x.hooks[0x1400fcac0]=command_gpu_end

    def allocate_state(self):
        x=self.x
        words,align,va_out,kind,rel_out=[x.arg(i) for i in (1,2,3,4,5)]
        words&=0xffffffff;align&=0xffffffff;kind&=0xffffffff
        assert (kind,words,align) in ((4,4,4),(4,9,4),(5,16,8),(4,5,1)),(kind,words,align)
        n=len(self.allocations);cpu=0x500000+n*4096
        sources=sum(k==5 for _,_,k,_ in self.allocations)
        gpu=self.constants_va+(sources-1)*4096 if (kind,words,align)==(4,5,1) else 0x800000+n*4096
        relative=(self.descriptor_index+sources*256)<<4 if kind==5 else self.state_base+n*4096
        x.uc.mem_write(cpu,bytes(words*4));x.put64(va_out,gpu)
        if rel_out:x.put64(rel_out,relative)
        self.allocations.append((cpu,words,kind,relative));x.ret(cpu)

    def reserve_command(self):
        assert self.x.arg(0)==self.ALLOCATOR
        if not self.command_page_allocated:
            self.x.put32(self.ALLOCATOR+0x98,1)
            self.command_page_allocated=True
        self.x.ret(self.COMMAND+self.command_used)

    def commit_command(self):
        x=self.x
        allocator,end,flag=[x.arg(i) for i in range(3)]
        assert allocator==self.ALLOCATOR and flag==1
        assert self.COMMAND+self.command_used<=end<=self.COMMAND+4096
        self.command_used=end-self.COMMAND;x.ret()

    def run(self,src,dst,element,width,height,constants_va=0x1234000,
            command_va=0x4000000,shader_base=0x10000,pds_base=0x7000,
            state_base=0x9000,descriptor_index=0x1234,copy_bytes=None,
            fill_words=None,fill_rect=None):
        x=self.x
        self.shader_base=shader_base;self.pds_base=pds_base;self.constants_va=constants_va
        self.state_base=state_base;self.descriptor_index=descriptor_index
        self.allocations.clear();self.command_used=0
        self.command_page_allocated=False;self.command_va=command_va
        x.uc.mem_write(self.CMD,bytes(0x2800))
        x.uc.mem_write(0x270000,struct.pack('<Q8I',0,5,0,0,0,0,0,0,0))
        x.put32(self.DEVICE+0x394,1)
        x.run(0x1400d9b08,[self.CMD,self.MODULE,0x270000],self.ranges)
        assert x.get64(self.CMD)==0x140baef10
        x.uc.mem_write(self.COMMAND,bytes(4096));x.uc.mem_write(self.RECORDS,bytes(0x128*4))
        x.put64(self.CMD,0x140baef10);x.put64(self.CMD+0xb68,self.MODULE)
        x.put64(self.CMD+0xb58,self.ALLOCATOR)
        x.put32(self.ALLOCATOR+0x98,0)
        x.put32(self.ALLOCATOR+0x198,20) # 0d9b08 allocates 20-DWORD windows.
        x.put64(self.CMD+0x2050,self.RECORDS);x.put32(self.CMD+0x205c,4)
        x.put64(self.CMD+0x2170,0x910000);x.put32(self.CMD+0x217c,4)
        if copy_bytes is not None:
            # Resource addresses include nonzero offsets; execute actual split,
            # operation selection and all jobs, without replacing emission.
            x.put64(0x280008,src-7);x.put64(0x281008,dst-11)
            x.uc.mem_write(0x282000,struct.pack('<QQQ',7,11,copy_bytes))
            x.run(0x14010f034,[self.HANDLER,self.CMD,0x280000,0x281000,1,0x282000],self.ranges)
        else:
            x.uc.mem_write(self.CHUNK,struct.pack('<Q4I',0,element,1,width,height))
            for off,surface,state,rect,va in ((0,0x231000,0x232000,0x233000,dst),
                                           (64,0x231100,0x232100,0x233100,src)):
                x.run(0x1400a5740,[self.CHUNK,surface,state],self.ranges)
                x.put64(self.VIEWS+off,surface);x.put64(self.VIEWS+off+8,state)
                x.put64(self.VIEWS+off+16,va);x.put64(self.VIEWS+off+40,rect)
                x.uc.mem_write(rect,struct.pack('<4I',0,0,width,height))
            if fill_words is not None:
                assert copy_bytes is None and len(fill_words)==4
                x.uc.mem_write(self.PARAMS,struct.pack('<Q4I',0,*fill_words))
                x.uc.mem_write(self.JOB,struct.pack('<5Q',1,self.VIEWS,self.PARAMS,0,0))
                if fill_rect is not None:
                    x.uc.mem_write(0x233000,struct.pack('<4I',*fill_rect))
                x.run(0x14010ae38,[self.HANDLER,self.CMD,self.JOB],self.ranges)
            else:
                operation=8 if element<=4 else (16 if element==8 else 21)
                x.run(0x140113c08,[self.HANDLER,self.CMD,self.VIEWS,operation,0],self.ranges)
        self.before_finish=bytes(x.uc.mem_read(self.COMMAND,self.command_used))
        assert x.get64(self.CMD+0x2290)==self.COMMAND+self.command_used-8 and x.get64(self.CMD+0x2298)==0
        assert x.run(0x14011d194,[self.CMD],self.ranges)==0
        count=struct.unpack('<I',x.uc.mem_read(self.CMD+0x2058,4))[0]
        assert count==1,(count,self.command_used)
        assert x.get64(self.CMD+0x2290)==0
        assert x.get64(0x910000)==command_va and x.get64(0x910008)==self.command_used
        x.uc.mem_write(0x920000,bytes(32))
        assert x.run(0x14011d6bc,[self.CMD,0x920000],self.ranges)==0
        assert x.get64(0x920000)==1 and x.get64(0x920008)==command_va
        return dict(root_export=bytes(x.uc.mem_read(0x920000,16)),page_record=bytes(x.uc.mem_read(0x910000,16)),command=bytes(x.uc.mem_read(self.COMMAND,self.command_used)),
                    record=bytes(x.uc.mem_read(self.RECORDS,0x128)),
                    allocations=[(n,k,r,bytes(x.uc.mem_read(cpu,n*4))) for cpu,n,k,r in self.allocations])

if __name__=='__main__':
    o=StreamOracle();r=o.run(0x123456781,0x234567891,4,17,32)
    print('command',r['command'].hex())
    print('record',[(hex(i),hex(struct.unpack_from('<I',r['record'],i)[0])) for i in range(0,0x128,4) if any(r['record'][i:i+4])])
    print('allocations',[(n,k,hex(rel),data.hex()) for n,k,rel,data in r['allocations']])
