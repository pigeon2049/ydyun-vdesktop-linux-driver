# Guest 协议初始化实验

**当前真机状态（r32）**：`mt_live_graphics` 提供原生 32 位矩形填充，62 次 GPU 填充和 11 次复制全部通过；累计 completed=231、pending=0。当前新 render 节点为 `renderD129`，原模块及上下文保留。见 [原生填充记录](../reports/r32-native-fill.md)。仍无 OpenGL/Vulkan 或桌面显示接入。

**前一阶段真机状态（r31）**：`mt_live_drm` 注册 `mtvgpu` render 节点，GEM 直接显存复制与
原生 syncobj 实测通过，累计 completed=158、pending=0。见 [DRM 真机记录](../reports/r31-drm-gem-syncobj.md)。

`mt_guest_probe.c` 是针对本机 PCI `0000:00:0e.0` / `1ed5:0222:1ed5:1101`
编写的实验模块。已实测宿主设备信息查询、四页共享内存注册、消息环往返及版本协商。
当前已通过保留上下文完成真实 TQX 复制和 Linux fence 往返，并可经实验 bridge
提供 root 用户态复制接口。r31 新增独立 DRM/GEM/syncobj 前端并验证直接显存复制；
r32 已验证原生矩形填充，尚无桌面显示或 3D 接入，不能当作成品驱动安装。

**前一阶段真机状态（r30）**：用户态 bridge 完成 50 个任务，累计 completed=132、pending=0，
固件正常。见 [用户态验证](../reports/r30-userspace-copy.md) 与 [工具用法](../userspace/README.md)。
以下条目为较早阶段记录，其未执行 GPU 的表述不代表当前状态。

**最新 VM 销毁检查**：VM 最终释放前会预检页表与全部映射 BO 的引用/pin 账目；异常时不做部分释放。
纯 RAM fence 322 项和 GEM/BO 280 项回归通过，自测模块已卸载。详见
[VM 销毁回归](../reports/vm-teardown-atomicity.md)。真实硬件提交仍关闭。

**最新上下文切片**：`mt_boot_pool_alloc` 为类型3/4/6提供池内分配，切片持有BO和上下文，
任务期间禁止释放。TQX支持保留区地址及共享静态PDS，非零池偏移RAM上传通过。
见 [原始回调、堆表纠正与验证](../reports/context-pool-slices.md)，仍未执行真实GPU任务。

**最新动态池接入**：`mt_reserved_pools.h` 增加三份独立后备，生产启动BO缓存现有九项，
通过一个原子批次加入VM。18项TQX组合映射使用21页页表并持有19个BO，257项内核RAM
与321项fence检查通过；未发布硬件上下文。见 [动态池集成](../reports/reserved-pools-integration.md)。

**最新静态程序接入**：`mt_static_programs.h` 将PDS/USC两个定义前缀加入启动CPU暂存，
总计20,272字节；15组集成检查、W=1编译及共享ABI通过。未改变硬件上传列表。
另外执行原始分配器，确认动态USC池与静态USC后备是不同对象；三个动态池尚未接入VM。
见 [原始证据、验证与接入边界](../reports/static-programs-path.md)。

**最新系统RAM接入**：[Paging Command真实后备与逐页地址转换](../reports/system-memory-path.md)
已替换此前临时BAR后备，接入非连续系统页、BO生命周期及VM页表。
3,132页原始对照、14组集成检查、235项内核RAM和321项fence检查通过，仍无GPU执行。

**最新映射纠正**：[4 MiB Paging Command创建与映射](../reports/paging-command-correction.md)
已替换此前误用的8 KiB Paging Context，新增独立分配并保持共享ABI。
16组原始路径、13组集成回归和227项真实内核RAM检查通过；仍未验证GPU执行。

**最新启动 BO 桥接**：`mt_boot_bo.h` 和 `bind_boot_shared` 将现有启动分配接入
共享引用；借用不清零或释放原块，关闭VM后仍可由GPU引用持有，空闲缓存不阻止卸载。
222项内核RAM检查通过，见 [生命周期与准备状态边界](../reports/boot-bo-lifetime.md)。

