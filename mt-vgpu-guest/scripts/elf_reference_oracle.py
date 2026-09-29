"""Bounded SysV x86-64 execution of the pinned Linux legacy UMD.

Only explicitly selected original function ranges and declared RAM/OS hooks
may run. No ELF entry point, initializer, dynamic loader, syscall or device I/O
is executed. TLS relocations are recorded but not implemented; any path that
needs TLS must supply an explicit OS model before it can be allowed.
"""
import hashlib
import io
import json
from pathlib import Path
import struct
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_RDI,UC_X86_REG_RSI,UC_X86_REG_RDX,
    UC_X86_REG_RCX,UC_X86_REG_R8,UC_X86_REG_R9,UC_X86_REG_RAX,
    UC_X86_REG_RSP,UC_X86_REG_RIP)
ROOT=Path(__file__).resolve().parents[1]
LIB=ROOT/'build/legacy-umd-pvr-connect-candidate/rootfs/usr/lib/x86_64-linux-gnu/libsrv_um_MUSA.so.1.0.0'
SHA='b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0'

class ElfOracle:
    BASE=0x100000  # Same image base as the permanent Ghidra corpus.
    STACK=0x3fefff8
    STOP=0x3fff000
    ARGS=(UC_X86_REG_RDI,UC_X86_REG_RSI,UC_X86_REG_RDX,UC_X86_REG_RCX,UC_X86_REG_R8,UC_X86_REG_R9)
    def __init__(self):
        data=LIB.read_bytes();self.sha256=hashlib.sha256(data).hexdigest()
        assert self.sha256==SHA,'Pinned ELF changed'
        elf=ELFFile(io.BytesIO(data));assert elf['e_machine']=='EM_X86_64'
        self.uc=Uc(UC_ARCH_X86,UC_MODE_64)
        segments=[s for s in elf.iter_segments() if s['p_type']=='PT_LOAD']
        end=max(s['p_vaddr']+s['p_memsz'] for s in segments)
        self.uc.mem_map(self.BASE,(end+4095)&~4095)
        for s in segments:self.uc.mem_write(self.BASE+s['p_vaddr'],s.data())
        self.uc.mem_map(0x2000000,0x2000000)
        self.uc.mem_map(0x50000000,0x100000)
        self.cursor=0x2200000;self.hooks={};self.called=set();self.ranges=[]
        self.symbols={};self.external={};self.tls_relocations=[]
        dyn=elf.get_section_by_name('.dynsym')
        for s in dyn.iter_symbols():
            if s.name and s['st_shndx']!='SHN_UNDEF':self.symbols[s.name]=self.BASE+s['st_value']
        def symbol_value(symbol):
            if symbol['st_shndx']!='SHN_UNDEF':return self.BASE+symbol['st_value']
            name=symbol.name
            if name not in self.external:self.external[name]=0x50000000+len(self.external)*16
            return self.external[name]
        for section in elf.iter_sections():
            if not isinstance(section,RelocationSection):continue
            table=elf.get_section(section['sh_link'])
            for reloc in section.iter_relocations():
                kind=reloc['r_info_type'];address=self.BASE+reloc['r_offset'];add=reloc.entry.get('r_addend',0)
                if kind==8:self.put64(address,self.BASE+add)
                elif kind in (1,6,7):self.put64(address,symbol_value(table.get_symbol(reloc['r_info_sym']))+add)
                elif kind in (16,17,18,36):self.tls_relocations.append(dict(address=address,type=kind))
                elif kind!=0:raise ValueError(f'Unsupported ELF relocation {kind}')
        # All external targets start outside allowed execution; an unresolved
        # call throws even when it reaches a PLT entry in an allowed function.
        for name,address in self.external.items():self.symbols.setdefault(name,address)
        self.entries=[e for e in map(json.loads,(ROOT/'decompiled/linux-legacy-umd-5.2.0/functions.jsonl').read_text().splitlines()) if not e.get('external')]
        self.entries.sort(key=lambda e:int(e['address'],16))
        self.by_name={e['name']:int(e['address'],16) for e in self.entries}
        self.uc.hook_add(UC_HOOK_CODE,self._instruction)
        plt=elf.get_section_by_name('.plt')
        if plt:self.ranges.append((self.BASE+plt['sh_addr'],self.BASE+plt['sh_addr']+plt['sh_size']))
        self.hook('PVRSRVCallocUserModeMem',lambda:self.ret(self.allocate(self.arg(0))))
        self.hook('PVRSRVFreeUserModeMem',lambda:self.ret())
        self.hook('PVRSRVMemCopy',self._memcpy)
        self.hook('memcpy',self._memcpy)
        self.hook('memset',self._memset)
        self.hook('PVRSRVDebugPrintf',self._diagnostic)
    def allow(self,*functions):
        addresses=[int(e['address'],16) for e in self.entries]
        for f in functions:
            a=f if isinstance(f,int) else self.by_name[f]
            self.ranges.append((a,next(e for e in addresses if e>a)))
    def hook(self,name,callback):self.hooks[self.symbols[name]]=callback
    def put32(self,a,v):self.uc.mem_write(a,struct.pack('<I',v))
    def put64(self,a,v):self.uc.mem_write(a,struct.pack('<Q',v))
    def get32(self,a):return struct.unpack('<I',self.uc.mem_read(a,4))[0]
    def get64(self,a):return struct.unpack('<Q',self.uc.mem_read(a,8))[0]
    def arg(self,i):return self.uc.reg_read(self.ARGS[i]) if i<6 else self.get64(self.uc.reg_read(UC_X86_REG_RSP)+8+(i-6)*8)
    def ret(self,v=0):
        sp=self.uc.reg_read(UC_X86_REG_RSP)
        self.uc.reg_write(UC_X86_REG_RAX,v&((1<<64)-1))
        self.uc.reg_write(UC_X86_REG_RIP,self.get64(sp));self.uc.reg_write(UC_X86_REG_RSP,sp+8)
    def allocate(self,n):
        assert 0<n<=0x800000
        a=self.cursor;self.cursor=(a+n+63)&~63;assert self.cursor<0x3800000
        self.uc.mem_write(a,bytes(n));return a
    def _memcpy(self):
        dst,src,n=[self.arg(i) for i in range(3)];assert n<=0x800000
        self.uc.mem_write(dst,bytes(self.uc.mem_read(src,n)));self.ret(dst)
    def _memset(self):
        dst,value,n=[self.arg(i) for i in range(3)];assert n<=0x800000
        self.uc.mem_write(dst,bytes([value&255])*n);self.ret(dst)
    def _diagnostic(self):
        raise AssertionError(f'Original UMD diagnostic at return {self.get64(self.uc.reg_read(UC_X86_REG_RSP)):#x}, line {self.arg(2):#x}')
    def _instruction(self,uc,a,n,_):
        if a in self.hooks:self.called.add(a);self.hooks[a]()
        elif not any(start<=a<end for start,end in self.ranges):
            name=next((k for k,v in self.symbols.items() if v==a),'unknown')
            raise AssertionError(f'Unpermitted instruction {a:#x} ({name})')
        else:
            code=bytes(uc.mem_read(a,min(n,2)))
            assert code not in (b'\x0f\x05',b'\x0f\x34',b'\xcd\x80'),'OS syscall forbidden'
    def run(self,function,args):
        address=function if isinstance(function,int) else self.symbols[function]
        self.uc.mem_write(self.STACK-0x18000,bytes(0x19000));self.put64(self.STACK,self.STOP)
        for i,v in enumerate(args):
            if i<6:self.uc.reg_write(self.ARGS[i],v)
            else:self.put64(self.STACK+8+(i-6)*8,v)
        self.uc.reg_write(UC_X86_REG_RAX,0);self.uc.reg_write(UC_X86_REG_RSP,self.STACK)
        self.uc.emu_start(address,self.STOP,timeout=3000000,count=1000000)
        assert self.uc.reg_read(UC_X86_REG_RIP)==self.STOP,'Instruction/time limit'
        return self.uc.reg_read(UC_X86_REG_RAX)
