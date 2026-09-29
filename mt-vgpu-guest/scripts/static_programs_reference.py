"""Execute shared PDS/USC generation, copy and temporary-bank release in RAM."""
import json
from pathlib import Path
from tqx_stream_reference import StreamOracle

ROOT=Path(__file__).resolve().parents[1]
class StaticProgramsOracle(StreamOracle):
    PLATFORM,CONTEXT,PDS,USC=0xa00000,0xb00000,0xc00000,0xd00000
    def __init__(self):
        super().__init__();x=self.x
        entries=sorted(int(e['address'],16) for e in map(json.loads,
            (ROOT/'decompiled/mtkm64.sys/functions.jsonl').read_text().splitlines()) if not e.get('external'))
        names='01a898 021888 02186c 04d758 04d708 09c7a8 09c788 0d93ac 0d94d0 0d9518 0fbca4 04567c'
        self.ranges += [(a,next(e for e in entries if e>a)) for a in (int('140'+n,16) for n in names.split())]
        # Real device and transfer-module vtables; no hook on the getter,
        # bank builder, static-table self-check or PDS/USC copy decisions.
        x.hooks.pop(0x1400d9518)
        x.put64(self.DEVICE+0x368,self.MODULE)
        x.put64(self.MODULE+0x30,self.MODULE)
        x.put64(self.CONTEXT+0x38,self.DEVICE)
        x.put64(self.PLATFORM+0x60,self.CONTEXT+0x1000)
        x.put64(self.PLATFORM+0xa0,self.CONTEXT+0x1040)
        x.put64(self.CONTEXT+0x1000,self.PDS)
        x.put64(self.CONTEXT+0x1040,self.USC)
        self.copies=[];self.releases=[];self.allocations=[]
        x.hooks[0x140046cd8]=self.allocate_bank
        x.hooks[0x140045ca0]=self.release_bank
        original_copy=x._memcpy
        def copy():
            dst,src,n=(x.arg(i) for i in range(3))
            if dst in (self.PDS,self.USC):self.copies.append((dst,src,n))
            original_copy()
        x.hooks[0x140130f80]=copy
    def allocate_bank(self):
        self.allocations.append(self.x.arg(1));self.x.ret(0x207000)
    def release_bank(self):
        x=self.x
        assert x.arg(0)==0x208000
        self.releases.append(x.get64(x.arg(1)));x.ret()
    def initialize(self,baseline):
        x=self.x
        assert len(baseline)==0x100000
        self.copies.clear();self.releases.clear();self.allocations.clear()
        for a in (self.PDS,self.USC):x.uc.mem_write(a,baseline)
        x.run(0x14001a898,[self.PLATFORM+0x30,self.CONTEXT+0x30],self.ranges)
        assert self.allocations==[20272]
        assert self.copies==[(self.PDS,0x404e80,176),(self.USC,0x400000,20096)]
        assert self.releases==[0x400000]
        return [bytes(x.uc.mem_read(a,len(baseline))) for a in (self.PDS,self.USC)]

if __name__=='__main__':
    x=StaticProgramsOracle();x.initialize(b'\xa5'*0x100000)
    print('PASS',x.copies,x.releases)