**最新进程映射**：`mt_process_resources.h`/VM `bind_shared` 原子加入固定共享组，
复用新批量绑定和整VM任务引用。166项内核GEM/RAM检查通过，TQX准备持有16个BO。
后续已接启动BO视图，见 [共享资源证据及限制](../reports/process-shared-resources.md)。

**最新初始化证据**：普通状态分配的 `InitContextResource` 原始路径只记录CPU映射，
不写状态模板；8组完整路径及错误通知验证通过。不能据此推断OS初始内容或GPU执行。
见 [状态初始化追踪](../reports/tqx-state-init-path.md)。

**最新任务接入**：GEM `prepare_tqx_work` 在同一锁内上传资源与页表并取得长期引用；
`submit_tqx_work` 将所有权移入 pending fence，队列失败可重试，完成后回收。
321项真实内核RAM fence检查通过；生产工作准入仍关闭，未执行GPU任务。
见 [任务引用与验证边界](../reports/tqx-work-lifetime.md)。

**最新上传接入**：`mt_tqx_submission.h`/GEM `upload_tqx_submission`检查九个对象，
上传并读回11页程序与DMA；独立引擎状态保持原内容。138项内核RAM检查通过。
尚无异步提交引用或GPU发布，详见 [完整资源上传](../reports/tqx-submission-upload.md)。

**最新拓扑接入**：`mt_tqx_topology.h` 从已校验的 Guest 信息页读取核心数，
主模块在查询成功后安装到 GEM；DMA输入核心数0可使用查询值，冲突参数拒绝处理。
原始传递路径、126项内核RAM检查及W=1/ABI对照通过，未加载生产主模块。
见 [核心数来源与接入](../reports/tqx-topology-path.md)。

**最新提交编码**：GEM `encode_tqx_dma` 将新完成的 TQX 软件记录转换为
`0x67` DMA 描述符及提交视图；`mt_tqx_engine_state.h` 核对独立状态区大小和地址。
144 组序列化对照、64 组原始分配参数对照及119项内核 RAM 检查通过。
状态内容、拓扑、新 BO 映射/上传和实际任务发布尚待完成。
见 [DMA 与引擎状态证据](../reports/tqx-dma-path.md)。

**最新 BO 上传接入**：[TQX 九页写入与读回](../reports/tqx-upload-path.md) 已加入 GEM 内部接口，
覆盖五类资源、非零 BO 偏移、填充与失败回收。144 组页面对照、45 个 I/O/map
故障注入及 114 项内核 RAM 检查通过；生产 BAR 路径尚未实测，未获得 GPU 加速。

**最新 TQX 接入**：`mt_tqx_heap.h` 从实际 VA 计算各堆偏移和纹理索引，
GEM `prepare_tqx_stream` 核对七对象句柄、VM 与物理别名，生成完整 CPU 命令镜像。
144 组原始路径对照、100 项内核 RAM 检查通过，W=1 构建无警告且 ABI 保持。
尚未上传程序/状态或执行 GPU 工作。见 [专用堆与 VM](../reports/tqx-heap-path.md)。
以下各组件记录包含此前阶段的历史限制。

**当前平台选择**：本机参考库 family=2、CE=0、transfer=1、compute=1。
`mt_device_profile.h` 为主模块/GEM/上下文/提交路径增加 CE 拒绝检查；
`mt_tqx_copy.h` 与 GEM `prepare_tqx_copy` 实现本机 TQX 的 CPU 拆块、surface/state
和操作选择，432 组原始指令对照通过。此计划不是完整 GPU 命令流，工作使能仍关闭。
主实验模块未加载。见 [平台证据](../reports/device-profile-path.md) 和
[TQX 适配记录](../reports/tqx-copy-path.md)。

**TQX 程序库与参数**：`mt_tqx_program.h` 和可重复提取的程序数据已加入主模块，
GEM `prepare_tqx_programs` 只生成 CPU 镜像。复制常量及 PDS 状态的 192 组原始
job 对照通过；shader/PDS 需要分别分配到专用 GPU 堆，该步骤尚未接入。
见 [程序模板与地址域](../reports/tqx-program-path.md)。

