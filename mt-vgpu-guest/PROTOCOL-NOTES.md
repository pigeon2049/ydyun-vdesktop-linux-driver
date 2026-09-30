# Windows Guest 协议逆向进度

参考二进制：`/opt/MTT-driver-only/mtkm64.sys`，版本 30.0.2505.1。
本记录中的地址为 PE 首选虚拟地址；依据保存在 `decompiled/mtkm64.sys/`。
包含静态还原与本机实测结果。宿主共享通道已验证；固件连接和图形渲染尚未实现。

## 当前恢复试验

2026-09-28 Guest 重启后已在 Guest1/FW1 下重新建立共享通道，主模块保持 IRQ/RPC
服务。BAR1+0x110 提交当前 OSID=4 后重新查询的步骤已实测，设备信息和固件快照均未变化；
没有证据表明它能重置当前会话。固件仍未执行连接命令，详见 [恢复记录](reports/local-recovery.md)。

## 早期基础探测的已验证结果

`probe/mt-status.c` 对 BAR0 做两个对齐 DWORD 读取：Guest 状态为 0，固件状态为 1（READY）。
`kernel/mt_guest_probe.c` 在本机 6.12.107 内核编译、加载、绑定、查询、卸载成功：

- BAR1 `0xc8` 返回签名 `aa557491`、信息结构版本 **2**、OSID **4**。
- 显存容量字段为 **1 GiB**；BAR2 实际内存字段为 **1072 MiB**，不同于 16 GiB PCI 地址窗口。
- 返回 6 个分段。按 `140027ab4` 推导，固件分段的 BAR2 偏移为 `0x3f000000`，长度 64 MiB；
  标志 `0x20` 的共享分段偏移为 `0x43000000`，长度 2 MiB。
- 4 个 4 KiB 共享页注册成功；消息环查询返回 mode=1，错误码 0。
- 版本查询返回 `0x0105000500070002`，字段为 2.7.5 / revision 5、bit 56 为 1。
  `14002680c` 对 bit 56 置位的回复直接接受，所以本机通过参考协议的兼容性检查。
  **这是协商回复，不足以独立证明宿主安装包的精确版本**。
- 临时通道均已撤销、模块已卸载；复读状态仍为 Guest=0、固件=READY。

原始证据：`reports/device-info.bin`、`device-info.json`、`shared-channels.bin`、
`shared-channel-status.txt`、`register-status-after-rpc.json`。
内核 taint 从 0 变为 12288（外部、未签名模块标记），卸载后仍保留，未发生内核崩溃。

## 已确认的入口

| 地址 | 证据 / 行为 |
| --- | --- |
| `140003af4` | PCI 厂商 1ed5，遍历能力链，检查能力 ID aa、标记 aaaa；与本机配置空间吻合 |
| `140025b88` | 按 device ID 高字节选择平台初始化；本机 0222 进入 `14002d73c` |
| `14002d73c` | Guest 分支选择显存 BAR2、寄存器 BAR0、自定义 BAR1；两个寄存器窗口配置为 64 KiB |
| `14002fe44` | 解析 PCI 资源，保存自定义寄存器物理地址和长度 |
| `140026324` | 映射自定义寄存器，注册共享缓冲区，再刷新设备信息 |
| `140027780` → `140007d84` | 自定义寄存器写操作；汇编证实是 **64 位**存储，随后 `lock or [rsp],0` 屏障 |
| `1400275dc` → `140007d70` | 自定义寄存器读操作；需保留 64 位访问语义 |
| `140026b30` | 清零设备信息缓冲区、提交客户物理地址与 BAR 地址，读取宿主填充的字段 |
| `1400168b4` | `NotifyFirmwareConnect`：通知宿主，等待 READY，发送连接命令，再等待 ACTIVE |
| `140016b4c` | `NotifyFirmwareDisconnect`，尚未还原完整清理前置条件 |

注意：Ghidra 对短包装函数会遗漏通过寄存器传递的参数，例如把
`140027780` 显示成单参数函数。必须结合调用点与汇编校对，不能直接编译伪 C。

## BAR1 自定义寄存器

