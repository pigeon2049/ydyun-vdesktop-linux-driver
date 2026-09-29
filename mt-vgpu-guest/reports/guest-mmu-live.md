# Guest LMA / MMU 真机进展（2026-09-29，r19b–r22b）

本轮在 Guest 本机完成，未操作宿主机。**显存分配、三级页表和固件内核虚拟堆已通过有边界的真机测试，但 GPU 加速尚未启动，没有 MTT render 节点。** 测试结束后已卸载模块、解除 PCI 绑定并清空 driver_override；没有安装这些诊断模块作为启动驱动。

当前 boot ID 为 `9635196d-8d84-4701-b9bc-9ff6e2bb8e24`，内核 `6.12.107+deb13-amd64`，设备 `0000:00:0e.0 / 1ed5:0222`。当前协商结果是 V2 / OSID 6；此前 OSID 4、7 记录属于历史快照，不应直接套用。模块完整路径、SHA-256 和 build ID 见 `guest-mmu-live-validation.json`。

## 已验证链路

| 测试 | 实机结果 | 证据 |
| --- | --- | --- |
| r19b LMA 两页读写 | 原厂分配器分配并映射两页，不同模式读回正确，恢复原内容，释放资源 | `r19b-live-kernel.log`、`r19b-after-backing.json` |
| r20 MMU 根上下文 | 创建、查询根地址、销毁均成功，Host 根地址 `0x60f800000` | `r20-live-kernel.log` |
| r21 FW 内核上下文 | FW_MAIN、FW_CONFIG、MMU 上下文均创建成功，随后销毁 | `r21-live-kernel.log` |
| r22b 三级页表与 PMR | 两页真实 GPU-local PMR 分配、映射、地址核验、撤销和释放通过 | `r22b-live-kernel.log` |

r22b 在未提交给固件的内核上下文中使用 VA `0x100000000`，原厂 MMU_Alloc 建立下级页表：

- PC 项 `0x60f8011` 解码为 Host PA `0x60f801000`；与下级目录实际 backing 的 V2 转换结果一致。
- PD 项 `0x60f802001` 解码为 Host PA `0x60f802000`；与页表 backing 的 V2 转换结果一致。
- PMR 页相对地址 `0x2200000 / 0x2201000` 转换为 `0x611000000 / 0x611001000`。
- PTE 为 `0x3c00000611000001 / 0x3c00000611001001`，地址字段与上述结果一致，映射时有效。
- 撤销后两页 `MMU_IsVDevAddrValid=0`，最终 `result=0 mapped=1 cleanup_complete=1 submission=0`。

r22 初次试验的映射部分成功，但撤销检查返回 -EIO。反汇编确认 `DevmemIntUnmapPages` 向 MMU_UnmapPages 传入零 mapping flags；诊断误传入原映射权限会触发替代页语义。r22b 修正调用参数并保留失效检查，复测通过。没有修改原厂 MMU_UnmapPages 实现或绕过失败检查。

## 堆布局修正

V2 typed 段表与 Windows `140026390` 原始指令 oracle 用于重建互不重叠的 PVR heaps：

| 用途 | BAR2 相对地址 | 长度 | 状态 |
| --- | --- | --- | --- |
| MMU 私有池 | `0xa00000` | 24 MiB | 分配、CPU 读写、页表层级实测 |
| GPU 通用池 | `0x2200000` | `0x3ce00000` | 两页 PMR 分配及 PTE 地址实测 |
| FW_MAIN | `0x3f000000` | 8 MiB | 仅堆构造与虚拟上下文，未执行固件加载 |
| VIDEO 候选池 | `0x3f800000` | 56 MiB | 仅构造；视频分配和固件用途仍未验证 |

CPU 地址等于 BAR2 基址 `0x800000000` 加相对地址；写入页表的 Host PA 由 V2 段表逐段转换。MMU 使用 Windows 普通私有池，绝不能回退到 BAR2 零点的 PB backing。合并 VPU group1/group2 的 usage flags 是本次诊断配置，不能据此宣称视频功能可用。

`tests/test_pvr_heap_layout.py` 比较 Windows 原始指令的私有池、通用池和固件池，另验证截断、BAR 太小、地址溢出、未对齐和不足以拆分的固件池被拒绝，错误输出保持原值。最新 Python 全套 96 项通过，见 `r22b-unit-tests.log`。

## 数据与退出状态

实验前后，整个 PB 2 MiB 与 FW_MAIN 8 MiB 哈希保持一致，见 `r19-before-backing.json`、`r20-after-backing.json`、`r21-after-backing.json`、`r22b-after-backing.json`。r19b 的整个 24 MiB 普通私有池也恢复一致。之后上下文测试会正常初始化并回收页表，因此私有池字节变化是预期行为；释放不代表恢复已释放页的原内容。

全部模块正常卸载，没有观察到 Oops 或 D 状态调试进程。原厂退出路径仍报告 system-wide workqueue flushing 的内核警告；这是弃用警告，不能描述为完全没有内核警告。

诊断通过 `DevmemIntCtxCreate+0x79` 和 `RGXInit+0x588` 的受保护 ELF 重定位插入测试边界。r20 在 MMU 上下文向 Devmem 返回前停止；r21/r22b 允许第一个内核上下文登记，创建虚拟堆后销毁并返回 INIT_FAILURE，阻止后续 RGXWinFWInit。日志中的该初始化错误是主动停止点，不是声称整机初始化成功。

## 下一适配点

需要完成 Guest 固件加载/连接、上下文发布及命令队列，随后才能验证 render 节点与 GPU 执行。`RGXWinFWInit -> LoadWindowsFirmwareRobust -> LoadWindowsFirmware` 包含固件内存写入、重试卸载和 RGXStart，后者包含整卡寄存器启动序列。已保存 `r20-win-fw-init.asm`、`r21-load-win-fw.asm`、`r21-load-win-entry.asm`、`r22-rgx-start.asm`；仍需逐段核对 Guest 应走的路径与 Windows Guest 描述符协议，不能将现有 Host 启动流程直接认作 Guest 初始化。

本报告证明 Guest 内存与 MMU 软件/硬件 backing 链路，不证明固件接收了根地址，也不证明 GPU 执行任务。当前没有强制修改 FW state、执行整卡复位或提交 GPU 命令。