**TQX 源描述符**：`mt_tqx_texture.h` 已接入复制计划，为每块生成 32 字节纹理、
16 字节默认 sampler 和 16 字节零填充。490 组编码、432 组拆块、192 组 job 对照
通过，仅启用本机 family2 的内部暂存。见 [编码证据](../reports/tqx-texture-path.md)。

**TQX 目标命令与组合 job**：`mt_tqx_destination.h` 编码 80 字节命令，
`mt_tqx_job.h` 形成 240 字节 CPU 镜像；490 组目标、192 组组合 job 原始指令
对照及内核 RAM 检查通过。专用堆分配、初始状态、收尾和提交尚未接入。
见 [目标发射证据](../reports/tqx-destination-path.md)。

**TQX 单页初始化与收尾**：`mt_tqx_stream.h` 增加初始 PDS、结束控制字和
软件状态/页记录，190 组真实构造至收尾对照通过；69 项内核 RAM 检查通过。
该组件没有专用堆/VM 分配及提交入口。见 [单页编码证据](../reports/tqx-stream-path.md)。

**TQX 多拆块命令段**：`mt_tqx_copy_stream.h` 组合 1～4 个 job，复用初始状态，
生成连续命令、软件/页记录和根地址导出。432 组完整原始路径、25 个拒绝案例
与 76 项内核 RAM 检查通过。真实堆/VM 和提交尚未接入，见
[完整单区域编码](../reports/tqx-copy-stream-path.md)。

**新增运行模式（已构建，未加载）**：`runtime_context=1` 配合固件准备参数生成
持久上下文；再配合 `trial_connect=1` 则仅在连接成功后发布并保持会话，不再自动断开。
默认仍为 0。新只读 runtime 属性与内存持有规则见
[运行模式说明与验证](../reports/runtime-context-integration.md)。当前会话未被替换。

**内存对象后备层**：`mt_bo.h` 与 `mt_bo_vram.h` 已接入普通显存池和会话锁，提供
页对齐分配、清零、CPU/GPU 使用引用及最后引用释放；新增只读 buffers 统计。
尚无 GEM 创建 ioctl 或用户态 mmap，默认不会分配 BO。详见
[对象生命周期与测试](../reports/buffer-object-layer.md)。

**GPU 地址空间层**：`mt_gpu_vm.h` / `mt_vm_vram.h` 将 BO 接入已还原的三级页表，
提供创建、精确范围绑定/解除绑定、上传读回和发布前封存接口。变更先在临时缓冲区
完成，失败保留旧映射；每个绑定持有 BO 引用。当前只允许编辑未发布地址空间，
没有在线 TLB 失效或上下文撤回协议，也未自动创建或上传 VM。详见
[映射实现与验证](../reports/gpu-vm-mapping.md)。

**GEM 内部桥接**：`mt_gem.h` 提供私有 GEM 对象、文件句柄创建和句柄到 VM 的绑定。
BO 容器与 GEM 包装独立存活，最后一个 VM/CPU/GPU 引用负责销毁 BO。新模块新增
DRM 符号依赖，仍未注册 DRM 设备、添加 ioctl 或开放用户 mmap/dma-buf。
真实 Linux GEM 核心配合 RAM 后备的临时模块已运行并卸载，最新 76 项检查通过；详见
[GEM 对象桥接](../reports/gem-object-bridge.md)。最新现场主实验模块未加载，以下
通信恢复模式加载状态是此前记录。

**完成事件和 fence**：`mt_fw_event.h` / `mt_marker_fence.h` 还原 24 字节事件分派、
普通完成的最早待完成编号匹配和 NULL 描述符空提交包。运行态 poll 已接事件消费者，
每 DM 的空提交待完成队列持有 dma_fence，失败不释放可能已被 GPU 引用的提交。
completions 为只读统计；submit_enabled 默认且目前始终为 0，尚无提交 ioctl。
221 项真实 dma_fence/RAM 队列检查通过，临时自测模块已卸载；详见
[事件与 fence 实现](../reports/event-fence-path.md)。