以下由 Windows 调用点还原；`0xc8`、`0x138`、`0x150`、`0x158` 已做本机验证。

| 偏移 | 宽度 | 观察到的操作 |
| --- | --- | --- |
| `0x20` | 64 位 | 提交 BAR2 物理基地址，见 `140026b30` |
| `0x28` | 64 位 | 提交本地 MMU BAR4 基地址（上下文字段 `+0x518`）；本机缺失，已验证应提交 0 |
| `0xc8` | 64 位 | 提交设备信息缓冲区的客户物理地址 |
| `0x110` | 64 位 | 写设备信息 `+8` 字段，随后刷新信息；旧头文件把该字段称为 OSID，当前语义待验证 |
| `0x138` | 64 位 | 提交通信环索引，宿主处理共享消息；本机验证环 0 请求、环 3 回复 |
| `0x148` | 64 位 | `14002249c` 写 1，连接流程中用于通知宿主 |
| `0x150` | 64 位 | `140025ed0` 提交共享缓冲区客户物理地址 |
| `0x158` | 64 位 | 写注册描述值，再读返回值；返回非零进入失败处理 |

`140025ed0` 注册四个缓冲区，描述值来自回调表 `141106500`。汇编恢复回调后确认：
长度在 bits 40:9，槽号在 bits 8:1，bit 0 为有效位；本例每页 4096 字节。
描述符为 `(4096 << 9) | (slot << 1) | 1`；撤销为 `slot << 1`。
Windows 撤销函数清除有效位后同步释放对应页，本机实现按槽 3→0 撤销。
回调表每行三个 64 位指针：

```
0: 140024ee4 140024f14 140024f24
1: 140024ee4 140024f38 140024f30
2: 140024ee4 1400250c4 1400250e4
3: 140024ee4 1400250c4 1400250e4
```

其中部分地址在现有 Ghidra 函数索引中没有独立条目，已用完整汇编补齐关键回调。
批处理成功统计覆盖工具识别出的函数，不代表每个代码入口都已正确识别。

## 固件状态与连接

S3000 初始化从 `141106e30` 复制 12 个 DWORD 到外层上下文 `+0x1df0`：

```
24f8 e8 e0 160 46c8 be0 b00 800 880 888 890 898
```

`140023afc` 根据操作码访问其中的寄存器偏移：

| 操作码 | 偏移 | 访问 |
| --- | --- | --- |
| 1 | `0x898` | 读固件状态 DWORD |
| 2 / 3 | `0x890` | 读 / 写 Guest 驱动状态 DWORD |
| 4 | `0x888` | 读 DWORD，语义待确认 |
| 5 / 6 | `0x880` | 读 / 写 DWORD，语义待确认 |

读写通过 `1400275a4` / `140027740` 使用平台寄存器映射数组。
`1400254dc` 和 Guest 平台配置确认本机窗口偏移为 0；已据此完成 BAR0 状态读取。
`140014900` 的日志明确有效组合为 READY=1/1 或 ACTIVE=2/2。
连接函数写 Guest 状态 1，发送命令 `0x46`，等待固件状态 2 **且共享字段
bFWStarted 非零**，然后写 Guest 状态 2；固件状态 4 表示拒绝连接。
等待间隔 25 ms、上限 400 次。只改状态寄存器不能替代命令队列初始化。

## 当前设备信息结构与旧 Host 头文件不同

当前 Windows 代码分配 / 复制 `0xcc8` 字节设备信息；`140026b30`：

- `+0x10` 为标志字段；`+0xc48` 在提交前置位低两位。
- `+0xc50` 为分段数量；分段表从 `+0x28` 开始，步长 `0x18`。
- 每段前两个 QWORD 参与地址和长度处理，`+0x10` 位置的标志用于选择内存用途。

旧官方 2.3.0 Host 头文件使用另一种 segment_info 布局，不能直接用于解析当前数据。
本机返回 version=2，符合当前 Windows 布局，不是旧 Host 的 version=1。

## 消息环格式

### 2026-09-28 持续接收与中断确认实测

