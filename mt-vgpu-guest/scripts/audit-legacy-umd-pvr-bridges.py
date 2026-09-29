#!/usr/bin/env python3
"""Statically inventory PVR bridge calls in the isolated legacy MTT Linux UMD."""

import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
UMD = Path(
    "/tmp/mtt-linux-umd-5.2.0/root/usr/lib/x86_64-linux-gnu/"
    "libsrv_um_MUSA.so.1.0.0"
)
EXPECTED_UMD_SHA256 = "b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0"
GENERATED = ROOT / "src/official-vgpu-2.3.0/usr/src/mtgpu-1.0.0/inc/pvr/generated"
KMD_2_7_INCLUDE = ROOT / "src/mtgpu-2.7.1-6.12/inc"
KMD_2_7_GENERATED = KMD_2_7_INCLUDE / "pvr/generated"
KMD_5_2_PACKAGE = ROOT / "downloads/mthreads-dkms_5.2.0_amd64.deb"
KMD_5_2_SHA256 = "e3f684b1f7582fa0b399a234b945ad4539c565a46399f8294db91368fa32c62b"

# PVR bridge IDs are stable numeric IDs from pvr/services/{pvr,rgx}_bridge.h.
GROUPS = {
    1: "SRVCORE",
    2: "SYNC",
    6: "MM",
    8: "CMM",
    11: "DMABUF",
    13: "CACHE",
    15: "PVRTL",
    19: "DEVICEMEMHISTORY",
    20: "HTBUFFER",
    23: "SYNCTRACKING",
    25: "DI",
    26: "DMA",
    129: "RGXCMP",
    130: "RGXTA3D",
    132: "RGXFWDBG",
    134: "RGXHWPERF",
    136: "RGXKICKSYNC",
    137: "RGXTQ2",
    138: "RGXTIMERQUERY",
    140: "MUSACE",
}


def require(condition, message):
    if not condition:
        raise SystemExit("bridge audit failed: " + message)


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def parse_callsite(line, before):
    """Recover Linux x86-64 bridge arguments set before a call to 0x92930."""
    values = {}
    output_size = None
    for instruction in reversed(before):
        match = re.search(
            r"\b(mov|xor)\s+(esi|edx|r8d),\s*([^,\s]+)(?:,\s*([^,\s]+))?",
            instruction,
        )
        if match:
            opcode, register, source, extra = match.groups()
            key = {"esi": "bridge_id", "edx": "function_id", "r8d": "input_size"}[register]
            if key not in values:
                if opcode == "xor" and source == register and extra is None:
                    values[key] = 0
                elif opcode == "mov" and source.startswith("0x"):
                    values[key] = int(source, 16)

        if output_size is None:
            match = re.search(r"\bpush\s+(0x[0-9a-f]+|[0-9]+)\s*$", instruction)
            if match:
                output_size = int(match.group(1), 0)

        if len(values) == 3 and output_size is not None:
            break
        if re.search(r"\bcall\s", instruction):
            break

    require(len(values) == 3 and output_size is not None,
            f"could not recover all bridge arguments at {line.strip()}")
    address = int(re.match(r"\s*([0-9a-f]+):", line).group(1), 16)
    return {
        "call_offset": f"0x{address:x}",
        **values,
        "output_size": output_size,
    }


KMD_5_2_GROUPS = {
    1: "SRVCORE", 2: "SYNC", 6: "MM", 8: "CMM", 11: "DMABUF",
    13: "CACHE", 15: "MTTL", 19: "DEVICEMEMHISTORY", 20: "HTBUFFER",
    23: "SYNCTRACKING", 25: "DI", 26: "DMA", 129: "MUSACMP",
    130: "MUSAGFX", 132: "MUSAFWDBG", 134: "MUSAHWPERF",
    136: "MUSAKICKSYNC", 137: "MUSAXFER", 138: "MUSATIMERQUERY",
    140: "MUSACE",
}
KMD_5_2_STRUCT_ALIASES = {
    (15, 8): ("TLOPENSTREAM", "TLOPENSTREAM2"),
    (15, 9): ("TLACQUIREDATA2", "TLACQUIREDATA"),
    (26, 3): ("DMAP2PTRANSFER", "DMATRANSFER"),
}