**工作包准备**：`mt_work_command.h` 实现完整 80 字节序列化和逻辑节点路由，
GEM 的 prepare_work 内部接口根据句柄与 VM 检查命令范围，页表根取自该 VM 自身。
这是 CPU 暂存接口，不解析命令流、不持有发布后的任务资源，也不调用提交队列。
见 [字段来源与验证](../reports/work-command-path.md)。

**复制分页暂存**：`mt_ce_paging.h` 连接 CE3 命令段、软件记录导出与 Windows 分页
描述符/状态槽转换；`prepare_copy_paging` 验证三个命令组件和源/目标的真实映射与
后备别名。192 组转换、128 组完整原始序列对照及 38 项内核 RAM 检查通过。
没有自动启用 CE3 或实际提交，详见 [转换路径与限制](../reports/ce-paging-path.md)。

**2026-09-28 此前通信恢复状态（历史记录）**：Guest 重启后已用新增的 `recover_channels=1` 模式恢复
四页共享通道及主模块内置 IRQ/RPC 服务，当前保持绑定，无自持引用。
该模式要求 Guest1/FW1，与固件上传/连接参数互斥；可选 `snapshot_memory=1` 只读快照。
一次 `refresh_osid=1` 实测未推动固件启动。具体参数、生命周期和证据见
[本机恢复记录](../reports/local-recovery.md)。没有硬件加速，未加入开机加载。
后续已加入只读 `publication` / `retained_status`，以及仅针对原五条保留命令的一次性
`retained_control` 通知入口；实测未使固件推进，见 [本轮记录](../reports/retained-kick-audit.md)。

**2026-09-28 00:37 历史状态**：后续 `trial_connect` 实验连接超时且断开未确认，
模块仍绑定、自持引用并保留通信页。共享 IRQ10 在一次恢复后再次被禁用，
后续通过 [持续通信辅助模块](recovery/README.md) 补上协议确认，已恢复并持续处理宿主查询。
下面的加载/卸载步骤属于先前基础探测流程，不能直接用于清理本次保留状态，
尤其不可强制卸载。详见 [当前进度](../reports/progress-2026-09-28.md)。

**PCI 启用修正**：新源码在 `trial_connect=1` 的干净连接路径中，按 Windows
`140003af4` 开启并回读 BusMaster；失败清理或确认断开后关闭。当前恢复模式不变，
新主模块只编译未替换。独立有界试验单开 BusMaster、再联合既有队列通知，均未使
固件前进，配置已恢复。见 [PCI 核查与实测](../reports/pci-master-audit.md)。

构建：

```sh
cd /opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest
make -C probe
make -C kernel
sudo build/probe/mt-status --read-status
```

临时测试命令（这些命令会注册共享页并写入已还原的 BAR1 寄存器）：

```sh
sudo insmod kernel/mt_guest_probe.ko enable_probe=1 query_info=1 probe_rpc=1
cat /sys/bus/pci/devices/0000:00:0e.0/mt_guest/connection
sudo cat /sys/bus/pci/devices/0000:00:0e.0/mt_guest/info_raw > reports/device-info-current.bin
sudo cat /sys/bus/pci/devices/0000:00:0e.0/mt_guest/channels_raw > reports/shared-channels-current.bin
sudo rmmod mt_guest_probe
sudo build/probe/mt-status --read-status
python3 scripts/decode-device-info.py reports/device-info-current.bin
```

已经保存首轮原始响应，不需要为查看结果重复加载：
`reports/device-info.json`、`reports/shared-channel-status.txt`。
`host_version` 是协议回复字段名，不表示已核实宿主安装包版本；bit 56 置位表示兼容性接受。
`render_ready=0` 明确表示没有实现渲染，不能因 PCI 已绑定就宣称加速成功。

