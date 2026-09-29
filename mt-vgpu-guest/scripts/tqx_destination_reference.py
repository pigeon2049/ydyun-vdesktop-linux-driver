"""Original family-2 destination command emission into a RAM command buffer."""
import json
from pathlib import Path
import struct
from tqx_texture_reference import TextureOracle

class DestinationOracle(TextureOracle):
    JOB, PARAMS, RECT, ALLOCATOR = 0x218000,0x219000,0x21a000,0x21b000
    def __init__(self):
        super().__init__();x=self.x
        entries=sorted(int(e['address'],16) for e in map(json.loads,
            (Path(__file__).resolve().parents[1]/'decompiled/mtkm64.sys/functions.jsonl').read_text().splitlines())
            if not e.get('external',False))
        names='0dc638 0d9eec 11eb2c 056010 07b9e0 07b7c8 07bec8 11ea1c 0da4a8 0d98c8 0dbf64'
        self.ranges += [(a,next(e for e in entries if e>a)) for a in
                        (0x140000000+int(s,16) for s in names.split())]
        x.put64(self.CMD+0xb58,self.ALLOCATOR)
        x.put64(self.VIEW+40,self.RECT)
        x.put64(self.JOB,1);x.put64(self.JOB+8,self.VIEW);x.put64(self.JOB+24,self.PARAMS)
        self.commits=[]
        x.hooks[0x1400fd3a4]=self.reserve
        x.hooks[0x1400fc600]=self.commit

    def reserve(self):
        assert self.x.arg(0)==self.ALLOCATOR
        self.x.uc.mem_write(self.OUTPUT,bytes(80));self.x.ret(self.OUTPUT)

    def commit(self):
        args=tuple(self.x.arg(i) for i in range(3))
        assert args==(self.ALLOCATOR,self.OUTPUT+80,1)
        self.commits.append(args);self.x.ret()

    def run(self,dst,element,width,height,code=0x7050,execution=0x9000,constants=0xc000):
        x=self.x;self.commits.clear()
        x.uc.mem_write(self.CHUNK,struct.pack('<Q4I',0,element,1,width,height))
        x.run(0x1400a5740,[self.CHUNK,self.SURFACE,self.STATE],self.ranges)
        x.put64(self.VIEW+16,dst)
        x.uc.mem_write(self.RECT,struct.pack('<4I',0,0,width,height))
        x.uc.mem_write(self.PARAMS,struct.pack('<6I',code,execution,constants,5,0,4))
        x.put32(self.CMD+0x2190,0)
        x.run(0x1400dc638,[self.CMD,self.JOB],self.ranges)
        assert len(self.commits)==1
        assert x.get64(self.CMD+0x2298)==self.OUTPUT+72
        assert bytes(x.uc.mem_read(self.CMD+0x22b0,2))==b'\1\1'
        assert struct.unpack('<I',x.uc.mem_read(self.CMD+0x2190,4))[0]==80
        return bytes(x.uc.mem_read(self.OUTPUT,80))