`140023810` 的 Guest 确认路径把共享页 0 `+4` DWORD 写成 2，随后 `+8` QWORD 加 1。
旧 Host 的 `mtgpu_vgpu_ack_irq` 同样写共享区 `+4=2`；其触发中断路径先置 `+4=1`。
本机第一次固件试验缺少该处理，导致共享 IRQ10 被内核禁用。
只设置 PCI COMMAND.INTX_DISABLE 并重新 enable_irq 仍会复发。
补上共享页确认、共享 IRQ handler 后，实测状态重新回到 0，IRQ10 持续启用。

Host 主动请求走环 1，Guest 回复走环 2，回复提交用 BAR1 `+0x138` 写 2。
`14002bd70` 负责复制 type/subtype，`14002bf90` 设置 operation=2。
`14002729c` 对 type=1 的查询：subtype=1 返回上下文 `+0x1d98` 的分配用量，
subtype=0 返回 `+0x1da0` 的低 32 位 GPU 利用率统计。
前者由 `140011510` 增加、`1411cb4d8` 减少，再经 `14000ee8c → 140023880` 更新；
后者来自 Escape `0x7a` 的 `EscapeSetGpuUtilStat → 140010078 → 14002363c`。
这些消息的输入 value/status/padding 可能是未初始化数据，不能当地址使用。

补充核对 Windows 回调：它还接受 type=1/subtype=2，返回 `EscapeSetGpuUtilStat`
写入的第二个 QWORD（adapter `+0x1da8`）；该值为零时退回 adapter `+0x1d90`，并在
`adapter[+0x1cc8]+0x38` 标记当前取值来源。Linux helper 现在接受此 subtype 并回复调用者
提供的第二统计值；当前 Guest 没有已确认的同源计数器，运行态显式传零值，因为 GPU 工作
提交尚关闭。这样 idle 查询不会堵住后续 RPC，但不代表支持运行中 GPU 的此项遥测；开放
提交前仍需找到同源值或定义 Linux 侧映射。`mtapi64.dll` 可见 WDDM GPU Engine 的
`UtilizationPercentage` 读取路径，但尚无证据把该 WMI 值关联到这三个 Escape QWORD。

还继续追踪 type=2/subtype=2：`FUN_140024f38` 配置的 opcode 4/5 回调分别落到
`FUN_140001d10`（返回 0，调用方随后报失败）和 `LAB_140027274`（直接返回
`0xfffffffc`）。`FUN_14002b260` 只在 payload 低字节为 4 或 5 时调用这对 MTT 私有
connector 回调；它们没有实现 Imagination PVZ heap 映射，Linux 侧继续保留未处理。

新增 `verify-host-query.py` 对照了 532 个统计查询和 133 个中断确认原始指令案例，
另检查 10 个未实现请求保持输出不变；传输验证也覆盖 subtype 2 和未知 subtype 的队列行为。
实机辅助服务已处理并回复已支持的查询，
Host 回复环 tail 持续推进。它们是统计查询；目前没有证据证明它们会解除固件连接停滞。
证据见 `reports/host-query-validation.json`、`reports/live-service-validation.json`。

首次模块遗漏了 `140026b30` 在设备信息请求后提交 BAR2 基地址的步骤。
现已补入主模块源码，并通过辅助模块实机写 BAR1 `+0x20=0x800000000`。
之后重发一次 online/DM0 doorbell，固件仍 READY、started=0、命令尾指针未前进。
进一步追踪 `14002fe44` 确认平台 `+0x518` 来自 PCI BAR4，日志名为
virtualization Local MMU memory base；本机 BAR4 无资源，因此参考值为 0。
主模块源码补齐 BAR1 `+0x28=0`，辅助模块已增加限定 BAR4 缺席的提交操作。

`140026b30` 在本机 flags=0x3d1 路径还发送三条 type=3、operation=0 通知：
subtype 0/1/2 的值为 0、`BAR2_GPA + shared_offset = 0x843000000`、`0x200000`。
65 组原始指令对照与 6 个拒绝案例通过；实机发送后 RPC 环 0 head/tail 均为 6，
确认宿主消费三条消息，但 FW 的 head=5/tail=0、started=0 未改变。
证据见 `reports/shared-announcements-{validation,hardware}.json`。