`enable_probe` 默认关闭；`query_info`、`probe_rpc` 分别开启设备信息与共享通道查询。
模块没有导出自动加载 alias，未配置 DKMS、initramfs 或系统启动加载。
信息请求使用内核分配页的客户物理地址；宿主协议进行同步复制，不能把它替换成 IOMMU IOVA。
共享页在探测结束时按 Windows 撤销序列逆序注销后释放，sysfs 只保留快照。
状态读取用 32 位访问，自定义寄存器用 64 位访问。上述基础探测不启用 BusMaster、
MSI 或固件连接；后述 `trial_connect=1` 路径会开启 BusMaster。

本机实验期间 QXL、SDDM、SPICE、Tailscale 保持正常。加载此未签名的外部模块后，
内核 taint 为 12288；即使卸载也会保留标记。本实验不需要重启。

后续实现应继续补齐持续通信、固件页表和队列、中断、DRM 与匹配用户态，
不能通过设置状态寄存器或创建空 render 节点代替真正的 GPU 初始化。

新增 `snapshot_memory=1` 可配合 `query_info=1` 获取只读内存基线，通过
`mt_guest/memory_raw` 导出 1 MiB 固件区开头和 64 KiB 共享区开头，读取后卸载模块。
`memory_result=0` 只表示快照读取成功。地址由当次设备信息解析，越界或状态不符会拒绝读取。

`mt_mmu.h` 的候选页表生成和原始指令对照测试：

```sh
python3 scripts/verify-mmu.py
python3 scripts/extract-firmware.py
python3 -m unittest discover -s tests -v
```

以上脚本从 `mt-vgpu-guest` 目录运行；需要系统 `python3-unicorn` 包（已安装）。
生成文件留在本地，不会自动写入显存。细节见 [固件与页表记录](../FIRMWARE-NOTES.md)。

新增 `reserve_memory=1`（需要 `query_info=1`）执行 BAR2 保留、私有池创建和分配：
8 MiB 固件区、24 KiB 页表区、4 KiB 辅助区。地址来自当次 Host 响应；先验证范围、
分段重叠、40 位 GPU 地址和已研究的 Host PB 配置，再创建三个独立 `gen_pool`。
分配成功后保持 CPU 映射直到模块卸载，退出或失败时释放全部映射、分配与 BAR2。
此阶段没有安装 GPU MMU 映射，也不向 Host 提交这些地址。

```sh
python3 scripts/verify-memory-layout.py
sudo insmod kernel/mt_guest_probe.ko enable_probe=1 query_info=1 reserve_memory=1
cat /sys/bus/pci/devices/0000:00:0e.0/mt_guest/vram
sudo rmmod mt_guest_probe
```

已经实测上述流程，并检验分配耗尽和释放后复用。`vram_samples` 为 root 可读的
8 KiB 快照，分别包含固件/页表分配的第一页；它不是完整显存备份。

`test_memory_write=1` 是单独、默认关闭的写入验证选项，必须搭配 `reserve_memory=1`。
它备份辅助页，写入并读回固定图案，再恢复原数据并逐字节检查。任何写入之后的失败路径
均先尝试恢复；如果恢复校验失败，日志会明确记录并拒绝保留本次初始化。
本机已经通过一次测试，地址为 BAR2 `0xa06000` / GPU PA `0x605806000`。
`vram` 中的 `device_memory_written`、`pattern_verified`、`restore_verified` 分别报告
是否实际写过、图案是否正确、恢复是否正确。它不测试 GPU 执行能力。

显存分配列表目前由 probe/remove 串行访问；接入运行时并发调用前还需加入外层生命周期锁。

固件队列实现位于 `mt_fw_queue.h` / `mt_fw_queue_io.h`：六个 DM，每个包含两条
80 字节命令环和一条 24 字节事件环，均为 64 项/63 项可用。提交路径使用每 DM
生产者自旋锁，按“命令内容 → 内存屏障 → head → 屏障/读回 → BAR0+b00”顺序发布。
满环立即返回 `-EAGAIN`，后续连接层需要在锁外作有期限的重试；异常计数返回 `-EIO`。
当前 probe 只绑定 CPU I/O 映射并读取各 DM 的排空快照，**没有调用提交路径**。