def read_kmd_functions(generated=GENERATED, group_names=GROUPS,
                       macro_prefix="PVRSRV_BRIDGE_"):
    result = {}
    for header in sorted(generated.glob("common_*_bridge.h")):
        content = header.read_text(errors="replace")
        for group_id, group_name in group_names.items():
            prefix = macro_prefix + group_name + "_"
            for line in content.splitlines():
                match = re.match(r"\s*#define\s+" + re.escape(prefix) + r"(\w+)\s+(.*)", line)
                if not match:
                    continue
                function_name, expression = match.groups()
                if function_name in {"CMD_FIRST", "CMD_LAST"}:
                    continue
                offset = re.search(r"CMD_FIRST\s*\+\s*(\d+)", expression)
                if offset:
                    key = (group_id, int(offset.group(1)))
                    try:
                        header_name = str(header.relative_to(ROOT))
                    except ValueError:
                        header_name = str(header)
                    result[key] = {
                        "name": function_name, "group": group_name, "header": header_name
                    }
    return result


STRUCT_ALIASES = {
    # Bridge ID/function ID: (input suffix, output suffix). Empty suffix means
    # that this KMD schema has no buffer of that direction for the command.
    (15, 8): ("TLOPENSTREAM", "TLOPENSTREAM2"),
    (15, 9): ("TLACQUIREDATA2", "TLACQUIREDATA"),
    (26, 3): ("DMAP2PTRANSFER", "DMATRANSFER"),
    (129, 10): ("", "RGXLLCPERSISTINGRESET"),
    (129, 11): ("RGXCREATECMPFENCE", "RGXCREATECMPFENCE"),
    (130, 15): ("RGXCREATETA3DFENCEPROPERTY", "RGXCREATETA3DFENCEPROPERTY"),
    (134, 4): ("RGXACQUIREHWPERFSETTING", "RGXACQUIREHWPERFSETTING"),
    (134, 5): ("RGXRELEASEHWPERFSETTING", "RGXRELEASEHWPERFSETTING"),
    (140, 4): ("MUSACEDMSUBMITTRANSFER", "MUSACEDMSUBMITTRANSFER"),
}