主驱动源码已接入共享 IRQ 与持续 RPC，并在连接等待持锁期间同步处理查询。
`mt_rpc_transport.h` 同时用于主驱动和现场辅助模块；RAM 模型测试覆盖 7680 个
批量发送、8192 个查询回复游标组合、26 个拒绝/非待处理中断案例和 133 个
原始指令 IRQ 确认案例。该测试不证明真实 PCI 内存顺序或工作队列调度正确。
新版主模块未加载；现场辅助模块复用同一 transport 的测试另有实机报告。

### 基础格式

`1400268d0` 在 mode=0/1 时，在模式查询后发送 type=0/subtype=4、operation=0、
value=0x48809490d 的参考包标识通知；mode=2 跳过。mode=1 随后协商版本。
新主模块已补齐该通知，并按 `140025740` 在协商成功后才执行设备信息查询、
BAR 基地址提交及共享区描述。独立非连接探测仍允许只查询信息。
没有执行包更新查询/下载函数。Linux 在消息无法入队时中止启动，而参考代码忽略此通知错误。

`verify-package-announcement.py` 执行原始模式/版本路径，16 个案例包含当前协议、
9 个旧版本兼容分支、mode=0/2、错误与拒绝路径，另有 4 个 builder 拒绝检查。
实机补交标识后环 0 从 6/6 到 7/7，通知已消费；随后 online=8，固件队列仍 5/0。
完整 8 MiB 固件区与首次失败试验快照逐字节相同。
BAR1+0x30/+0x38 回读为 0x771fef000/0x800000，与保留分配吻合，不能据此推断映射已安装。
见 `package-announcement-{validation,hardware}.json`、`publication-readback.json`。

共享页 0 首字节是 gpu_normal，注册后由 Guest 置 1。
共享页 1 含四个环，每环 `0x210` 字节；首两个字节分别为 head/tail，
消息从 `+0x10` 开始，共 16 个 32 字节槽，保留一个空槽判断满队列。
消息：`u64 value; u8 type, subtype, status, operation; u8 reserved[20]`。
operation 0=通知，1=请求，2=回复。字段通过对齐及内存屏障后发布 head。

本机验证的序列：注册页 0 → 置 gpu_normal → 注册页 1 → 通知 type=2/subtype=0 →
注册页 2/3 → 请求 type=0/subtype=1 → 收到 mode=1 → 请求 type=0/subtype=2、
value=`0x0005000500070002` → 收到兼容回复 → 通知 type=2/subtype=1 → 逆序撤销四页。
请求走环 0，回复走环 3；通过 BAR1 `0x138` 通知宿主，无须先开启 MSI 或 BusMaster。
未发送包更新命令，未修改固件连接状态。

## 3D 渲染上下文与 Data Master 2 (Universal) 协议 (r35 - r38)

### 1. 3D 渲染上下文显存切片 (11 BOs) 与生命周期

原厂 Linux UMD (`libsrv_um_MUSA.so.1.0.0`) 在初始化 3D 渲染上下文（`RGXCreateRenderContextCCB`）时，为每个上下文建立 11 个专用 BO，总净开销为 **86,300 字节**（29 个 4KiB 页面）：

| 索引 | 大小 (字节) | 对齐 (字节) | 分配标志 | 对应堆属性 | 描述 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| BO 0 | 3,072 | 128 | 0 | PDS 堆 | PDS code/data buffer for DCE context switch tasks |
| BO 1 | 6,144 | 128 | 0 | USC 堆 | USC shader buffer for DCE context switch tasks |
| BO 2 | 776 | 32 | 771 | Component 堆 | DCE context switch snapshot |
| BO 3 | 468 | 32 | 771 | General 堆 | TA state |
| BO 4 | 1,024 | 128 | 0 | PDS 堆 | PDS code/data buffer for DCE context switch tasks |
| BO 5 | 1,024 | 128 | 0 | PDS 堆 | PDS code/data buffer for DCE context switch tasks |
| BO 6 | 16,400 | 32 | 771 | General 堆 | VDM uniform PDS state |
| BO 7 | 16,400 | 32 | 771 | General 堆 | VDM uniform PDS state |
| BO 8 | 16,400 | 32 | 771 | General 堆 | DDM uniform PDS state |
| BO 9 | 16,400 | 32 | 771 | General 堆 | DDM uniform PDS state |
| BO 10| 8,192 | 128 | 2147484419 | General 堆 | Rasterisation context state |

