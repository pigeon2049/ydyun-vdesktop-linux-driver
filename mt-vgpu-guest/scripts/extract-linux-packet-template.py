#!/usr/bin/env python3
"""Extract a minimal legal Linux 3D packet template from LinuxGfxOracle."""
from pathlib import Path
from linux_gfx_reference import LinuxGfxOracle

ROOT = Path(__file__).resolve().parents[1]
oracle = LinuxGfxOracle()
inputs = [bytes(n) for n in (0x7b8, 0x600, 0x200, 0x140, 0x330)]
r = oracle.packet(*inputs)
packet = r['packet']

assert len(packet) == 0x46f0

lines = [
    "/* SPDX-License-Identifier: GPL-2.0 */",
    "/* Generated minimal legal Linux 3D packet template (18,160 bytes).",
    " * Sourced from verified libsrv_um_MUSA.so reference execution.",
    " */",
    "#ifndef MT_GFX_PACKET_TEMPLATE_H",
    "#define MT_GFX_PACKET_TEMPLATE_H",
    "",
    "#include <linux/types.h>",
    "",
    "#define MT_GFX_LINUX_PACKET_BYTES 0x46f0U",
    "",
    "static const u8 mt_gfx_linux_packet_template[0x46f0] __maybe_unused = {",
]

for i in range(0, len(packet), 16):
    chunk = packet[i:i+16]
    hex_str = ", ".join(f"0x{b:02x}" for b in chunk)
    comma = "," if i + 16 < len(packet) else ""
    lines.append(f"\t{hex_str}{comma}")

lines.append("};")
lines.append("")
lines.append("#endif /* MT_GFX_PACKET_TEMPLATE_H */")
lines.append("")

out_path = ROOT / "kernel/mt_gfx_packet_template.h"
out_path.write_text("\n".join(lines))
print(f"Generated {out_path} ({len(packet)} bytes)")
