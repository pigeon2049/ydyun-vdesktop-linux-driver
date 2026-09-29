"""Restricted TQX descriptor layout, serialization and submission-view query."""
import json
import struct
from pathlib import Path
from tqx_stream_reference import StreamOracle
ROOT=Path(__file__).resolve().parents[1]
class DmaOracle(StreamOracle):
    META, CMDPTR, DMA_LAYOUT, SHAPE, SUBMIT, VIEW = 0x290000,0x291000,0x292000,0x293000,0x294000,0x295000
    DMA=0xa00000
    def __init__(self):
        super().__init__()
        entries=sorted(int(e['address'],16) for e in map(json.loads,
            (ROOT/'decompiled/mtkm64.sys/functions.jsonl').read_text().splitlines()) if not e.get('external'))
        names='04cc80 09c3e8 088f5c 04e924 04d174 04d188 052020 11d7b8 100c7c'
        self.ranges += [(a,next(e for e in entries if e>a)) for a in
            (int('140'+s,16) for s in names.split())]
        self.x.run(0x14004cc80,[self.DEVICE],self.ranges)
    def serialize(self,dma_va=0x40010000,state_va=0x40020000,cores=1,header_offset=0,record=None):
        x=self.x
        # The actual module layout builder receives record/page counts and
        # the device's core count. No allocator/OS routine is executed here.
        x.put32(self.DEVICE+0x3b4,cores)
        x.uc.mem_write(self.SHAPE,bytes(64))
        x.run(0x14009c3e8,[self.MODULE,self.SHAPE,1,1,0,0,header_offset,0],self.ranges)
        shape=struct.unpack('<16I',x.uc.mem_read(self.SHAPE,64))
        x.run(0x140052020,[self.DMA_LAYOUT,self.SHAPE],self.ranges)
        x.put64(self.DMA_LAYOUT+8,self.DMA);x.put64(self.DMA_LAYOUT+16,dma_va)
        x.uc.mem_write(self.META,bytes(0x50));x.put32(self.META,1)
        x.put64(self.CMDPTR,self.CMD);x.put64(self.META+0x10,self.CMDPTR)
        x.put64(self.META+0x18,self.DMA_LAYOUT);x.put64(self.META+0x20,state_va)
        x.uc.mem_write(self.DMA,bytes(shape[0]+16))
        if record is not None:
            assert len(record)==296;x.uc.mem_write(self.RECORDS,record)
        assert x.run(0x14004e924,[self.DEVICE,self.META],self.ranges)==0
        x.uc.mem_write(self.SUBMIT,bytes(0x90))
        x.put64(self.SUBMIT+0x80,self.META);x.put64(self.SUBMIT+0x88,0x50)
        x.run(0x14011d7b8,[self.CMD,0,self.SUBMIT,self.VIEW],self.ranges)
        view=bytes(x.uc.mem_read(self.VIEW,24))
        return dict(shape=shape,data=bytes(x.uc.mem_read(self.DMA,shape[0])),view=view,
                    after=bytes(x.uc.mem_read(self.DMA+shape[0],16)))
if __name__=='__main__':
    o=DmaOracle();r=o.run(0x100001,0x200000000,1,1,1,copy_bytes=0xffffffff)
    d=o.serialize()
    print('shape',[hex(v) for v in d['shape']]);print('view',d['view'].hex())
    print('record',[(hex(i),hex(struct.unpack_from('<I',r['record'],i)[0])) for i in range(0,296,4) if any(r['record'][i:i+4])])
    print('dma',[(hex(i),hex(struct.unpack_from('<I',d['data'],i)[0])) for i in range(0,len(d['data']),4) if any(d['data'][i:i+4])])