每个 BO 在创建后由驱动自动写入初始硬件模板常量（保存在 `kernel/mt_gfx_context_data.h`）。

### 2. 248 字节 CSW（上下文切换状态字）与 Linux 提交规范

逆向原厂 Linux UMD (`libsrv_um_MUSA.so.1.0.0`) 查明其与 Windows UMD 的结构性区别：
- **Windows UMD**：CSW 调度块位于 `+0x3620` 偏移；
- **Linux 原厂 UMD**：CSW 调度块严格位于 **`+0x58`** 偏移（全长 18,160 字节即 `0x46f0`）。
- **Linux Envelope 头部**：
  - `+0x00 .. +0x0f`: Envelope 控制标志；
  - `+0x10`: CSW 的 GPU VA 指针（指向 `command_va + 0x58`）；
  - `+0x18`: 状态/版本标志；
  - `+0x1c`: 固件提交操作码 `0x66`（RGXVertex / UniversalQueue）；
  - `+0x58`: 248 字节 CSW 状态字紧随其后：
    - `+0x58 + 0x00`: DCE snapshot GPU VA（取自 BO 2，掩码 `& ~0x1fULL`）；
    - `+0x58 + 0x08`: TA state GPU VA（取自 BO 3，掩码 `& ~0xfULL`）；
    - `+0x58 + 0x10 .. +0xef`: 任务阶段控制字与 stage metadata；
    - `+0x58 + 0xf0`: Rasterisation context GPU VA（取自 BO 10）。

### 3. Data Master 2 路由与真机 3D 负载硬件闭环

- S3000 硬件支持的数据主控队列（Data Master）有效集合为 **DM 1 (2D TQX)**、**DM 2 (3D Universal)** 和 **DM 3 (Compute)**。
- 向未激活队列（DM 4/5）发送数据会导致超时，驱动在 `mt_marker_fence.h` 中严格拒绝 `dm < 1 || dm > 3`。
- 3D 渲染执行上下文规范：
  - 节点类型：`node_type = 5`；
  - 硬件队列：`dm = 2`；
  - 调度优先级：`scheduling_class = 1`；
  - 工作负载类型：`type = 3`（RGXVertex/UniversalQueue）；
  - 固件提交操作码：`opcode = 0x66`；
  - 包格式：包含 18,160 字节（`0x46f0`）的 Linux 原厂 Universal Queue 完整包。
- **真机实测数据**：
  - 单帧初探：`seq=1 result=0`，耗时 106 微秒；
  - 10 帧批量（5ms 延时）：10/10 成功，最低延迟 91 微秒；
  - 50 帧零延时压测：50/50 成功，耗时 4.568 毫秒，均延 60 微秒（最低 48 微秒），等效吞吐超 16,000 FPS；
  - **硬件 Ring 队列 64 槽自动回绕验证**：
    - 硬件队列深度为 64 slots。当累计提交超过 64 时，游标正确回绕：`head=(prev + N) % 64`；
    - 实测从 `head=61` 提交 20 帧后回绕至 `head=17`，再提交 25 帧推进至 `head=42`；
    - 硬件与固件保持严格同步，未发生队列拥塞或指针错位。


### 4. 游标对齐自愈与安全重连协议

- **命令队列（Ring 0/1）**：Guest 是 Producer，更新 head；固件是 Consumer，更新 tail。异常中止时需将 head 调回 tail 恢复平衡。
- **完成/事件队列（Ring 2）**：Firmware 是 Producer，更新 head；Guest 是 Consumer，更新 tail。重连或会话复位时，Guest 必须将 `cursor + 8`（tail）写入与 head 相同的值，以清空历史事件并完成对齐。
- 全局 `d->service.poll_session` 回调必须保持只引用常驻驱动符号，临时加载的诊断或恢复模块严禁留下野指针。