def compile_kmd_sizes(calls, kmd_functions, include_root, generated, label,
                      struct_prefix="PVRSRV_BRIDGE", include_dirs=None,
                      prelude=None, aliases=STRUCT_ALIASES):
    """Ask the kernel C compiler for packed bridge struct sizes, without loading code."""
    config = include_root / "config_kernel.h"
    require(config.is_file(), f"missing {label} KMD config header {config}")
    headers = sorted(generated.glob("common_*_bridge.h"))
    require(headers, f"missing generated bridge headers under {generated}")
    header_text = "\n".join(path.read_text(errors="replace") for path in headers)
    struct_names = set(re.findall(
        r"}\s*__packed\s+" + re.escape(struct_prefix) + r"_(IN|OUT)_(\w+)\s*;",
        header_text
    ))

    items = []
    call_structs = {}
    for call in calls:
        key = (call["bridge_id"], call["function_id"])
        if key not in kmd_functions:
            continue
        if key in aliases:
            input_suffix, output_suffix = aliases[key]
        else:
            name = kmd_functions[key]["name"]
            suffix = "MUSACE" + name if key[0] == 140 else name
            if struct_prefix == "MTGPU_BRIDGE":
                alternatives = [suffix]
                if name.startswith("MUSA"):
                    alternatives.append(kmd_functions[key]["group"] + name[4:])
                suffix = next((candidate for candidate in alternatives
                               if ("IN", candidate) in struct_names or
                               ("OUT", candidate) in struct_names), suffix)
            input_suffix = output_suffix = suffix

        names = []
        for direction, suffix in (("IN", input_suffix), ("OUT", output_suffix)):
            if suffix and (direction, suffix) in struct_names:
                names.append(f"{struct_prefix}_{direction}_{suffix}")
            else:
                names.append(None)
        call_structs[key] = names
        for name in names:
            if name:
                items.append(name)

    # Stable deduplication keeps the probe small while preserving each command's
    # association with its input/output structure.
    unique_items = list(dict.fromkeys(items))
    indices = {name: index for index, name in enumerate(unique_items)}
    include_dirs = include_dirs or (
        "", "pvr", "pvr/generated", "pvr/hwdefs", "pvr/hwdefs/km",
        "pvr/include", "pvr/include/powervr", "pvr/services", "common",
        "mtgpu", "mtgpu/vgpu", "imgtec",
    )
    kernel_release = subprocess.run(
        ["uname", "-r"], check=True, text=True, stdout=subprocess.PIPE
    ).stdout.strip()
    kernel_build = Path("/lib/modules") / kernel_release / "build"
    require(kernel_build.exists(), f"missing kernel build headers {kernel_build}")

    with tempfile.TemporaryDirectory(prefix=f"mt-vgpu-{label}-bridge-size-audit-") as temp:
        temp_path = Path(temp)
        makefile = ["obj-m += bridge_size_probe.o",
                    f"ccflags-y += -D__linux__ -include {config}"]
        makefile.extend(f"ccflags-y += -I{include_root / directory}" for directory in include_dirs)
        (temp_path / "Makefile").write_text("\n".join(makefile) + "\n")
        includes = "\n".join(f'#include "{header.name}"' for header in headers)
        expressions = ", ".join(f"sizeof({name})" for name in unique_items) or "0"
        if prelude is None:
            prelude = '#include "rgxdefs_km.h"\n' if label == "kmd-2-7" else ""
        source = (
            "#include <linux/module.h>\n"
            "#include <linux/types.h>\n"
            "#include <linux/compiler_attributes.h>\n"
            f"{prelude}"
            f"{includes}\n"
            f"const unsigned int bridge_probe_sizes[] __used = {{{expressions}}};\n"
            'MODULE_LICENSE("GPL");\n'
        )
        (temp_path / "bridge_size_probe.c").write_text(source)
        build = subprocess.run(
            ["make", "-s", "-C", str(kernel_build), f"M={temp_path}", "modules", "-j2"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        require(build.returncode == 0,
                f"temporary {label} C size probe failed: " + (build.stderr or build.stdout)[-3000:])
        dump = temp_path / "sizes.bin"
        objcopy = subprocess.run(
            ["objcopy", "--dump-section", f".rodata={dump}", str(temp_path / "bridge_size_probe.o")],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        require(objcopy.returncode == 0, "objcopy could not read the temporary size probe")
        data = dump.read_bytes()
        require(len(data) == 4 * len(unique_items),
                f"unexpected .rodata size from C probe: {len(data)} bytes")
        sizes = struct.unpack("<" + "I" * len(unique_items), data)

    result = {}
    for key, names in call_structs.items():
        result[key] = [sizes[indices[name]] if name else 0 for name in names]
    return result


def summarize_reference(calls, functions, sizes):
    pairs = {(call["bridge_id"], call["function_id"]) for call in calls}
    matched = pairs & set(functions)
    absent = sorted(pairs - set(functions))
    mismatches = []
    for call in calls:
        key = (call["bridge_id"], call["function_id"])
        if key not in functions:
            continue
        input_size, output_size = sizes[key]
        if call["input_size"] != input_size or call["output_size"] != output_size:
            mismatches.append({
                "bridge_id": key[0], "function_id": key[1],
                "name": functions[key]["name"],
                "umd": [call["input_size"], call["output_size"]],
                "kmd": [input_size, output_size],
            })
    return {
        "bridge_functions_present": len(matched),
        "bridge_functions_absent": len(absent),
        "bridge_functions_with_exact_sizes": len(matched) - len(mismatches),
        "bridge_functions_with_size_mismatches": len(mismatches),
        "size_mismatches": mismatches,
        "missing": [{"bridge_id": group, "function_id": function,
                     "bridge": GROUPS.get(group, "UNKNOWN")}
                    for group, function in absent],
    }


def compile_kmd_5_2_reference(calls):
    """Compare the legacy UMD against all bridge headers in the packaged 5.2 KMD."""
    require(KMD_5_2_PACKAGE.is_file(), f"missing reference DKMS package {KMD_5_2_PACKAGE}")
    package_hash = sha256(KMD_5_2_PACKAGE)
    require(package_hash == KMD_5_2_SHA256,
            "5.2 DKMS package hash changed; review it before trusting the reference schema")
    kernel_release = subprocess.run(
        ["uname", "-r"], check=True, text=True, stdout=subprocess.PIPE
    ).stdout.strip()
    kernel_build = Path("/lib/modules") / kernel_release / "build"
    require(kernel_build.exists(), f"missing kernel build headers {kernel_build}")

    with tempfile.TemporaryDirectory(prefix="mt-vgpu-5-2-bridge-ref-") as temp:
        temp_path = Path(temp)
        package_root = temp_path / "package-root"
        package_root.mkdir()
        extract = subprocess.run(
            ["dpkg-deb", "-x", str(KMD_5_2_PACKAGE), str(package_root)],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        require(extract.returncode == 0,
                "could not unpack the reference 5.2 DKMS package: " + extract.stderr[-1000:])
        source = package_root / "usr/src/mtgpu-5.2.0-server"
        include_root = source / "inc"
        config = include_root / "config_kernel.h"
        generated = include_root / "mt/generated/common_sync_bridge.h"
        require(config.is_file() and generated.is_file(),
                "5.2 DKMS package is missing its config or Sync bridge header")

        probe = temp_path / "probe"
        probe.mkdir()
        include_dirs = (
            "", "common", "imgtec", "mt", "mt/generated", "mt/hwdefs",
            "mt/hwdefs/km", "mt/hwdefs/km/configs", "mt/include", "mt/include/mt",
            "mt/services", "mtgpu", "mtgpu/vgpu", "mtgpu-next", "mtvpu",
            "mtvpu/linux", "shared_include",
        )
        makefile = ["obj-m += bridge_size_probe.o",
                    f"ccflags-y += -D__linux__ -include {config}"]
        makefile.extend(f"ccflags-y += -I{include_root / directory}" for directory in include_dirs)
        (probe / "Makefile").write_text("\n".join(makefile) + "\n")
        source_text = (
            "#include <linux/module.h>\n"
            "#include <linux/types.h>\n"
            "#include <linux/compiler_attributes.h>\n"
            '#include "musaconfig_km_1.V.0.0.h"\n'
            '#include "musadefs_km.h"\n'
            '#include "common_sync_bridge.h"\n'
            "const unsigned int bridge_probe_sizes[] __used = {"
            "sizeof(MTGPU_BRIDGE_IN_ALLOCSYNCPRIMITIVEBLOCK),"
            "sizeof(MTGPU_BRIDGE_OUT_ALLOCSYNCPRIMITIVEBLOCK)};\n"
            'MODULE_LICENSE("GPL");\n'
        )
        (probe / "bridge_size_probe.c").write_text(source_text)
        build = subprocess.run(
            ["make", "-s", "-C", str(kernel_build), f"M={probe}", "modules", "-j2"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        require(build.returncode == 0,
                "temporary 5.2 C size probe failed: " + (build.stderr or build.stdout)[-3000:])
        dump = probe / "sizes.bin"
        objcopy = subprocess.run(
            ["objcopy", "--dump-section", f".rodata={dump}", str(probe / "bridge_size_probe.o")],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        require(objcopy.returncode == 0, "objcopy could not read the temporary 5.2 size probe")
        data = dump.read_bytes()
        require(len(data) == 8, f"unexpected 5.2 C probe output size: {len(data)} bytes")
        input_size, output_size = struct.unpack("<II", data)
        header_text = generated.read_text(errors="replace")
        require("MT_UINT64 ui64MemType" in header_text and
                "MT_UINT64 ui64SyncPrimVAddr" in header_text,
                "5.2 sync struct fields changed; review the new reference ABI")

        all_headers = source / "inc/mt/generated"
        all_functions = read_kmd_functions(
            all_headers, KMD_5_2_GROUPS, macro_prefix="MTGPU_BRIDGE_"
        )
        all_sizes = compile_kmd_sizes(
            calls, all_functions, include_root, all_headers, "kmd-5-2",
            struct_prefix="MTGPU_BRIDGE",
            include_dirs=(
                "", "common", "imgtec", "mt", "mt/generated", "mt/hwdefs",
                "mt/hwdefs/km", "mt/hwdefs/km/configs", "mt/include",
                "mt/include/mt", "mt/services", "mtgpu", "mtgpu/vgpu",
                "mtgpu-next", "mtvpu", "mtvpu/linux", "shared_include",
            ),
            prelude=(
                '#include "musaconfig_km_1.V.0.0.h"\n'
                '#include "musadefs_km.h"\n'
            ),
            aliases=KMD_5_2_STRUCT_ALIASES,
        )
        all_summary = summarize_reference(calls, all_functions, all_sizes)

    return {
        "dkms_package_sha256": package_hash,
        "header_root": "usr/src/mtgpu-5.2.0-server/inc/mt/generated",
        "input_struct": "MTGPU_BRIDGE_IN_ALLOCSYNCPRIMITIVEBLOCK (ui64MemType: MT_UINT64)",
        "output_struct": "MTGPU_BRIDGE_OUT_ALLOCSYNCPRIMITIVEBLOCK (ui64SyncPrimVAddr: MT_UINT64)",
        "compiled_input_size": input_size,
        "compiled_output_size": output_size,
        "matches_legacy_umd_sizes": input_size == 8 and output_size == 32,
        **all_summary,
        "interpretation": (
            "Header-level ABI comparison against the packaged 5.2.0 DKMS source only; "
            "the package is a Host build and this does not establish Guest runtime support."
        ),
    }


def parse_calls(disassembly):
    lines = disassembly.splitlines()
    calls = []
    for index, line in enumerate(lines):
        if re.search(r"\bcall\s+.*\b92930\b", line):
            calls.append(parse_callsite(line, lines[max(0, index - 250):index]))
    require(calls, "no calls to the PVR SRVKM bridge wrapper were found")
    return calls


def main():
    require(UMD.is_file(), f"missing isolated UMD {UMD}")
    actual_hash = sha256(UMD)
    require(actual_hash == EXPECTED_UMD_SHA256,
            "UMD hash changed; review the new package before trusting the fixed wrapper address")

    result = subprocess.run(
        ["objdump", "-d", "-M", "intel", str(UMD)],
        check=True, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    calls = parse_calls(result.stdout)
    kmd_functions = read_kmd_functions()
    kmd_sizes = compile_kmd_sizes(
        calls, kmd_functions, GENERATED.parent.parent, GENERATED, "kmd-2-3"
    )
    kmd_2_7_functions = read_kmd_functions(KMD_2_7_GENERATED)
    kmd_2_7_sizes = compile_kmd_sizes(
        calls, kmd_2_7_functions, KMD_2_7_INCLUDE, KMD_2_7_GENERATED, "kmd-2-7"
    )
    reference_2_7 = summarize_reference(calls, kmd_2_7_functions, kmd_2_7_sizes)
    reference_5_2 = compile_kmd_5_2_reference(calls)
    for call in calls:
        key = (call["bridge_id"], call["function_id"])
        call["bridge"] = GROUPS.get(key[0], "UNKNOWN")
        match = kmd_functions.get(key)
        call["kmd_2_3_function"] = match["name"] if match else None
        if match:
            call["kmd_2_3_input_size"], call["kmd_2_3_output_size"] = kmd_sizes[key]
            call["input_size_matches_kmd_2_3"] = call["input_size"] == call["kmd_2_3_input_size"]
            call["output_size_matches_kmd_2_3"] = call["output_size"] == call["kmd_2_3_output_size"]

    pairs = {(call["bridge_id"], call["function_id"]) for call in calls}
    matched = pairs & set(kmd_functions)
    absent = sorted(pairs - set(kmd_functions))
    size_mismatches = [
        call for call in calls
        if call.get("kmd_2_3_function") and not (
            call["input_size_matches_kmd_2_3"] and call["output_size_matches_kmd_2_3"]
        )
    ]
    report = {
        "legacy_umd_path": str(UMD),
        "legacy_umd_sha256": actual_hash,
        "kmd_2_3_generated_headers": str(GENERATED.relative_to(ROOT)),
        "callsite_count": len(calls),
        "unique_bridge_function_count": len(pairs),
        "bridge_functions_present_in_kmd_2_3_headers": len(matched),
        "bridge_functions_absent_from_kmd_2_3_headers": len(absent),
        "bridge_functions_with_exact_input_and_output_sizes": len(matched) - len(size_mismatches),
        "bridge_functions_with_size_mismatches": len(size_mismatches),
        "size_mismatches": [
            {"bridge_id": call["bridge_id"], "function_id": call["function_id"],
             "name": call["kmd_2_3_function"],
             "umd": [call["input_size"], call["output_size"]],
             "kmd_2_3": [call["kmd_2_3_input_size"], call["kmd_2_3_output_size"]]}
            for call in size_mismatches
        ],
        "reference_kmd_5_2_schema": reference_5_2,
        "reference_kmd_2_7_1_schema": {
            "include_root": str(KMD_2_7_INCLUDE.relative_to(ROOT)),
            **reference_2_7,
            "interpretation": (
                "Header-level ABI comparison against the local 2.7.1 source tree only; "
                "does not prove that its disabled-Guest build or runtime is suitable."
            ),
        },
        "missing_from_kmd_2_3": [
            {"bridge_id": bridge_id, "function_id": function_id,
             "bridge": GROUPS.get(bridge_id, "UNKNOWN")}
            for bridge_id, function_id in absent
        ],
        "callsites": calls,
        "interpretation": (
            "Static library callsite inventory only. It does not show which calls are reached "
            "by a given application, validate payload field layout, or prove rendering compatibility."
        ),
    }
    output = ROOT / "reports/legacy-umd-pvr-bridge-abi.json"
    output.write_text(json.dumps(report, indent=2) + "\n")

    mismatch_lines = []
    for call in size_mismatches:
        pair = f"0x{call['bridge_id']:02x}:0x{call['function_id']:02x}"
        umd = f"{call['input_size']}/{call['output_size']}"
        kmd = f"{call['kmd_2_3_input_size']}/{call['kmd_2_3_output_size']}"
        mismatch_lines.append(f"| `{pair}` | `{call['kmd_2_3_function']}` | {umd} | {kmd} |")
    missing_ids = ", ".join(
        f"`0x{bridge_id:02x}:0x{function_id:02x}`" for bridge_id, function_id in absent
    )
    reference_lines = [
        "| Schema | Purpose | Covered IDs | Exact sizes | Different or unmodeled |",
        "| --- | --- | ---: | ---: | ---: |",
        f"| 2.3.0 | Guest KMD | {len(matched)}/205 | {len(matched) - len(size_mismatches)} | {len(size_mismatches)} size differences, {len(absent)} missing IDs |",
        f"| 2.7.1 | Native KMD headers | {reference_2_7['bridge_functions_present']}/205 | {reference_2_7['bridge_functions_with_exact_sizes']} | {reference_2_7['bridge_functions_with_size_mismatches']} size differences, {reference_2_7['bridge_functions_absent']} missing IDs |",
        f"| 5.2.0 | Host DKMS headers | {reference_5_2['bridge_functions_present']}/205 | {reference_5_2['bridge_functions_with_exact_sizes']} | {reference_5_2['bridge_functions_with_size_mismatches']} size differences or unmodeled payloads, {reference_5_2['bridge_functions_absent']} missing IDs |",
    ]
    markdown = [
        "# Legacy Linux UMD PVR bridge ABI 审计",
        "",
        "日期：2026-09-29。只对隔离解包的 legacy UMD、本地 2.3.0 Guest KMD、2.7.1 Native 头文件和 5.2.0 DKMS Host 头文件做静态反汇编、结构核对及临时 C 大小探针；没有安装软件包、加载模块、打开 DRM 设备或执行 MUSA 程序。",
        "",
        "## 结果",
        "",
        f"`libsrv_um_MUSA.so.1.0.0` 中识别出 {len(calls)} 个对 PVR SRVKM wrapper 的直接调用点，桥接 ID/function ID 均唯一。当前 2.3.0 Guest KMD 生成头文件覆盖 {len(matched)} 个 ID；{len(absent)} 个 ID 没有对应定义。对已覆盖的命令，以当前内核构建头文件环境临时编译 `sizeof()` 结果后，{len(matched) - len(size_mismatches)} 个输入/输出长度相同，{len(size_mismatches)} 个不同。完整调用点、长度和命令映射保存在 [JSON 清单](legacy-umd-pvr-bridge-abi.json)。",
        f"本地 2.7.1 源树的头文件作为 ABI 参照覆盖 {reference_2_7['bridge_functions_present']} 个 ID，其中 {reference_2_7['bridge_functions_absent']} 个缺失；{reference_2_7['bridge_functions_with_exact_sizes']} 个长度完全一致，{reference_2_7['bridge_functions_with_size_mismatches']} 个不同。该对照仅说明生成头文件层面的匹配度；这份 2.7.1 源树自身配置仍为 `RGX_NUM_OS_SUPPORTED=1`，不代表现成 Guest 运行栈。",
        f"官方 5.2.0 DKMS Host 包的生成头文件覆盖 {reference_5_2['bridge_functions_present']} 个调用 ID；{reference_5_2['bridge_functions_with_exact_sizes']} 个调用的编译结构长度相同，{reference_5_2['bridge_functions_with_size_mismatches']} 个有长度差异或头文件未建模结构。其 Sync 原语分配结构为 8/32 字节，与 legacy UMD 相同；此 DKMS 包是 Host 配置，不能直接作为 Guest KMD。三套版本逐命令结果均保存在 JSON 清单。",
        "",
        "长度或编号不一致本身不总能证明失败：bridge dispatcher 会按调用方提供的长度复制缓冲区，且若干 2.3 KMD 输入结构只有一个占位字段，legacy UMD 对它传 0 字节。不过，以下结构差异不能只凭 Connect ABI 判作兼容：",
        "",
        "| Bridge ID:function ID | 命令 | UMD 输入/输出字节 | 2.3 KMD 输入/输出字节 |",
        "| --- | --- | ---: | ---: |",
        *mismatch_lines,
        "",
        "### 跨版本头文件对照",
        "",
        *reference_lines,
        "",
        "5.2 的“不同或未建模”包含生成头文件没有对应 IN/OUT 结构的命令，不能把 0 字节探针结果当成 handler 预期长度。对照只负责缩小差异范围，不替代对 dispatcher 和具体 handler 的分析。5.2 DKMS 源码配置关闭多 OS Guest；2.7.1 头文件来自 Native 单 OS 源树。",
        "",
        "### 需要优先解决的结构差异",
        "",
        "- `SYNC/ALLOCSYNCPRIMITIVEBLOCK`（`0x02:0x00`）：UMD 请求 8/32 字节；2.3 KMD 结构是 4/28 字节，而 5.2 DKMS 头文件是 8/32。2.3 bridge handler 不读取输入的 `ui64MemType`，在输出偏移 `+24` 写 32 位 `ui32SyncPrimVAddr`；UMD 随后按 64 位读取该地址。bridge staging 区由 `OSAllocZMem(0x3000)` 零分配，所以高 32 位会为零。仍需确认 32 位 VA 足够，以及忽略 memType 不会改变分配语义；这两点都不能从当前静态证据推出。",
        "- `RGXKICKTA3D3`（`0x82:0x0e`）：UMD 发 276 字节，2.3、2.7.1 和 5.2 头文件的输入结构均为 268 字节，字段名和顺序相同。2.3 KMD handler 的可见读取到输入偏移 `0x108` 的 32 位 `ui32TACmdSize` 为止，没有读取 268 字节结构之后的 8 字节；dispatcher 按调用方长度接收输入（上限 `0x2000`）并复制到 staging 区，不要求长度等于 C 结构大小。因此这 8 字节很可能是尾部扩展并会被旧 handler 忽略，但 UMD 端字段偏移尚未完全恢复，先标为高概率兼容而非已证明。",
        "- HWPerf/PFM（`0x86:0x08`–`0x0e` 中七个已调用命令）：输入/输出长度和/或结构定义不同，包含 UMD 的 1176 字节 dump-trigger 配置，而 2.3 KMD 对应命令没有输入结构。此组不应按同名命令视作兼容。",
        "",
        "16 个剩余长度差异是 UMD 传 0 字节、KMD 结构含 4 字节空结构占位符，输出长度相同。此差异有机会是无害的生成器占位符差异，但需要逐个确认 handler 不读取该字段；目前只从核心对象确认 `Disconnect` handler 不读输入。",
        "",
        "2.3 KMD 缺少的调用 ID：",
        "",
        missing_ids,
        "",
        "## 对适配的决定",
        "",
        "结果支持继续以官方 2.3.0 Guest KMD 为适配起点：它已有 Guest/VZ 构建路径；5.2 Host 包虽更接近 UMD 结构，却编译时关闭多 OS Guest。当前还不能把整套 Linux 5.2 UMD 宣称为可用配套。下一步应恢复 Sync `ui64MemType` 的实际传值，并追踪启动/普通提交是否触发缺失命令和 HWPerf/PFM 命令，再决定是否需要针对性 bridge 兼容层；无需因 `RGXKICKTA3D3` 的 8 字节差异先改 KMD。",
        "",
        "## 复现",
        "",
        "```sh",
        "python3 scripts/audit-legacy-umd-pvr-bridges.py",
        "```",
        "",
        "脚本校验 legacy ELF 与 5.2 DKMS 包 SHA-256，使用 `objdump` 提取 bridge 参数，从本地 2.3/2.7.1 和临时解包的 5.2 头文件编译结构大小探针；仅生成临时 `.o/.ko` 文件，不加载该文件。",
        "",
    ]
    (ROOT / "reports/legacy-umd-pvr-bridge-abi.md").write_text("\n".join(markdown))
    print(json.dumps({key: value for key, value in report.items() if key != "callsites"}, indent=2))
    print(f"Full callsite inventory: {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
