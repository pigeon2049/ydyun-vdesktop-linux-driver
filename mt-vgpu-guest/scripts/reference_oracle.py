"""Restricted x86 reference execution for CPU-side driver data construction.

No guest module, device resources, Windows entry points or imported OS code are
executed. Callers explicitly permit local instruction ranges and model helpers.
"""
import struct
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_R8,
                              UC_X86_REG_R9, UC_X86_REG_RSP, UC_X86_REG_RIP,
                              UC_X86_REG_RAX)
from pe_image import ReferencePE, REFERENCE_SHA256


class ReferenceOracle:
    STACK = 0x2f0000
    STOP = 0x2ff000

    def __init__(self, path='/opt/MTT-driver-only/mtkm64.sys', expected_sha256=REFERENCE_SHA256):
        self.pe = ReferencePE(path, expected_sha256)
        self.uc = Uc(UC_ARCH_X86, UC_MODE_64)
        end = max(va + length for va, length, _ in self.pe.sections)
        self.uc.mem_map(self.pe.base, (end - self.pe.base + 4095) & ~4095)
        for va, length, raw in self.pe.sections:
            self.uc.mem_write(va, self.pe.data[raw:raw + length])
        self.uc.mem_map(0x200000, 0x100000)
        self.uc.mem_map(0x400000, 0x2000000)
        self.cursor = 0x1000000
        self.ranges = []
        self.hooks = {}
        self.called = set()
        self.uc.hook_add(UC_HOOK_CODE, self._instruction)
        if self.pe.sha256 == REFERENCE_SHA256:
            self.put32(0x141107d00, 0)  # Disable reference diagnostic logging only.
            self.hooks[0x140007d90] = self._allocate
            self.hooks[0x140007d9c] = self._allocate
            self.hooks[0x140130c80] = self._memset
            self.hooks[0x140130f80] = self._memcpy

    def get32(self, address):
        return struct.unpack('<I', self.uc.mem_read(address, 4))[0]

    def put32(self, address, value):
        self.uc.mem_write(address, struct.pack('<I', value))

    def put64(self, address, value):
        self.uc.mem_write(address, struct.pack('<Q', value))

    def get64(self, address):
        return struct.unpack('<Q', self.uc.mem_read(address, 8))[0]

    def arg(self, index):
        if index < 4:
            return self.uc.reg_read((UC_X86_REG_RCX, UC_X86_REG_RDX,
                                     UC_X86_REG_R8, UC_X86_REG_R9)[index])
        return self.get64(self.uc.reg_read(UC_X86_REG_RSP) + 0x28 + (index - 4) * 8)

    def ret(self, value=0):
        rsp = self.uc.reg_read(UC_X86_REG_RSP)
        self.uc.reg_write(UC_X86_REG_RAX, value)
        self.uc.reg_write(UC_X86_REG_RIP, self.get64(rsp))
        self.uc.reg_write(UC_X86_REG_RSP, rsp + 8)

    def allocate(self, size):
        assert 0 < size <= 0x800000
        address = self.cursor
        self.cursor = (address + size + 63) & ~63
        assert self.cursor <= 0x2400000
        self.uc.mem_write(address, bytes(size))
        return address

    def _allocate(self):
        self.ret(self.allocate(self.arg(0)))

    def _memset(self):
        dest, value, size = (self.arg(i) for i in range(3))
        assert size <= 0x800000
        self.uc.mem_write(dest, bytes([value & 255]) * size)
        self.ret(dest)

    def _memcpy(self):
        dest, source, size = (self.arg(i) for i in range(3))
        assert size <= 0x800000
        self.uc.mem_write(dest, bytes(self.uc.mem_read(source, size)))
        self.ret(dest)

    def import_noop(self, iat, stub):
        # For single-threaded lock primitives only; never use for I/O helpers.
        self.put64(iat, stub)
        self.hooks[stub] = self.ret

    def _instruction(self, uc, address, size, _):
        if address in self.hooks:
            self.called.add(address)
            self.hooks[address]()
        elif not any(start <= address < end for start, end in self.ranges):
            raise AssertionError(f'Unexpected instruction at {address:#x}')
        else:
            opcode = bytes(uc.mem_read(address, min(size, 2)))
            assert opcode not in (b'\x0f\x05', b'\x0f\x34', b'\xcd\x80'), 'OS syscall forbidden'

    def run(self, address, args, ranges, count=500000):
        self.ranges = ranges
        self.uc.mem_write(self.STACK - 0x8000, bytes(0x9000))
        self.put64(self.STACK, self.STOP)
        for index, value in enumerate(args):
            if index < 4:
                self.uc.reg_write((UC_X86_REG_RCX, UC_X86_REG_RDX,
                                   UC_X86_REG_R8, UC_X86_REG_R9)[index], value)
            else:
                self.put64(self.STACK + 0x28 + (index - 4) * 8, value)
        self.uc.reg_write(UC_X86_REG_RSP, self.STACK)
        self.uc.emu_start(address, self.STOP, timeout=3000000, count=count)
        assert self.uc.reg_read(UC_X86_REG_RIP) == self.STOP, 'Instruction/time limit exceeded'
        return self.uc.reg_read(UC_X86_REG_RAX)
