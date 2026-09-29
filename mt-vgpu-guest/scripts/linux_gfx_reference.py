"""Original QY1 Linux GFX descriptor construction from bounded RAM fixtures."""
from linux_submission_reference import LinuxSubmissionOracle

class LinuxGfxOracle(LinuxSubmissionOracle):
    JOB=0x2006000; SURFACE=0x2007000; RENDER=0x2008000
    VERTEX=0x2009000; CONTEXT=0x200a000; REGISTERS=0x200b000
    def __init__(self):
        super().__init__()
        self.allow('FUN_0017dab0','FUN_0017d0b0','FUN_0017d4b0','FUN_0017d6e0','FUN_0017d890')
    def state(self,job,surface,render,vertex,context):
        assert [len(x) for x in (job,surface,render,vertex,context)]==[0x7b8,0x600,0x200,0x140,0x330]
        self.setup()
        for a,data in zip((self.JOB,self.SURFACE,self.RENDER,self.VERTEX,self.CONTEXT),
                          (job,surface,render,vertex,context)):self.uc.mem_write(a,data)
        for offset,address in ((0x28,self.CONTEXT),(0x30,self.SURFACE),(0x2d8,self.VERTEX),(0x2e0,self.RENDER)):
            self.put64(self.JOB+offset,address)
    def registers(self,job,surface,render,vertex,context):
        self.state(job,surface,render,vertex,context)
        self.uc.mem_write(self.REGISTERS,bytes(0x210)+b'\xa5'*16)
        self.run(0x17d890,[self.CONNECTION,self.JOB,self.REGISTERS])
        self.run(0x17d4b0,[self.CONNECTION,self.JOB,self.REGISTERS+0xb0])
        assert bytes(self.uc.mem_read(self.REGISTERS+0x210,16))==b'\xa5'*16
        return bytes(self.uc.mem_read(self.REGISTERS,0x210))
    def packet(self,job,surface,render,vertex,context,dma_va=0x50000000):
        self.state(job,surface,render,vertex,context)
        self.begin(5,0x66,dma_va)
        assert self.run(0x17dab0,[self.CONNECTION,self.JOB,self.kick,0])==0
        assert self.get32(self.kick+0x363c)==0x210
        kick=bytes(self.uc.mem_read(self.kick,0x35c0))
        packet=self.finish(0x210,self.CONTEXT+0x218)
        assert packet[-0x35c0:]==kick
        return dict(packet=packet,kick=kick,registers=kick[0x33b0:])

if __name__=='__main__':
    x=LinuxGfxOracle();inputs=[bytes(n) for n in (0x7b8,0x600,0x200,0x140,0x330)]
    r=x.packet(*inputs)
    print('packet',hex(len(r['packet'])),'kick',hex(len(r['kick'])))
    print('fields',[(hex(i),hex(int.from_bytes(r['kick'][i:i+4],'little'))) for i in range(0,len(r['kick']),4) if any(r['kick'][i:i+4])])
