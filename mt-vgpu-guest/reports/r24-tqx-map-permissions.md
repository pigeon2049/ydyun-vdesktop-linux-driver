# r24：修正 TQX 普通页的只读映射

北京时间 2026-09-30。上一阶段 r23 真机已完成 marker fence，首个 256 字节 TQX
复制返回 0x101 故障。本轮从保存现场定位并修正一个具体页表权限错误，没有重新提交硬件任务。
这证明存在错误，不证明它是上次故障的唯一原因。GPU 加速仍未完成。

## 原始指令交叉验证

r23 helper 给五个普通 BO 的 `bind` 传入数值 `3`。接口的 flags 是 Windows MMUMapPage
位域，不能当作 Linux/POSIX 的 READ|WRITE。

`140018bd0` 将输入 bit0 移到 PTE bit1；`14002a8d8` 保留该位。
原厂 Linux 2.3 的 `RGXDerivePTEProt8`，结合 `mmu_common.h` 中
VALID=1、READABLE=2、WRITEABLE=4、CACHE_COHERENT=8，提供独立语义证据：
仅 READABLE 时置 PTE bit1，READABLE|WRITEABLE 时不置该位。

| Guest map flags | 生成 PTE 的低位 | 含义 |
|---|---|---|
| 0 | 1 | 有效、可写、未请求 coherent |
| 1 | 3 | 有效、只读 |
| 2 | 5 | 有效、可写、coherent |
| 3 | 7 | 有效、只读、coherent |

`verify-r24-map-permissions.py` 执行 Windows 的分配/映射/编码路径和 Linux 原始叶函数，
对照上述四组及带设备标志 0x10 的四组，共八组通过。Windows GPU 分配和地址转换为模型；
Linux 仅跳过 fentry，限制执行在无外部调用的有效输入路径。所有代码均来自散列核验的本地参考。

保存的真实目标页 PTE=`0x61002f007`，因此确实只读；默认可写候选应为 `0x61002f001`。
Linux 原厂额外设置高位 `0x3c00000000000000`，Windows 编码器没有该字段。
本轮没有擅自把 Linux 高位写进 Windows Guest 页表，该差异仍需独立解释。

证据：[八组原始指令验证](r24-map-permissions.json)、
[Linux 原始函数反汇编](r24-rgx-pte-protection.asm)、
[r23 真机页表快照](r23-tqx-memory.json)。

## 实现修正

- `mt_mmu_bootstrap.h` 命名 DEFAULT、READ_ONLY、CACHE_COHERENT 三个已核实标志，明确位域含义。
- 下一版 `mt_live_tqx` 五个普通 BO 改用 `MT_GPU_MAP_DEFAULT`。
- `mt_ce_copy_resolve_access` 可按实际匹配的 BO/VA 映射检查写权限，保留现有只读解析接口。
- TQX 完整提交准备对目标、DMA 工作区、引擎状态要求可写，在任何上传 I/O 前拒绝 `-EACCES`。
  只读源仍可使用；shader/命令等读取角色不会被笼统拒绝。
- 原先使用数值 3 的 TQX RAM 测试夹具改用明确的默认可写标志。

回归覆盖三个写入角色分别使用只读及只读+coherent（六种失败），确保没有 write/read/map，
没有改变原提交结果；只读源成功。既有 alias、边界、I/O 故障、pin 和生命周期检查继续通过。

## 构建与验证

99 项 Python 检查、15 项 RAM/ASan/UBSan/集成检查、W=1 主模块及 helper 构建通过。
实际内核 RAM 自测 GEM=280 项、fence=330 项通过；只加载/卸载自测模块，前后绑定驱动、
主模块 srcversion 和 DRM 节点一致，没有把 RAM 自测视为 GPU 复制证明。

候选已固定保存在 `build/r24-candidate/`：

| 文件 | SHA-256 |
|---|---|
| mt_guest_probe.ko | c1c97955663a86d3f56703e69cfcfb799f7c1bbdd1552544c8238a9902b1a662 |
| mt_live_marker.ko | 54c45e89cffa6d1ed98d8b921163c420dbfcbaabe3c96c80bfa0059952cb1f77 |
| mt_live_tqx.ko | 3d2f118a168db68bc4a7edd9276fae6e0cfd1e7deecc96d30045f3343e4dc87d |

[候选清单](r24-candidate.json)、[集成日志](r24-integration.log)、
[GEM 内核 RAM 日志](r24-gem-ram.log)、[fence 内核 RAM 日志](r24-fence-ram.log)。
候选尚未加载，没有安装启动项。当前机器仍运行 r23 保存的两个模块；磁盘新文件不是运行态身份。

## 重新实测的条件

本轮结束前的 Guest 会话仍为 `Guest=0 / FW=0 / started=1`、pending=1。
`fresh-trial.py --runtime-context` 正确拒绝替换该会话；[只读 preflight](r24-current-preflight.json)。
旧 VM/BO 的引用、GPU pin 和原始故障事件仍保留，不能在原地修改旧页表再重发。

下一轮需先重启 Guest，重读 boot ID、PCI 绑定及 Guest/FW 状态。
只有设备未绑定且 Guest=0、FW=READY(1)，才运行新的 runtime trial。
之后先重复 marker 往返，再准备 TQX，确认页表目标/状态/DMA 的只读位清零后才单次提交。
若出现新故障，继续保留队列和全部资源。当前已询问 Guest 重启时机，等待用户安排；不需要操作宿主机。
