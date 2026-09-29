# 保留会话的通信恢复工具

当前 r34 新增 `mt_live_surface.ko`，在 token=3 的独立根上提供 8 MiB/4 MiB/六个 64 KiB 槽，已实测 1080p 填充。此模块已自持引用，不应卸载；旧 r28/r31/r32 根均保留。见 [r34 真机记录](../../reports/r34-large-surface.md)。`mt_drm_snapshot` 新增 `expected_pages` 和 `require_sealed`，可只读核验发布前的大表面根；本次快照模块均已卸载。

**当前 r32**：`mt_guest_probe`、`mt_live_tqx`、`mt_live_copy_bridge`、`mt_live_drm` 与 `mt_live_graphics` 正在运行。已完成复制和原生矩形填充，固件正常。四个持有封存根/上下文的模块不能热卸载，快照 helper 已卸载。使用 [会话检查](../../scripts/check-graphics-session.py) 和 [r32 记录](../../reports/r32-native-fill.md)；下文恢复工具描述是历史状态。

## 历史状态（早期重启后）

目前只加载主模块 `mt_guest_probe`，由其 `recover_channels=1` 模式处理 IRQ/RPC。
下文 `mt_live_service` 的加载、自持引用与 IRQ 屏蔽描述均属于重启前历史状态。
不要按照历史步骤再次加载旧辅助模块。

新增临时工具已完成取证并正常卸载：

- `mt_retained_dump`：只读保存六段上传资源，共 9,478,144 字节。
- `mt_package_probe`：查询更新开关与包元数据，未下载或安装。
- `mt_hwr_request`：一次性故障恢复入口，已做只读加载/卸载预检，未触发。
  **可能请求 Host MPC 级重置，不能当作仅影响本 VM 的操作。**
  用户已明确宿主无法操作，此入口不执行；见 [历史准备记录](../../reports/hwr-request-preparation.md)。
- `mt_master_notify`：仅在核实原始 Guest PCI 启用步骤后，临时开启 MASTER 并通知
  既有五条命令一次；无新命令、无重置，20 秒自动恢复 PCI 位。实测未推动固件，
  自动恢复及卸载已完成，见 [本轮依据](../../reports/pci-master-audit.md)。

最新构建的主模块还包含异常健康值门控，但在用的通信恢复模块未替换。
现场与验证范围见 [当前进度](../../reports/progress-2026-09-28.md) 和
[健康门控记录](../../reports/guest-health-gate.md)。

## 历史：首次失败连接后的辅助通信

`mt_live_service.ko` 是首次失败固件连接试验的临时辅助模块，已在本机加载。
主模块 `mt_guest_probe` 仍绑定且自持引用；辅助模块不解绑设备、不释放上传资源、
不改固件状态，也不清空或新增固件命令。

辅助模块复用已注册的四页通信内存，持有主模块和页面引用，并使用原 trial mutex
串行处理宿主查询。它依赖 `mt_guest_state.h` 与当前加载模块的结构布局完全一致。
首次加载前已用 pahole 对比两个模块的 `struct mt_guest`，结果一致；不是通用热补丁机制。
当前主模块原始二进制保存在 `build/host-query/first-trial-loaded-module.ko`。

已实现：

- 仅在共享页 0 的 DWORD +4 等于 1 时确认 MTT 中断：写 2，QWORD +8 加 1。
  IRQ10 为 shared，其他来源返回 IRQ_NONE。MTT PCI INTx 配置位仍保持屏蔽。
- 请求环 1、回复环 2；type=1/subtype=1 回复当前保留分配的总字节数，
  subtype=0 回复当前无作业状态下的利用率初始值 0。忽略输入 payload 中的随机数据，
  不解释为指针；未知请求不消费。IRQ 调度及每 250 ms 补充轮询处理已知查询。
- root-only `control` 支持 `publish-bar2`（一次，提交 BAR2 客户物理基地址）、
  `publish-local-mmu`（一次，确认 BAR4 不存在后向 BAR1+0x28 提交 0）、
  `announce-package`（一次，mode=1/版本已接受时提交参考包标识，不查询或下载更新）、
  `announce-shared`（一次，提交参考 type=3 的三条共享区描述）和
  `notify-online`（只重发上线通知及 DM0 doorbell，不增加队列条目）。
  操作要求当前仍 pinned/published/registered，Guest1/FW1。

当前状态读取：

```sh
cat /sys/bus/pci/devices/0000:00:0e.0/mt_live/status
cat /sys/bus/pci/devices/0000:00:0e.0/mt_live/publication
cat /sys/bus/pci/devices/0000:00:0e.0/mt_guest/trial
```

`publication` 只回读 BAR1+0x20/+0x28/+0x30/+0x38，并列出保留固件分配的预期值。
当前回读匹配 BAR2 GPA、缺席 BAR4、FW PA=0x771fef000、长度=0x800000；
这证明接口值可读回，不证明宿主内部映射和固件运行已经建立。

`mt_irq_recover.ko` 是先前仅重新启用共享 IRQ10 的一次性工具。
实测单独使用它不能解决中断来源未确认的问题；不要用它代替协议处理。

主模块源码现已通过 `mt_rpc_service.h` 接入首次连接前的 IRQ/RPC 生命周期，
并在连接/断开等待中同步处理查询。它与此辅助模块共用 `mt_rpc_transport.h`。
新版主模块已编译，尚未替换当前自持引用的旧模块；当前机器仍由此辅助模块服务。
没有安装自动加载配置。辅助模块恢复通信成功，不代表固件连接成功，更不代表可渲染。