`reserve_memory=1` 的 `vram` 输出新增六行 `queue_dm`；`idle_snapshot=1` 只表示
读取时三个环的 head/tail 相同，不证明固件连接完成或 GPU 执行过命令。
`initialized=0 submitted=0` 明确区分本轮只读检查。内核编译和实机映射读取均已通过，
卸载后 Guest=0 / FW=1，服务保持正常。

```sh
python3 scripts/verify-fw-queue.py
```

该脚本执行 Windows 原始指令，对照队列全区内容、命令构造、MMIO 参数和边界行为。
锁/进程 ID/延时用单线程模型，MMIO helper 只记录参数，不接触硬件。
它验证代码和发布调用顺序，不能代替实际 PCI 写入顺序与固件响应的验证。

`prepare_resources=1` 需要 `reserve_memory=1`，新增八项显存保留：
三页默认映射、PDS/USC 各 1 MiB、YUV/DM Kill 各 512 KiB、fence 4 KiB、
paging context 8 KiB、Host PB 2 MiB。USC 的 GPU VA 仍按参考路径保留为 deferred(0)。
这些保留只建立 CPU I/O 映射，不清零、不改写现有设备内容。

`mt_boot_resources.h` 使用实际分配地址在普通 CPU 内存中构造固件页表、
默认映射和两份静态资源初始化镜像。成功时 `bootstrap` 显示地址、尺寸和
`uploaded=0 root_published=0`。root 只映射参考 `1400159cc` 已确认的固件区域；
其他资源属于后续上下文映射，不能直接全部塞进固件根页表。

```sh
python3 scripts/verify-mmu-bootstrap.py
python3 scripts/verify-static-resources.py
sudo insmod kernel/mt_guest_probe.ko enable_probe=1 query_info=1 reserve_memory=1 prepare_resources=1
cat /sys/bus/pci/devices/0000:00:0e.0/mt_guest/bootstrap
sudo cat /sys/bus/pci/devices/0000:00:0e.0/mt_guest/bootstrap_raw > build/firmware/bootstrap-stage-live.bin
sudo rmmod mt_guest_probe
```

CPU 数据总长 `0x109000`：`0..0x6000` 固件页表，`0x6000..0x9000` 默认映射，
`0x9000..0x89000` YUV，余下 `0x80000` 字节 DM Kill。sysfs 原始数据只允许 root 读取。
成功准备不代表资源已安装或固件已连接；尚不包含 Guest 的 4 MiB 系统内存 paging command
缓冲区、每上下文映射和 TLB 管理。退出和失败路径释放新增显存保留与 CPU 数据。

`load_firmware=1` 要求 `prepare_resources=1`，通过 firmware API 加载并验证固定镜像，
然后在 CPU 内存中应用 Guest 状态。镜像尺寸或 SHA-256 不匹配会拒绝绑定。
本机已安装 `MT_FW_LOADER_NAME` 对应文件，未安装模块或自动加载配置。

```sh
python3 scripts/verify-fw-connection.py
sudo install -D -m 644 build/firmware/guest-loader-stage.bin /lib/firmware/mt-vgpu-guest/gen1-guest-loader.bin
sudo insmod kernel/mt_guest_probe.ko enable_probe=1 query_info=1 reserve_memory=1 prepare_resources=1 load_firmware=1
cat /sys/bus/pci/devices/0000:00:0e.0/mt_guest/firmware
sudo rmmod mt_guest_probe
```

单独追加 `test_firmware_upload=1` 会在未发布的 8 MiB 固件分配区执行整块备份、
上传、读回和原数据恢复；本机已成功验证。`firmware` 的 `written/verified/restored`
分别记录是否写过、镜像是否匹配、原内容是否恢复。`firmware_image` 和 `firmware_backup`
为 root 可读原始数据。恢复失败时 probe 保留映射和备份供检查，不能把该结果当成正常卸载条件。
`vram` 的 `device_memory_written` 汇总辅助页或固件写入；其 `pattern_verified` /
`restore_verified` 仍仅描述旧辅助页测试。固件结果以 `firmware` 属性为准。

