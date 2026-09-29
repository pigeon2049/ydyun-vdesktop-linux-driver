# 进程、任务上下文与 VM 关联

本轮继续本机 Guest 适配，没有执行通信恢复、宿主操作或真实 GPU 工作负载。

## 原始指令证据

`1411ca690`（SIMCreateContext）分配并清零 0x88 字节软件对象：
Context+0 指向设备，+0x10 指向选中的逻辑节点，+0x30 初始为零，后续保存根页表；
+0x28/+0x38 继承设备的内存管理相关指针，+0x70 保存调用方句柄。
正常分支调用 `14001e000` 申请 0x5000 字节、对齐参数 0x80 的辅助分配，将句柄保存在
+0x68；另有设备标志可跳过此分配。节点类型 1 额外拥有第二组 DMA 管理数组。
这些都是驱动软件管理状态，不能将整个 0x88 字节对象当成固件命令结构直接上传。
辅助分配内部内容/用途和特殊 GDI/系统上下文分支仍未还原完成。

`14000a750` 经 `14000a600` 解析根地址参数：segment 为零时直接取偏移，否则加对应
segment 的基址，再调用 `140021d90` 做平台地址转换。转换结果写入 Context+0x30 和
Process+0xa8。Linux VM 的 BO 已持有 GPU 物理地址，不重复对它加 BAR CPU 地址。
本轮验证了 segment 加法及关联写入；平台转换 helper 的完整行为仍未验证。

`140015048` 将 Adapter+0x1618 的 64 位序号赋给 Process+0xa0 后递增。
`140014344` 从 Context+0x30 和所属 Process+0xa0 取字段进入先前验证过的提交包。
PID 与进程 token 分开处理，不把用户 PID 当 GPU 地址或进程唯一序号。

`scripts/verify-execution-context.py` 在隔离 RAM 中执行原始函数：
6 种节点的正常 Context 创建路径、12 个根地址关联和 4 个进程 token 案例通过。
Windows 分配/日志、辅助 BO、进程页表初始化及平台地址转换被显式建模；没有执行硬件。
详见 [原始指令记录](execution-context-validation.json)。

## Linux 接入

新增 `kernel/mt_execution_context.h`，提供会话级 token 管理器、进程和任务上下文：

- 进程绑定一个 VM，持有 VM owner；即使 VM 尚未封存，也不能在进程存活时销毁。
- 多个上下文可共享进程 token 与 VM，各自选择逻辑节点/固件 DM；PCIe DMA、timer
  等非固件 DM 节点明确拒绝，未自动选择本机的节点 profile。
- token 从零递增，到 64 位上限后拒绝新建，不回绕复用；失败不消耗 token。
- `submit_context` 根据上下文提供 DM、进程 token、PID、页表根。调用者只提供命令 VA、
  类型、长度与 flags。fence 编号继续由 pending 引擎产生。
- 提交检查上下文所属 BO 管理器与当前队列匹配，沿用 VM/BO 全部资源持有和队列回滚。
  待完成任务持有上下文，正确完成时先释放资源并减少活跃任务计数，再通知 dma_fence。
- 上下文有待完成任务时禁止销毁，进程有上下文时禁止销毁；任务完成不解除 VM 封存。

主模块初始化执行管理器，buffers 只读统计添加 processes/contexts，并在移除检查中
保留这些对象。没有添加用户 ioctl、注册 DRM 节点或打开工作提交开关。

本轮依据 `SIMDestroyProcess` 路径补强 `mt_execution_process_destroy`：除仍有上下文外，
只要 VM `active_uses` 非零也拒绝销毁，防止没有软件 context 绑定的通用 pending fence
仍引用页表/BO 时进程 owner 被提前丢弃。该新增检查本轮只做 W=1 编译，未重跑下方 RAM 自测。
当前主模块 SHA256 为 `5b86c76562265b0d33ab18061c9b08441517942840ff40769384149456f4338c`。

## 验证与限制

新增 ASan/UBSan 测试覆盖 VM 所有权、共享进程、节点路由、身份字段、跨设备拒绝、
忙状态销毁、token 耗尽；现有对象/任务/运行态测试通过。主模块 W=1 构建无警告，
保留 `mt_guest` ABI 对照通过。真实 dma_fence/RAM 队列测试通过 **216 项检查**，
包括上下文提交生成的 token/PID/队列、关闭保护和完成后销毁；临时模块已卸载。

- 主模块 SHA256：`5907a74571e007e549bc19270f90522e0eb386c0d75cf3002f2b0ba7884d0209`
- fence 自测 SHA256：`2d0a78baf722f48ffbad32a2a5553bb482cf1f5e072d3f205320bcf934090fa0`
- [构建与回归](runtime-integration-build.json)、[内核 RAM 测试](fence-kernel-validation.json)

**尚未实现硬件加速**。真实固件上下文、辅助缓冲区格式、完整命令流与任务类型/节点
兼容性、用户态接口及根页表撤回仍需适配。当前工作使能保持关闭，没有把软件上下文
创建当成固件接受或 GPU 执行的证据。下一步继续分析 `14000f008`、`140020a1c` 等
辅助执行上下文构造与命令流生成路径。
