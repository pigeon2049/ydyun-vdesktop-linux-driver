# Windows vGPU 驱动离线反编译库

## Linux legacy UMD 补充反编译（2026-09-29）

为追踪 Linux Guest bridge ABI，另对隔离解包的 `libsrv_um_MUSA.so.1.0.0` 执行了 Ghidra 12.1.4 headless 全量分析。`decompiled/linux-legacy-umd-5.2.0/` 保存了函数伪 C、地址/调用索引、字符串及进度；8411 个本地函数中 8410 个成功导出，唯一失败函数 `FUN_002d7f80` 的原始 x86-64 汇编单独保存在 `failed-function-002d7f80.asm`。伪 C 是静态分析结果，不是原始源码，也不能直接编译。

该 UMD ELF SHA-256 为 `b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0`。保留的 Ghidra 工程位于 `ghidra-projects/linux-legacy-umd-5.2.0/`；bridge 调用提取、结构大小编译与 2.3/2.7.1/5.2 ABI 比对可用 `python3 scripts/audit-legacy-umd-pvr-bridges.py` 重现。

## 补充分析（2026-09-28）

原批次之外，已从回调表和取址标签补识别 `mtkm64.sys` 的 **107 个代码入口**，
修正一处快速失败后误合并函数边界，并排除 16 个被误当成代码候选的字符串。
结果保存在 `recovered-*` 子目录及既有 Ghidra 工程；原批次文件保持不变。
`corpus.py function` 优先查询修正后的补充结果。14 个原始指令案例验证关键回调，
不代表所有新增入口语义均已确认。详见 [补充分析](reports/callback-recovery.md)。

## 已完成批次

2026-09-27 北京时间 19:06 至 22:22，全量批处理完成，用时约 3 小时 16 分钟。
29/29 个二进制均已分析并保存工程，成功导出 223,447 个本地函数；
22 个函数在 60 秒首次尝试及 180 秒重试后仍未成功：12 个超时、2 个指令数量上限、
8 个反编译器分析错误。失败地址、原因及汇编全部保留。

三个内核驱动共 5,675 个本地函数全部导出成功：`mtkm64.sys` 4,219 个、
`mtdispkm64.sys` 680 个、`mtvpukm64.sys` 776 个。
函数级“成功”表示工具返回伪 C，不保证没有控制流警告或已还原原始语义。

收尾的 `corpus.py verify` 已通过：文件覆盖、函数数量、C 行号索引、源文件哈希和
工程存在性检查无异常。结果文本约 2.4 GiB，Ghidra 工程约 2.6 GiB。
汇总为 `reports/decompilation-summary.json`，验证为 `reports/decompilation-verification.json`。
此批次完成的是反编译资料库，尚未使 Linux Guest 显卡获得硬件加速。

## 输入和工具

输入：`/opt/MTT-driver-only/` 中全部 29 个 PE 文件（3 个 `.sys`、26 个 `.dll`），
合计 436,843,200 字节。原文件不修改、不执行。

工具：官方 Ghidra 12.1.4，发行包 SHA-256 已与 GitHub release digest 核对；
依赖从 Debian 仓库安装 `openjdk-21-jdk-headless` 和 `ripgrep`。
工具来源和校验记录：`reports/decompiler-toolchain.json`。

## 保存内容

每个二进制的结果位于 `decompiled/<原文件名>/`：

| 文件 | 用途 |
| --- | --- |
| `decompiled.c` | 每个被识别本地函数的伪 C，包含原始地址 |
| `functions.jsonl` | 函数地址、名称、签名、C 文件行号、成功/失败状态；也记录外部函数 |
| `calls.jsonl` | 静态可识别的函数调用关系 |
| `symbols.jsonl` | 符号表、导入/导出相关符号 |
| `strings.jsonl` | Ghidra 识别的字符串及引用它们的代码地址 |
| `disassembly.txt` | objdump 完整可执行节汇编 |
| `pe-headers-imports-exports.txt` | PE 节、导入导出及重定位等信息 |
| `strings-ascii.txt`、`strings-utf16.txt` | 带文件偏移的原始字符串 |
| `failures.jsonl` | 两次尝试后仍未成功的函数及错误；不默默跳过 |
| `metadata.json`、`progress.json` | 源文件哈希、导出器哈希、命令、进度和统计 |
| `analysis.log`、`headless.log`、`export.log` | 分析及导出日志 |

完整可重开的 Ghidra 数据库保存在 `ghidra-projects/<原文件名>/driver.gpr`。
没有原厂 PDB，很多函数/变量仍使用自动名称；“完整”指对全部文件及所有被识别函数
执行批量分析和导出，不表示能恢复原始源码、原始注释或保证发现所有间接调用。
伪 C 不能直接作为 Linux 驱动编译。

## 查询已有结果

在 `/opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest` 下运行：

```sh
python3 scripts/corpus.py status
rg -n 'VGPU|Firmware|Shared|Host' decompiled/mtkm64.sys/decompiled.c
python3 scripts/corpus.py function mtkm64.sys 140003af4
python3 scripts/corpus.py verify
```

查询只读取已保存文本，不会再次启动反编译。
验证检查文件覆盖、函数总数、失败记录、C 行号索引、源文件哈希和 Ghidra 工程是否存在。
结果写入 `reports/decompilation-verification.json`。

## 批处理与恢复

```sh
python3 scripts/decompile-drivers.py --workers 2
```

脚本按源文件和导出器哈希跳过已完成的结果。定向重跑可使用
`python3 scripts/decompile-drivers.py --only mtkm64.sys`；它只重做指定文件，
同时保留其余二进制在完整清单中的记录。仅导出脚本变化时复用已保存工程，不重跑自动分析。
进程中断的文件会重试；已经完成的文件不受影响。
默认每个函数首次超时 60 秒，失败后重试 180 秒，仍失败就保留明确的失败记录和汇编。
批处理使用独立项目防止并发锁冲突，最多同时运行两个 Ghidra 实例。

本次任务通过临时用户服务 `mt-vgpu-decompile.service` 运行，日志为
`reports/decompile-run.log`，不设置开机启动，不是定时任务。
可用 `systemctl --user status mt-vgpu-decompile.service` 查看运行状态；结束后单元会自动回收。
批次总状态以 `decompiled/manifest.json` 和 `corpus.py status` 为准。`corpus.py verify`
还会把清单成员和输入目录中的全部 SYS/DLL 做集合比较，防止仅有部分清单时误报完整验证。

厂商二进制、反编译大文件、Ghidra 安装目录和数据库均保留在本机并由 Git 忽略，
不会自动提交或发布。