`trial_connect=1` 已接入地址发布、online 和 `0x46/0x47` 连接状态机。
首次实测超时且断开未确认，当前旧主模块仍自持引用，保留通信页、上传资源和备份。
不能将前面各次独立、已恢复测试的卸载命令直接用于当前保留会话。

最新源码通过 `mt_rpc_service.h` 在共享页注册前安装 shared IRQ handler，
通过 `mt_rpc_transport.h` 确认中断并回复已解析的宿主统计查询。
初始化及资源操作由 trial mutex 串行；worker 使用 trylock，防止取消工作时互等。
连接/断开持锁等待时同步 poll 查询；退出先撤销 Host 页面注册，再同步停止 IRQ/work，
最后释放页面。资源结构 ABI 保持不变，服务对象放在外层分配中。
新主模块的 `mt_guest/rpc_service` 显示处理计数；当前旧模块没有此属性。
源码已编译，但首次完整启动的生命周期尚未实机验证。当前辅助模块仍维持旧会话。

新增测试 `python3 scripts/verify-rpc-transport.py` 覆盖游标回绕、输出满、非法请求和
原始指令中断确认，报告明确区分 RAM 模型与实机验证范围。

完整 trial 的启动顺序现为：安装 IRQ → 注册四页/消息模式 → 查询 Host mode →
提交参考包标识 → 协商版本 → 查询设备信息 → 提交 BAR2/BAR4 基址和共享区描述 →
保留/上传资源 → 发布固件地址 → 连接。`trial_connect=1` 明确要求 `query_info=1`。
模式/版本路径由 `verify-package-announcement.py` 对照原始指令；新版完整顺序尚未实机启动。

下一次启动试验可使用 `python3 scripts/fresh-trial.py` 做默认只读预检。
该脚本位于项目根目录的 scripts 下，要求从上述已编译、已记录散列的模块开始。
当前会话会被拒绝，不会因看到旧模块而自动卸载或解绑。
需要宿主确认旧 vGPU 会话已释放并重新创建后，才有条件执行
`python3 scripts/fresh-trial.py --run`。这会上传并尝试连接/断开，结果和备份保存在
`build/fresh-trials/<时间-标识>/`；仍可能失败并保留资源，不代表渲染驱动已完成。
具体前置条件与代码依据见 [启动前置条件核查](../reports/startup-prerequisites-audit.md)。

**工作资源生命周期**：新增 `mt_work_job.h`，完整工作包通过内部 submit_work 可持有
页表与全部映射 BO；VM 活跃保护、队列拒绝回滚和匹配完成回收已接入。任务完成不会
撤回已封存根页表。主模块的 work_ready 始终为 false，只在 RAM 测试中启用。
详见 [任务资源与验证](../reports/work-resource-lifetime.md)。

**软件任务上下文**：`mt_execution_context.h` 提供进程 token、VM 所有权与节点上下文，
内部 submit_context 根据所属上下文构造身份和 DM，任务完成前禁止销毁上下文。
不是固件上下文注册接口；主模块保持未加载，216 项内核 RAM 检查通过。
详见 [字段与接入验证](../reports/execution-context-path.md)。

**线性复制载荷**：`mt_ce_copy.h` 重现 CE1/2/3 的单条 40 字节编码，GEM prepare_copy
验证源/目标文件对象和实际 VM 映射，拒绝后备重叠。192 个原始指令案例与 24 项内核
GEM/RAM 检查通过。未实现完整流起止和同步，不可将该载荷直接当作可执行缓冲区。
见 [复制载荷和后续调用链](../reports/ce-copy-path.md)。

**CE3 命令段**：`mt_ce_stream.h` 和 GEM prepare_copy_stream 提供单条复制的初始化、
同步、结束及软件记录暂存，检查命令/源/目标三个映射和后备重叠。128 组原始序列对照
与 29 项内核 GEM/RAM 检查通过。不是已完成的固件 DMA 提交格式，不开启 GPU 提交。
见 [限定布局与验证](../reports/ce-stream-path.md)。
