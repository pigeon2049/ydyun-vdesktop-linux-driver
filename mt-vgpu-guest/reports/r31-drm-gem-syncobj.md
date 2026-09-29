# r31：直接显存 GEM、DRM render 与真实 GPU syncobj

2026-09-30，boot ID `fe18deeb-d431-4c03-ac88-34235e9849c5`。本轮在原固件会话上
新增独立 GPU 上下文与 DRM 前端，没有替换/卸载原驱动、修改原页表、重启或操作宿主。

## 现在可用的功能

新增 `mt_live_drm` 已注册 DRM 名称 `mtvgpu`、`/dev/dri/card1` 和
`/dev/dri/renderD128`。GPU PCI 仍由原 `mt_guest_probe` 管理；DRM 前端借用它的
BO/VM 分配、提交和完成服务，没有覆盖 PCI drvdata。原 QXL `/dev/dri/card0` 保持不变。

应用可创建 GEM 句柄、显式读写显存内容，通过句柄提交显存到显存复制，并通过标准
DRM syncobj 等待和导出 sync_file。COPY 阶段直接由 GPU 操作 GEM 背后的显存，
不走 r30 的 CPU 源/目标中转，也不使用 CPU 计算结果替代 GPU 结果。

这只是复制/同步前端。没有实现图形绘制、原厂 PVR/MUSA ABI、Mesa 驱动、
OpenGL/Vulkan 或桌面加速；render 节点存在不等于这些功能已完成。

## 真机证据

新上下文 token=1，根 GPU PA=`0x606033000`，32 页保留区中使用 18 页，20 个映射。
另外持有 8 个 64 KiB 显存槽，以及独立 command、DMA 和 engine-state。
准备时只写入自有新分配并生成页表，不提交任务；第一次 COPY 才封存根并保留模块引用。

DRM 直接显存复制共 9 次，均核对完整 64 KiB 源/目标及范围外保护数据：

| 长度 | 源偏移 | 目标偏移 | fence sequence |
|---:|---:|---:|---:|
| 256 | 0 | 0 | 133 |
| 256 | 0 | 0 | 134 |
| 65536 | 0 | 0 | 135 |
| 32768 | 0 | 0 | 136 |
| 8192 | 4096 | 8192 | 137 |
| 8207 | 3 | 17 | 138 |
| 4097 | 4095 | 12289 | 139 |
| 31 | 65505 | 65505 | 140 |
| 256 | 0 | 0 | 158 |

每次都使用标准 DRM SYNCOBJ_WAIT 和 HANDLE_TO_FD 导出，再通过 poll 与
SYNC_IOC_FILE_INFO 确认一个已完成 fence；原生驱动名 `mt-vgpu-guest`，时间线
`firmware-submit`，有实际完成时间戳。不是手动触发的替代 fence。

在 fence 140 后，旧 r30 接口又复制了 65,659 字节文件，17 次任务使用旧上下文，
输入输出完全一致；随后切回新 DRM 上下文的 fence 158 也通过。证明当前两份固定根
能在顺序提交中切换；不据此声称并发 GPU 执行或动态页表撤销已验证。

驱动累计 completed=158、pending=0，Guest2/FW2/started1/event_result0。
DRM 自有计数 submitted=9/completed=9，leased=0，faulted=0，retained=1。

## GEM 和错误路径

测试程序同时验证：8 槽耗尽后的第 9 次 CREATE 返回 ENOSPC；新槽和复用槽全部清零；
另一个独立 DRM fd 无法使用本 fd 的句柄；超界 READ/COPY 被拒绝；无效 syncobj 被拒绝；
这些拒绝没有增加 GPU 提交数。显式 GEM_CLOSE 及子进程直接退出均回收全部租用槽位。

GEM 对象各自拥有 reservation。源对象增加 READ fence，目标增加 WRITE fence，
二者和用户指定的 binary syncobj 都引用原驱动实际生成的 dma_fence。对象引用保护
同一任务中的句柄并发关闭；GPU pin 和原完成事件处理保护已提交的 BO 生命周期。

当前所有 GPU 任务串行化，COPY ioctl 同步等待最多 5 秒。根封存后不能卸载该模块，
因为独立的硬件根撤销协议仍未实现；模块自持引用保证根、上下文、BO 元数据和切片存活。
任务失败后不自动重置；暂不提供 mmap、PRIME、裸 GPU VA 或原始命令流。
打开及私有 ioctl 都要求 CAP_SYS_RAWIO。

## 独立页表核验

只读 helper `mt_drm_snapshot` 从原对象已有映射复制根页表和绑定元数据，不写硬件。
离线完整遍历结果：

- 18 个页表页、3461 个有效叶页，全部 PTE 默认可写标志 1。
- 11 个私有绑定共 132 页，每页物理地址与实际 BO 后备逐页一致。
- 9 个共享绑定共 3329 页，与旧 r28 上下文对应 PTE 完全一致。
- 原 r28 的完整 128 KiB 页表逐字节未变。
- 新根 SHA-256：`c6bc810bf884ca78af0c984be65d306182d24ac0f2d9ac40a3cf5ba21b53ab74`。

快照 helper 已卸载。实际 DRM 模块 SHA-256 为
`4f3db3739a695dd876483e900314b89e4a9f78f83a73746b394f895c372de484`。

原主模块 sysfs 的 `drm_registered=0` 是旧代码里的固定输出，`gem_objects=0` 只统计
原私有 GEM store；两者未覆盖这个新增前端。当前是否有 DRM 应看实际 sysfs 节点、
DRM VERSION(`mtvgpu`) 和本前端 QUERY。`render_ready=0` 仍准确表示原栈尚无图形渲染。

## 代码与复核

新增 `include/mt_drm_uapi.h`、`kernel/recovery/mt_live_drm.c`、
`userspace/mt-drm-check.c` 和只读快照 helper。安装 libdrm-dev/pkg-config 作为构建依赖。
模块 W=1、用户程序 C11/Wall/Wextra/Werror 构建通过；没有更改系统显示驱动配置。

`build/r31-live/` 保存实际模块/测试程序、逐次 JSONL 结果、前后运行态、新旧根快照和
旧上下文文件复制结果。`reports/r31-drm-validation.json` 保存完整核验。
离线重验：`sudo python3 scripts/verify-r31-drm.py`；sudo 只用于读取 root 创建的证据文件。

下一步仍需把图形命令和配套用户态接起来，并完善缓冲对象规模、异步调度、共享映射与
页表生命周期。不能把本轮复制与 DRM 节点当作完整显卡适配已完成。
