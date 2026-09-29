"""Original Linux QY1 render-context allocation and save/restore generation.

All allocation is bounded emulated RAM. GPU addresses are explicit fixtures;
no bridge, kernel module, syscall, shader execution or hardware I/O is used.
"""
import json
from pathlib import Path
import struct
from linux_submission_reference import LinuxSubmissionOracle

ROOT=Path(__file__).resolve().parents[1]

class LinuxGfxContextOracle(LinuxSubmissionOracle):
    CONTEXT=0x2006000; DEVMEM=0x2007000; PDS_HEAP=0x2008000; USC_HEAP=0x2009000
    MUTEX=0x200a000; GENERAL=0x200b000; COMPONENT=0x200c000
    def __init__(self):
        super().__init__()
        self.allow(*['FUN_'+a for a in ('00182b00','00183050','001836b0','001832c0',
            '00182f40','00183c40','00183d30','00184280','00184390','00184030',
            '00184060','00184750','00184a20','00184bb0','001843e0','00184590')])
        # PSC compiler closure only, excluding diagnostic/OS subgraphs. All
        # external calls are still rejected unless explicitly modeled below.
        graph={e['from']:e['to'] for e in map(json.loads,
            (ROOT/'decompiled/linux-legacy-umd-5.2.0/calls.jsonl').read_text().splitlines())}
        todo=['001a4fc0','001ad6c0','001adc20','001b6a40','001b6e10',
              '001acf40','001ac9d0','001b6e40'];seen=set()
        while todo:
            address=todo.pop()
            if address in seen or not ('001a4000'<=address<'001b7000'):continue
            seen.add(address);todo.extend(graph.get(address,[]))
        self.compiler_addresses=sorted(seen)
        self.allow(*[int(a,16) for a in seen])
        self.allow(0x1a5250) # Empty compiler-context cleanup used by 184390.
        for name in ('PVRSRVAllocUserModeMem','malloc'):
            self.hook(name,lambda:self.ret(self.allocate(self.arg(0))))
        self.hook('free',lambda:self.ret())
        self.hook('_setjmp',lambda:self.ret(0)) # No exception/longjmp path allowed.
        self.hook('qsort',self._sort)
        for name in ('pthread_mutex_lock','pthread_mutex_unlock'):
            self.hook(name,self._mutex)
        self.allow(0x18df10,0x18df20) # In-module tail thunks, not GOT slots.
        self.allow(0x12c8a0,0x12c8a8) # malloc/free .plt.got thunks.
        self.hook('MTSRVFindHeapByName',self._find_heap)
        self.hooks[0x1b9700]=self._heap_alloc
        self.hooks[0x196f30]=self._device_alloc
    def _mutex(self):
        assert self.arg(0)==self.MUTEX;self.ret()
    def _sort(self):
        # Exact 001a6390 comparator: occupied entries first, longest live
        # range first. Equal entries retain their original order in this model.
        address,n,stride,comparator=[self.arg(i) for i in range(4)]
        assert comparator==0x1a6390 and stride==32 and n<=128
        records=[bytes(self.uc.mem_read(address+i*stride,stride)) for i in range(n)]
        def key(b):
            used,start,end=struct.unpack_from('<3I',b)
            return (0,-((end+1-start)&0xffffffff)) if used else (1,0)
        self.uc.mem_write(address,b''.join(sorted(records,key=key)));self.ret()
    def cstring(self,address):
        data=bytearray()
        for i in range(128):
            c=self.uc.mem_read(address+i,1)[0]
            if c==0:return data.decode()
            data.append(c)
        raise AssertionError('Unbounded string')
    def _find_heap(self):
        assert self.arg(0)==self.DEVMEM
        name=self.cstring(self.arg(1));assert name in ('General','Component Control')
        self.put64(self.arg(2),self.GENERAL if name=='General' else self.COMPONENT)
        self.ret()
    def _bo(self,heap,n,alignment,flags,name):
        assert heap in self.gpu_next and 0<n<0x100000 and alignment>0 and not alignment&(alignment-1)
        va=(self.gpu_next[heap]+alignment-1)&-alignment
        self.gpu_next[heap]=(va+n+4095)&-4096
        cpu=self.allocate(n+32);self.uc.mem_write(cpu+n,b'\xa5'*32)
        desc=self.allocate(0x30)
        self.put64(desc+0x10,va);self.put64(desc+0x18,cpu);self.put64(desc+0x20,n)
        self.bos.append(dict(heap=heap,bytes=n,alignment=alignment,flags=flags,name=name,
                             gpu_va=va,cpu=cpu,desc=desc))
        return desc,va
    def _heap_alloc(self):
        heap,n=self.arg(0),self.arg(1)
        assert heap in (self.PDS_HEAP,self.USC_HEAP)
        desc,_=self._bo(heap,n,128,0,'PDS suballocation' if heap==self.PDS_HEAP else 'USC suballocation')
        self.ret(desc)
    def _device_alloc(self):
        mode,heap,n,align,flags,label=[self.arg(i) for i in range(6)]
        assert mode==1 and heap in (self.GENERAL,self.COMPONENT)
        desc,va=self._bo(heap,n,align,flags,self.cstring(label))
        self.put64(self.arg(6),desc);self.put64(self.arg(7),va);self.ret()
    def create(self,pds_offset=0x100000,usc_offset=0x100000,general_va=0x43000000,
               component_va=0xf000100000):
        self.setup()
        self.uc.mem_write(self.CONTEXT,bytes(0x7000));self.bos=[]
        self.gpu_next={self.PDS_HEAP:0x8100000000+pds_offset,self.USC_HEAP:0x8400000000+usc_offset,
                       self.GENERAL:general_va,self.COMPONENT:component_va}
        self.put64(self.CONNECTION+0x80,self.MUTEX);self.put64(self.CONNECTION+0x88,self.MUTEX)
        self.put64(self.CONNECTION+0x90,self.PDS_HEAP);self.put64(self.CONNECTION+0x98,self.USC_HEAP)
        self.put64(self.PDS_HEAP+0x48,0x8100000000);self.put64(self.USC_HEAP+0x48,0x8400000000)
        c=self.CONTEXT
        assert self.run(0x1836b0,[self.CONNECTION,self.DEVMEM,c+0x70,c+0x80,c+0xa0,c+0xf0,c+0x140])==0
        self.put64(c+0x218,self.get64(c+0x78)&0xffffffffffe0)
        self.put64(c+0x220,self.get64(c+0x88)&0xfffffffff0)
        self.task_records=[]
        for src,dst in ((c+0x140,c+0x258),(c+0x160,c+0x2c8)):
            for store in (1,0):
                before=self.get32(self.get64(src+8)+0x10)
                assert self.run(0x183c40,[src,store,dst if store else dst+0x38])==0
                self.task_records.append(dict(kind=3,store=store,pds_start=before,
                    pds_end=self.get32(self.get64(src+8)+0x10)))
        for kind,src,dst in ((0,c+0xa0,c+0x228),(0,c+0xc8,c+0x298),
                             (1,c+0xf0,c+0x240),(1,c+0x118,c+0x2b0)):
            for store in (1,0):
                pds=self.get64(src+8);usc=self.get64(src+0x10)
                before=self.get32(pds+0x10);ubefore=self.get32(usc+0x10)
                assert self.run(0x183d30,[src,kind,store,dst if store else dst+0x38])==0
                self.task_records.append(dict(kind=kind,store=store,pds_start=before,
                    pds_end=self.get32(pds+0x10),usc_start=ubefore,usc_end=self.get32(usc+0x10)))
        assert self.run(0x182f40,[self.CONNECTION,self.DEVMEM,c+0x90])==0
        self.put64(c+0x308,self.get64(c+0x98))
        for b in self.bos:
            assert bytes(self.uc.mem_read(b['cpu']+b['bytes'],32))==b'\xa5'*32,b
            b['data']=bytes(self.uc.mem_read(b['cpu'],b['bytes']))
        return dict(context=bytes(self.uc.mem_read(c,0x330)),csw=bytes(self.uc.mem_read(c+0x218,0xf8)),
                    bos=self.bos,tasks=self.task_records)

if __name__=='__main__':
    x=LinuxGfxContextOracle();r=x.create()
    print(json.dumps({'bos':[{k:v for k,v in b.items() if k!='data'} for b in r['bos']],
                      'tasks':r['tasks'],'csw':r['csw'].hex()},indent=2))
