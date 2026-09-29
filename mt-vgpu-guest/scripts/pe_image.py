"""Bounds-checked file-backed VA reads for the reference PE32+ image."""
from pathlib import Path
import hashlib
import struct

REFERENCE_SHA256 = "0512ad5a75dcf16680d608e0a20b5154564798451d6b42224be9ae8e67d6ef33"


class ReferencePE:
    def __init__(self, path, expected_sha256=REFERENCE_SHA256):
        self.data = Path(path).read_bytes()
        self.sha256 = hashlib.sha256(self.data).hexdigest()
        if self.sha256 != expected_sha256:
            raise ValueError("Reference binary hash mismatch: fixed addresses must not be reused")
        pe = struct.unpack_from("<I", self.data, 0x3c)[0]
        self.base = struct.unpack_from("<Q", self.data, pe + 48)[0]
        size = struct.unpack_from("<H", self.data, pe + 20)[0]
        count = struct.unpack_from("<H", self.data, pe + 6)[0]
        self.sections = []
        for i in range(count):
            offset = pe + 24 + size + i * 40
            _, rva, length, raw = struct.unpack_from("<4I", self.data, offset + 8)
            self.sections.append((self.base + rva, length, raw))

    def read(self, address, size):
        if size < 0:
            raise ValueError("Negative read length")
        for va, length, raw in self.sections:
            if va <= address and address + size <= va + length:
                offset = raw + address - va
                if offset + size > len(self.data):
                    break
                return self.data[offset:offset + size]
        raise ValueError(f"VA range is not file-backed: {address:#x}+{size:#x}")
