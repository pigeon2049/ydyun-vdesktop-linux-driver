"""Execute native family-2 clear emission and finalization in bounded RAM.

Color words are the already packed destination pixel representation consumed by
1400dc430 (color object type zero). No CPU color conversion is being emulated.
"""
import json
from tqx_stream_reference import ROOT, StreamOracle


class FillOracle(StreamOracle):
    def __init__(self):
        super().__init__()
        entries=sorted(int(e['address'],16) for e in map(json.loads,
            (ROOT/'decompiled/mtkm64.sys/functions.jsonl').read_text().splitlines())
            if not e.get('external',False))
        for address in (0x14010ae38,0x1400a8a4c,0x140056048,
                        0x1400dc430,0x1400d9838):
            self.ranges.append((address,next(e for e in entries if e>address)))
        # The constructor reserves 20 DWORDs; allocator cursor lookup returns
        # that window's end even though a clear commits only 19 DWORDs.

    def fill(self,dst,element,width,height,words,rect=None,**kwargs):
        return super().run(0,dst,element,width,height,fill_words=words,
                           fill_rect=rect,**kwargs)


if __name__=='__main__':
    import struct
    oracle=FillOracle()
    result=oracle.fill(0x40100000,4,64,64,[0x12345678,0,0,0],(3,5,17,19))
    print('command',result['command'].hex())
    print('record',[(hex(i),hex(struct.unpack_from('<I',result['record'],i)[0]))
        for i in range(0,0x128,4) if any(result['record'][i:i+4])])
    print('allocations',[(n,k,hex(r),b.hex()) for n,k,r,b in result['allocations']])
