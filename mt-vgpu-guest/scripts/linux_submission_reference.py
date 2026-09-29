"""Original Linux DDK2 submission envelope and TDM register construction in RAM."""
import struct
from elf_reference_oracle import ElfOracle

class LinuxSubmissionOracle(ElfOracle):
    CONNECTION=0x2000000
    FEATURE=0x2001000
    OUTPUT=0x2002000
    TQ=0x2003000
    CCB=0x2004000
    ALLOCATOR=0x2005000
    def __init__(self):
        super().__init__()
        self.allow('GetFeatures','InitMTFeatures','SubmissionHeadCreate','SubmissionRegionDescCreate',
            'SubmissionRegionCreate','SubmissionKickCreate','SubmissionTotalSizeAdd',
            'SubmissionAddCswBuf','SubmissionCmdGenerate','FUN_0015fba0',
            'FUN_00162f50','FUN_00162f60','FUN_00162f70')
        def cores():
            assert self.arg(0)==self.CONNECTION and self.arg(1)==0 and self.arg(3)==0
            self.put32(self.arg(2),self.cores);self.ret()
        self.hook('PVRSRVGetMultiCoreInfo',cores)
        self.hooks[0x1b9700]=self.allocate_gpu_buffer
    def allocate_gpu_buffer(self):
        assert self.arg(0)==0x1234
        n=self.arg(1);assert 0<n<0x10000
        desc=self.allocate(0x30);self.packet_cpu=self.allocate(n+32)
        self.uc.mem_write(self.packet_cpu+n,b'\xa5'*32)
        self.put64(desc+0x10,self.dma_va);self.put64(desc+0x18,self.packet_cpu);self.put64(desc+0x20,n)
        self.packet_bytes=n;self.ret(desc)
    def setup(self,cores=1,generation=10):
        self.cores=cores;self.cursor=0x2200000
        self.uc.mem_write(self.CONNECTION,bytes(0x6000))
        self.put64(self.CONNECTION+0xa0,self.FEATURE)
        assert self.run('InitMTFeatures',[self.CONNECTION,generation,self.FEATURE+0x620])==1
        # Only DDK2 envelope functions are called below, no bridge dispatcher.
        self.put32(self.FEATURE+0x674,2)
    def select_protocol(self,drm_major,bvnc=0x1b000500fe0002,generation=10,cores=1):
        """Execute the actual selector; BVNC/core/DRM values are RAM fixtures."""
        self.cores=cores;self.cursor=0x2200000
        self.uc.mem_write(self.CONNECTION,bytes(0x6000))
        self.allow('FUN_00151b20')
        self.put32(self.OUTPUT,drm_major)
        def version():
            assert self.arg(0)==7
            self.ret(self.OUTPUT)
        def free_version():
            assert self.arg(0)==self.OUTPUT
            self.ret()
        self.hooks[0x503750]=version;self.hooks[0x503710]=free_version
        assert self.run(0x151b20,[self.CONNECTION,7,bvnc,generation])==0
        return self.get32(self.get64(self.CONNECTION+0xa0)+0x674)
    def begin(self,region_type,command_type,dma_va):
        self.dma_va=dma_va
        assert self.run('SubmissionHeadCreate',[self.OUTPUT])==0
        self.head=self.get64(self.OUTPUT);self.put32(self.head+0x1c,command_type)
        assert self.run('SubmissionRegionDescCreate',[self.head,region_type,self.OUTPUT+8])==0
        desc=self.get64(self.OUTPUT+8)
        assert self.run('SubmissionRegionCreate',[desc])==0
        self.region=self.get64(desc+0x20)
        assert self.run('SubmissionKickCreate',[self.region,self.OUTPUT+16,self.CONNECTION])==0
        self.kick=self.get64(self.OUTPUT+16)
    def finish(self,registers_bytes,csw=0):
        self.put32(self.kick+0x363c,registers_bytes)
        self.put32(self.kick+8,self.get32(self.kick+0x3638)+registers_bytes)
        self.run('SubmissionTotalSizeAdd',[self.head,registers_bytes])
        self.run('SubmissionAddCswBuf',[self.head,csw])
        a=self.ALLOCATOR
        self.put64(a,0x1234)
        self.put64(a+0x10,a+0x10);self.put64(a+0x18,a+0x10)
        self.put64(a+0x28,a+0x28);self.put64(a+0x30,a+0x28)
        self.put64(a+0x40,self.CONNECTION)
        assert self.run('SubmissionCmdGenerate',[a,self.head,self.OUTPUT+24,self.OUTPUT+32])==0
        assert self.get64(self.OUTPUT+24)==self.dma_va and self.get32(self.OUTPUT+32)==self.packet_bytes
        assert bytes(self.uc.mem_read(self.packet_cpu+self.packet_bytes,32))==b'\xa5'*32
        return bytes(self.uc.mem_read(self.packet_cpu,self.packet_bytes))
    def tdm(self,command_va,size_array_va,state_va,padded_bytes,code,initial,flags=0x08040001,
            dma_va=0x50000000,cores=1):
        self.setup(cores);self.begin(3,0x67,dma_va)
        self.put64(self.TQ,self.CONNECTION);self.put64(self.TQ+0x30,self.CCB)
        self.put64(self.TQ+0x50,state_va)
        self.put64(self.TQ+0x60,flags);self.put64(self.TQ+0x68,initial);self.put64(self.TQ+0x70,code)
        self.put64(self.CCB+0x20,size_array_va);self.put64(self.CCB+0x38,command_va)
        self.put32(self.CCB+0x40,padded_bytes)
        self.run(0x15fba0,[self.TQ,0,self.kick])
        assert self.get32(self.kick+0x363c)==0x28
        registers=bytes(self.uc.mem_read(self.kick+0x33b0,0x28))
        packet=self.finish(0x28)
        return dict(packet=packet,registers=registers,head_bytes=0x58,
            region_type=3,kick_bytes=self.get32(self.kick+0x3638),
            total_bytes=self.packet_bytes,csw_bytes=self.get32(self.head+0x1060))
    def shape(self,region_type,registers):
        # Register bytes supplied as opaque markers only to audit serialization,
        # not interpreted as hardware commands and never submitted to a device.
        self.setup();self.begin(region_type,{2:0x66,3:0x67,4:0x68,5:0x66}[region_type],0x50000000)
        self.uc.mem_write(self.kick+0x33b0,registers)
        return self.finish(len(registers))

if __name__=='__main__':
    x=LinuxSubmissionOracle();r=x.tdm(0x40000000,0x40010200,0x40020000,96,0x7020,0x9000)
    print('bytes',hex(len(r['packet'])),'regs',r['registers'].hex())
    print('nonzero',[(hex(i),hex(struct.unpack_from('<I',r['packet'],i)[0])) for i in range(0,len(r['packet']),4) if any(r['packet'][i:i+4])])
