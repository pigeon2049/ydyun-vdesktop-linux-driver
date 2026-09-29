# 本次重启后的实机适配：信息页协商与地址转换

2026-09-29，boot `9635196d-8d84-4701-b9bc-9ff6e2bb8e24`，内核 `6.12.107+deb13-amd64`。
仅操作 Guest。硬件加速仍未完成；本轮诊断在物理堆/MMU 分配前主动返回失败。

## 本次证据改变了什么

开机自动加载的系统模块 build-id 为 `ee63001f24d90b86a273bbc89e8d1586a8ff232c`，
仍停在 `PhysHeapsInit [644]`。它不是 r15b。本轮将其正常卸载，并依次加载/卸载
r16、r16b、r17、r18b 诊断模块；没有 Oops 或遗留 D 状态进程。r18 编译失败，未加载。
最后恢复空 `driver_override`，PCI 未绑定、mtgpu 未加载，QXL card0 保持可用。

r16b 首次完整保存本次 Guest 收到的 v1 页：OSID **6**，不是旧记录的 4/7。
页面虽然 version=1，段记录却是 24 字节（base/size/flags），而 Linux 2.3 头文件
只有 16 字节（base/size）。第二组表从 `0x638` 开始，扩展区从 `0xc68` 开始。
用旧布局会把第二段的 flags=1 当成地址，并把后续大小/固件/MMU 字段读为零。
本轮将解码器增加显式 `--segment-stride 24`，不靠 version=1 自动猜布局。
修正布局后，实际 `0xc48/0xc50/0xc58/0xc60` 的 FW/MMU 独立字段也仍为零，
因此仅压缩记录步长不能解决缺失 BAR4。

Windows `mtkm64.sys!140026b30` 会先清零信息缓冲区，再把 `+0xc48` 的低两位置位，
随后向 BAR1+0xc8 发布缓冲区 GPA。Linux 2.3 遗漏这两个请求位。
r17 在**信息页的那一次** `os_virt_to_phys` 调用前写入 3 并执行屏障，保留原 GPA
查询和寄存器发布。真机响应立即变成 version=2、6 条 typed segment；OSID 仍为 6。
这是同一次启动中的实际协议验证，不是沿用旧 Windows OSID 4 文件推断。

当前 V2 页的地址：

| 用途 | BAR2 相对地址 | Host/GPU PA |
|---|---:|---:|
| PB free list | `0` | `0x56800000` |
| 第一段普通显存 | `0x200000` | `0x60f000000` |
| 第二段普通显存 | `0x5200000` | `0x1ae000000` |
| 64 MiB 固件段 | `0x3f000000` | `0x769fef000` |
| 独立共享窗口 | `0x43000000` | CPU GPA=`0x843000000` |

BAR2 GPA 基址为 `0x800000000`，PCI aperture 为 16 GiB，映射数据区长度为
`0x43000000`；这三个数不是同一个地址/长度概念。

## 实际修改

- 新增 `MTGPU_GUEST_HEAP_AUDIT_ONLY=1`：记录缓存信息页、平台字段和堆描述符，
  在 `PVRSRVPhysMemHeapsInit` 前停止。诊断包装器不分配/写入 GPU 页表，但驱动正常
  probe 仍会写 PCI/信息页协议寄存器，不能把整个加载称为只读操作。
- 新增 `MTGPU_GUEST_REQUEST_INFO_V2=1`：请求 typed V2 格式。目前强制搭配诊断停止、
  >=4 KiB 缓冲区和地址兼容层，不作为可启动 GPU 的生产配置。
- 修正 dispatcher：平台数据 `+0x40/+0x48` 实际是 MMU size/card-base，旧适配错误地
  当作 BAR2 size/base。现在使用真实 `pcie_memory_base +0x08` 和信息页 `+0x20` 的窗口长度。
  PVR 输入是相对设备 PA，必须加 BAR2 GPA 基址后再传给 Windows 语义的转换器。
  Kbuild 对平台字段偏移执行编译期断言。
- 修正调用接线：此前 `objcopy --redefine-sym` 同时重命名了核心调用的目标，
  所以 7 个核心调用仍直接进入旧 V1 函数。新增后链接审计将它们转向 dispatcher，
  仅保留 dispatcher 内的一次 V1 fallback；拒绝未知调用点、错误 relocation 和重复补丁。
- 撤回 r15b BAR2 起点 MMU fallback。BAR2 offset 0 当前映射的是 PB free list，
  不能当作未经分配的页表内存。删除对应实现并禁用旧构建开关。

## 验证和限制

r18b SHA-256：`788c1d084e1aaa277b64e5042de1a95b0a88f92503e2d4a82886e3451b1abed5`。
活动模块 build-id 实测：`1f7b5ead0cea1675fd975d7a22554e975a8106eb`。
真实 `PVRSRV_DEVICE_CONFIG → sysdata → platform data` 链上调用 dispatcher，结果为
`0 → 0x56800000`、`0x200000 → 0x60f000000`、`0x3f000000 → 0x769fef000`，
窗口末端 `0x43000000 → 0`。这些是算术转换调用，不是 GPU 内存读写或 GPU 执行。
7 个核心 MMU 调用点的接线由静态 relocation 验证；本轮没有让这些 MMU 路径实际运行。

94 项 Python 测试通过；当前捕获 V2 页与 Windows 原始 `140027ab4` 指令执行对照的
142 个窗口样本、5 个窗外样本及 5 个合成 flags 样本全部一致。补丁重复应用测试
确认拒绝且不改输入模块。完整构建、原 ftrace 检查和 PCI 初始化顺序检查通过。

仍需修复 **PVR 堆描述符布局**：即便 V2 查询和转换正确，原始核心仍假设 VPU 在
显存前部、FW 位于其后，并保留 BAR4 MMU 地址；GPU_LOCAL/VPU 堆长度仍下溢。
不能拿这些描述符继续执行，也不能把 Host GPU PA 直接塞进要求 BAR2 相对 PA 的字段。
下一步应从当前 V2 段表及 Windows 私有分配器确定互不重叠的 CPU/设备范围，
为 MMU 分配有明确归属的后备，再核对 FW premap/lifecycle。当前没有 MTT render node。

机器结果：`guest-info-negotiation-live-validation.json`。
原始证据：`r16b-linux-v1-info-full.bin`、`r17-linux-v2-info-full.bin`、
`r18b-linux-v2-info-full.bin`、`r18b-live-kernel.log`、`r18b-current-info-oracle.json`。
