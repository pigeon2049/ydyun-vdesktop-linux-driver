# 固件与页表适配记录归档（09-28 前后）

> **归档文件，只读。** 2026-10-03 从 `mt-vgpu-guest/FIRMWARE-NOTES.md` 移出全文。其中的尚未实现/尚未发布结论已过期，当前状态见仓库根 `STATUS.md`。

---

连接后上下文描述符新增纯 CPU 构造与缓存刷新代码，原始指令对照已通过；
尚未向宿主发布，详见 [描述符布局与限制](reports/firmware-context-layout.md)。

现已完成显存快照、固件解析与状态构造、页表和资源构造、队列与连接状态机的对照验证。
2026-09-28 已实测将 8 MiB 固件镜像写入分配区，读回一致后恢复完整原数据并核验。
没有发布固件地址或 GPU 页表根、发送连接命令或启用硬件渲染。

## 实机内存读取

`kernel/mt_guest_probe.c` 新增 `snapshot_memory=1`，根据当次设备信息响应计算地址，
校验版本、分段边界、BAR 长度、对齐和 READY 状态后读取：

| 区域 | BAR2 偏移 | 读取长度 | 非零字节数 |
| --- | --- | --- | --- |
| 固件分段开头 | `0x3f000000` | 1 MiB | 104974 |
| 标志 0x20 的共享分段开头 | `0x43000000` | 64 KiB | 1600 |

读取成功，随后模块卸载，Guest 状态仍为 0、固件仍为 READY。
这些区域已有数据，不能把 READY 理解成显存为空；数据的来源和生命周期尚未完全确认。
原始快照为 `reports/firmware-shared-before.bin`（本地保留，Git 忽略），
摘要和哈希为 `reports/memory-baseline.json`。快照不覆盖完整 64 MiB 固件分段，
也不代表已备份全部 GPU 状态。

## 固件布局的静态证据

`14001728c` 的 Guest 分支配置分配大小为 `0x800000`（8 MiB），
状态区域大小为 450000（`0x6ddd0`）；`140015dc8` 设置状态、队列等 CPU 指针。
`14001ccd8` 的两个默认堆表 `141030d90` / `141030fa0` 的 `+0x98` 均给出
固件 GPU VA `0xe1c0000000`。这是默认布局证据，还需核对全部运行时调整。

`14000c0ac` 构造 80 字节固件命令，操作码位于 `+0x0c`，PID 位于 `+0x4c`。
`14000bf20` 使用 64 槽环，head/tail 分别位于队列基址 `+0x2e00` / `+0x2e08`，
写命令后更新 head，并通过 BAR0 `0xb00` 通知对应队列。
`1400168b4` 用操作码 `0x46` 建立连接；前提是固件内存、页表和状态结构初始化完整。
现在没有执行该命令。

`1400224b0` 向 BAR1 `0x30` / `0x38` 提交固件设备物理地址和长度；
它在 `1400159cc` 的 Guest 路径中调用。其宿主映射、副作用及撤销顺序尚待核实，
不能仅凭寄存器存在就发布未完成的固件区。

## GPU 页表代码与原始指令验证

`kernel/mt_mmu.h` 实现三级、4 KiB 页表的 PC/PD/PT 编码及 8 MiB 固件映射构建。
参数使用 **GPU 设备物理地址**，不能把 CPU BAR 基地址传入。
VA 索引分别取 bits 39:30、29:21、20:12。PC 项为 32 位，PD/PT 项为 64 位。
条目位 62 按 Windows 原指令保留，不套用 x86 CPU 页表解释。

安装了 Debian `python3-unicorn`，`scripts/verify-mmu.py` 在受控模拟内存中执行三个
无外部调用的 Windows 叶函数（`14002a82c`、`14002a878`、`14002a8d8`），
与新 C 实现逐个比较：每函数 660 组，共 **1980 组全部一致**。
未执行 Windows 驱动入口或任何硬件 I/O。

依据本机返回的 GPU 地址生成 6 个页表页，映射 2048 页 / 8 MiB。
独立遍历全部 PTE 和两层目录检查目标地址与权限位，另验证 5 组无效输入不修改输出。
结果为 `reports/mmu-validation.json`；页表候选文件在 `build/mmu/firmware-page-tables.bin`。
**候选页表尚未上传，指定的设备页表存储范围也尚未由驱动保留**。
通过位编码验证不代表已证明 MMU 模式、分配策略或硬件上下文发布正确。

## 已提取的嵌入固件

`scripts/extract-firmware.py` 依据 `14001ee48` / `14001ee94` 的表地址提取五个固件容器，
依据 `14001f0b0` 解析链式载入记录，输出拷贝、清零、寄存器脚本和跳过记录：

| 选择值 | 固件 | 容器字节数 | 记录数 |
| --- | --- | --- | --- |
| 0 | gen1 | 160768 | 98 |
| 1 | gen2 | 183568 | 110 |
| 2 | gen2_1 | 168464 | 101 |
| 3 | gen3 | 190400 | 113 |
| 4 | gen6 | 168544 | 97 |

容器及逐条记录在 `reference/firmware/`，哈希清单在 `reports/firmware-image-inventory.json`。
固定地址工具先检查整个参考 `.sys` 的 SHA-256，避免套用到不同版本。
这些是被解析的固件镜像，不是已验证可装载的 Linux 固件包。
本机 PCI ID 对应的参考驱动选择值已进一步确认，证据如下。

## PCI 分支、固件选择与 Guest 布局验证

`140025b88` 按 PCI device ID 的高字节选择平台初始化。
模拟执行本机 `1ed5:0222` 输入，确实到达 `14002d73c`；该函数调用
`14002d950` 安装 `14002de98` 特征回调。公共 PCI 能力/BAR 初始化不替换此回调。
`14000de94` 把设备上下文 `D+0x78` 作为输出缓冲传入 `1400230e0` / `140026064`。

回调写出的 `+0x18` 对应 `D+0x90`，其 bit 9 为 0，选择 `1400242ec` 的三级 MMU 路径；
`+0x20` 对应 `D+0x98`，低 3 位为 0，传给 `14001728c` 选择编号 0 的容器。
目录内将它命名为 `gen1.ldr`，但嵌入的构建文件名是 `mtfirmware_t0.elf`；
此名称不是对物理芯片代际的判定。验证覆盖 PB 能力位两种状态、平台 `+0x5e`
所有 256 个字节值及 `+0x60` 低三位的 8 种组合，共 4096 组，选择结果不变。

`kernel/mt_fw_layout.h` 实现 `14001728c` 的 Guest 分支，输出完整 `0x1610` 字节
CPU 侧布局描述符。固件分配 8 MiB，状态区 `0x6ddd0` 字节，命令队列从该偏移开始，
默认队列 GPU VA 为 `0xe1c006ddd0`；另需独立 4 KiB 辅助分配。
它不包含实际设备分配、固件运行状态和 MMIO 操作。

`scripts/verify-fw-layout.py` 在隔离的 Unicorn 内存中执行原始函数，
只替代内存分配、memset、memcpy 三种常规辅助操作。元数据校验、字段计算和
固件地址转换均运行原始指令，限制可执行函数范围，遇到意外分支立即失败。
五个选择值 × 四个边界/本机虚拟地址，共 20 组布局逐字节一致，另验证四组无效输入。

## 固件加载阶段镜像构造

`scripts/extract-firmware.py` 新增 `build_guest_loader_image()`，复现 `14001eee0`：
初始化 `+0x200` 寄存器脚本，按元数据添加固件地址映射，顺序执行容器拷贝/清零，
追加脚本和终止记录。Guest 的 code/state 基址重合，bootstrap 拷贝落在 `+0x29160`。
这些寄存器脚本只被写进本地字节数组，没有执行硬件写入。

对五个固件、两种 MMU 参数分别执行原始 `14001eee0` / `14001f0b0`，
与新构造器生成的完整 8 MiB 逐字节比较，10 组全部一致。
实际参考选择值 0 / MMU 参数 0 的结果保存在
`build/firmware/guest-loader-stage.bin`，SHA-256 为
`35d40f75099aaf7ff74040736ee7ee34e09e26f3556b7ea9040fa143bad9cee9`。
布局描述符为 `build/firmware/guest-layout.bin`，验证报告为
`reports/firmware-layout-validation.json`。

这只是 `140015f78` 填写运行状态之前的镜像，不能直接上传或用于证明硬件兼容性。
后续运行状态构造如下；宿主是否接受最终初始化、固件是否进入 ACTIVE 仍须实机验证。

## Guest 初始运行状态和虚拟地址预留

`kernel/mt_guest_heaps.h` 实现本机参考路径使用的 `0x1ef9` 资源掩码和三级 MMU
虚拟地址布局。预留区按 **2 MiB** 对齐，这是 `14002a3f4` 描述符中的 PT 覆盖范围，
不是 4 KiB 单页大小。动态 PDS、Static USC 和 Texture state 等延后分配资源，
在这一阶段保留容量但 VA 仍为 0，不能误当成已经映射。

`scripts/verify-fw-state.py` 执行原始 `14002a34c`、`14001ccd8` 和实际的
DVMM/RA 分配器代码；仅模拟 CPU 内存分配、内存拷贝/清零及单线程自旋锁。
新 C 实现的 22 个堆（包括可用容量、预留区）和 13 项资源与原始输出一致。
PB freelist VA `0xa000e00000`、大小 2 MiB 同时匹配本机 Host 信息；
YUV Coefficient VA 为 `0x84ffe00000`，DM Kill VA 为 `0x84ffe80000`。

`kernel/mt_fw_state.h` 实现 `140015f78` / `1400227e4` / `14002db90` 的
**Guest + Host PB + 初次初始化**路径：写入队列指针、PB 地址对、堆基址、YUV 地址、
DM Kill 偏移等字段，并生成 `+0x27148` 的 8 KiB 参数区。
PB 分支来自 `14001b194` -> `14001b018`，只构造两个 freelist 虚拟地址，
不复制 Native 路径的 0x1800 字节 PMVA 数组。

33 组地址输入分别执行原始完整状态函数调用链，与新 C 实现生成的完整 8 MiB
逐字节对照，全部一致。候选镜像 `build/firmware/guest-state-stage.bin` 的 SHA-256：
`d27f408f9048236a10095c849af150eb133834890045328e37c610a85a0cfe15`。
报告为 `reports/firmware-state-validation.json`。

候选输入选择 fence GPU PA 为 0（关闭对应可选功能），并不证明宿主要求的所有功能配置。
这些 VA 尚未映射到设备内存，镜像不是可以直接发布的完整驱动初始化结果。
新 C 代码在状态写入之前检查长度与地址范围，拒绝输入时不修改镜像。

## 私有设备内存池的进一步确认

将本机原始 `device-info.bin` 输入 `140026390`，并执行 `140027ab4` 检查
CPU BAR 到 GPU 地址转换，得到下表。`140027bec` 的池 0/1/3 分别使用这些区域。

| 池 | BAR2 偏移 | GPU PA 起点 | 大小 |
| --- | --- | --- | --- |
| 普通私有分配（页表等） | `0xa00000` | `0x605800000` | 24 MiB |
| Host PB | `0` | `0x36000000` | 2 MiB |
| 固件 | `0x3f000000` | `0x771fef000` | 64 MiB |

原离线页表候选使用第一个普通分段起点 `0x605000000`，它位于私有池之前的 8 MiB。
`verify-mmu.py` 已改为使用普通私有池起点 `0x605800000`；重新验证 1980 组编码和
2048 页映射通过，新页表 SHA-256 为
`8efb81e19b66356105f2ae46ba2093197ed5fe97cdd61ac5e82b78b60dcc391b`。
新旧候选内容均未写入硬件。新版候选对应的设备范围已在下面的分配测试中临时保留并释放。

## Linux 显存分配与实机写入通路

`kernel/mt_memory_layout.h` 解析实际 v2 响应并验证私有池布局。
针对本机已研究的 Host PB/独立固件配置拒绝不支持的输入，不对未知布局猜地址。
检查完整分段表长度、40 位 GPU 范围、页对齐、GPU 分段重叠、BAR 边界和固件末尾位置。
固件池上限设为本机已验证的 64 MiB，避免从不合理的响应创建过大的分配位图。
`verify-memory-layout.py` 对照原始 `140026390` 验证了 9 种合法尺寸组合，
另外拒绝 15 个畸形输入、截断响应和过短 BAR；失败不修改输出。

`kernel/mt_vram.h` 使用三个 Linux `gen_pool`，把分配 token、BAR 偏移、GPU PA
和 CPU I/O 映射分别保存，避免混淆地址域。由 PCI 资源管理保留 BAR2，
所有成功分配记录在链表中，部分失败及卸载统一回收。PB 池已创建但本轮没有分配或修改它。

实机 `reserve_memory=1` 分配结果：

| 用途 | BAR2 偏移 | GPU PA | 字节数 |
| --- | --- | --- | --- |
| 固件 | `0x3f000000` | `0x771fef000` | 8388608 |
| 页表 | `0xa00000` | `0x605800000` | 24576 |
| 辅助页 | `0xa06000` | `0x605806000` | 4096 |

占用普通池后，再请求整个 24 MiB 池正确返回 ENOSPC；页表区释放后可在同一地址重新分配。
只读快照显示固件第一页与此前基线一致。卸载后再次加载成功，验证 BAR2 能够重新保留。
证据为 `reports/vram-reservation-status.txt` / `vram-reservation-validation.json`。

之后单独启用 `test_memory_write=1`，在未提交给 Host/FW/MMU 的辅助页进行
**备份 → 4 KiB 图案写入 → 读回比较 → 恢复 → 读回比较**。两项比较均通过，
见 `reports/vram-write-status.txt` / `vram-write-validation.json`。
这次确实写过设备显存，因此不能再说“没有任何显存写入”；但固件和页表候选内容尚未上传。

两次测试后模块均已卸载。Guest=0、固件=READY(1)，SDDM/SPICE/Tailscale 均 active，
内核 taint 保持已有的 12288，没有新增内核异常。当前既无 GPU 资源映射，也无硬件加速。

## 固件命令队列移植

`14000c198` 清零 `0x11520` 字节，等于六个 `0x2e30` 字节 DM 区。
每 DM 的布局如下，所有计数均为 32 位：

| 相对偏移 | 内容 |
| --- | --- |
| `0x0000` | 命令环 0：64 × 80 字节 |
| `0x1400` | 命令环 1：64 × 80 字节 |
| `0x2800` | 事件环：64 × 24 字节 |
| `0x2e00 / 0x2e08` | 命令环 0 head/tail |
| `0x2e10 / 0x2e18` | 命令环 1 head/tail |
| `0x2e20 / 0x2e28` | 事件环 head/tail |

命令提交 `14000bf20` 以 `(head+1)&63 == tail` 判断满；先复制 80 字节，
执行有序屏障、写 head、再次屏障并读回 head，再经 `14001b750` 写 BAR0 `0xb00=DM`。
Windows 在生产者锁内轮询最多 10000 次，每次 stall 90us；Linux 版本单次返回
`-EAGAIN`，将有期限重试留给后续连接层，避免在锁内长时间忙等。
Linux 还拒绝 head/tail >=64、DM>=6、ring>=2 和过短映射，失败不发布命令。

内核命令构造 `14000c0ac` 清零 80 字节，`+0xc` 为 opcode、`+0x4c` 为 PID；
非空可选参数将输入 DWORD 0/2 放入 `+0x18/+0x20`。连接 `0x46`、断开 `0x47`
使用 DM0/ring0，参数为空。当前尚未在硬件发送这两条命令。

排空检查 `14000bdc4` 比较同一 DM 的三个环，不能忽略事件环；`14000bc9c`
只检查 DM1..5，DM0 在断开命令提交后另行检查。这些读取只有在停止新提交、
配合固件状态变化时才有生命周期意义，不能单凭队列空就释放仍被固件引用的资源。

`scripts/verify-fw-queue.py` 验证了全部 6×2×64 个入队位置（含回卷），
12 个满环、36 个排空组合、66 个命令、9 个非法提交输入，以及初始化边界。
参考执行只对锁、进程 ID、延时作模型，MMIO write helper 捕获参数不访问设备。
整块 70944 字节内容、BAR0 kick 参数与 Linux 后端调用顺序均匹配；不证明硬件时序。

`kernel/mt_fw_queue_io.h` 已接入 BAR2 固件映射，内核构建通过。
本机 `reserve_memory=1` 读取六个 DM 的三环均为空；此轮设备显存未写、命令未提交。
模块随后卸载，状态恢复到原本的 Guest0/FW1，服务正常。
证据：`reports/firmware-queue-validation.json`、`firmware-queue-status.txt`、
`firmware-queue-hardware-validation.json`。

## 旧 Host 2.3 代码的适用边界

为追踪固件注册生成了 `reports/host-2.3-core.asm`（完整反汇编）、
`host-2.3-bar0.asm` 和 `host-vgpu-symbols.txt`。这些是旧 Host 的辅助证据，
不能据此认定当前宿主版本相同，也不能把其 OSID 偏移用于本机。

旧 Host `vgpu_firmware_init` 依据预先分配的显存信息映射 Guest 固件区；
BAR1 handler 对 `+0x30/+0x38` 没有直接分支，经过注册回调后走通用缓存。
其 BAR0 handler 将 `890/898` 转到共享固件状态，使用 OSID-8，而本机 OSID=4。
因此“写入 FW PA/长度”的当前 Host 接收细节及撤销语义仍需独立确认。
`mtgpu_vgpu_vm_offline` 中 FW context `+0x208` 指针是在线位图，不能把它误读为
BAR1 `+0x38` 的缓存值；该路径发的是 Host 命令 `0x6e`，也不同于 Guest `0x47`。

## 稀疏页表、默认映射与静态资源（2026-09-28）

`mt_mmu_bootstrap.h` 将固定六页固件构造扩展为初始资源可用的稀疏构造器。
按第一次使用的顺序生成 root/PD/PT，支持跨 2 MiB 页表和 1 GiB 目录边界，
最多 16 个不相交、物理连续的区间、64 页表页、64 MiB 映射。
它在修改输出前检查所有地址、长度、重叠、标志和页表容量；失败保持输出和页数不变。
这不是运行时映射器，不处理 GPU TLB 刷新、提交中的上下文或离散系统内存页。

验证器实际执行 `1400180ec → 140018f68 → 140019074/1400197b4 → 140018bd0`
和编码指令，只替代 CPU/GPU 分配、物理地址翻译。38 个场景共 129 个页表页
逐字节一致，覆盖全部 32 种映射标志；另有 14 类非法输入不改变输出。
多资源组合树只作为算法测试，**不是已经确认的固件上下文布局**。
固件自身路径 `1400159cc` 仍只安装其独立映射，不能把其他上下文资源混入同一 root。

`14002a4ec` 另行分配三页：第 1 页 512 个 PDE 指向第 2 页、编码低位为 9；
第 2 页 512 个 PTE 用 encoder flags `0x11` 指向第 3 页；第 3 页为零。
普通 root/PD/PT 的创建仍使用全零初始化，与这份默认映射分开。
三个默认映射地址场景完整匹配原始代码。证据为 `reports/mmu-bootstrap-validation.json`。

`140019cc4` 总是分配四项静态资源：PDS 1 MiB、USC 1 MiB、YUV 512 KiB、
DM Kill 512 KiB。USC 虚拟地址暂缓分配不表示其 backing allocation 可以省略。
`mt_static_resources.h` 还原 12 组 YUV 系数、末尾缩放系数和 68 字节 DM Kill 程序，
保留函数未指定的字节。分别以全零和随机底图执行原始资源分配/写入路径，对照整个
3 MiB 资源内容通过；`reports/static-resources-validation.json` 保存摘要和镜像散列。

内核 `prepare_resources=1` 已实机完成八项额外分配。此次地址如下：

| 用途 | GPU PA | BAR2 偏移 | 大小 |
| --- | --- | --- | --- |
| 默认映射 | `0x605807000` | `0xa07000` | 12 KiB |
| PDS | `0x60580a000` | `0xa0a000` | 1 MiB |
| USC | `0x60590a000` | `0xb0a000` | 1 MiB |
| YUV | `0x605a0a000` | `0xc0a000` | 512 KiB |
| DM Kill | `0x605a8a000` | `0xc8a000` | 512 KiB |
| fence | `0x605b0a000` | `0xd0a000` | 4 KiB |
| paging context | `0x605b0b000` | `0xd0b000` | 8 KiB |
| PB | `0x36000000` | `0` | 2 MiB |

内核按本次实际分配地址生成 1085440 字节 CPU 准备数据，导出到
`build/firmware/bootstrap-stage-live.bin`。固件六页页表和三页默认映射再次使用原始指令
以本次地址重建核对；两项静态资源与原始指令镜像一致。
整体 SHA-256 为 `38cfb705abeb814a1ad504415e547325c625cbc35c39a846ff1b45f66ee63b1b`。
所有新资源都未写入设备，也没有提交 FW 地址或 MMU root。
退出后显存保留、I/O 映射和 CPU 准备数据均释放；Guest0/FW1、服务正常。
记录为 `reports/bootstrap-status.txt`、`bootstrap-vram-status.txt`、
`bootstrap-hardware-validation.json`。

## 连接状态机与完整固件传输（2026-09-28）

`mt_fw_connection.h` 对应 `1400168b4 / 140016b4c` 的正常 Guest 分支。
连接先发 online 通知、写 Guest1、提交 `0x46`；只有 FW2 且镜像 `+4` 非零才写 Guest2。
400 次轮询每次 25ms，在计数 100/200/300 时重复 online/`0x46`；FW4 是明确拒绝。
断开先等 DM1..5 排空，再发 `0x47`，DM0 排空后才写 Guest0。
参考代码的工作队列等待允许到第 401 次 delay 后成功；若仍不空，则发断开但报告失败。
移植保留了该边界，不能仅看累计次数判断超时。

原始指令测试记录每一次 FW 状态读取、Guest 写入、online、命令、队列检查和 delay，
27 个即时/延迟成功、拒绝、状态切换和超时场景逐步一致。Linux 额外传播入队失败，
避免把丢弃的命令当成功。`reports/firmware-connection-validation.json` 保存每案重试时间。
所有状态机测试目前使用模拟固件状态，不能视为真实连接成功。

`14000de94` 在 feature word `D+0x90` bit0 置位时才分配独立的 8 字节系统通知区，
将翻译后的物理地址存入 `D+0x1048`。`14002de98` 在本机参考路径清除此 bit，
因此镜像 `+0x270f0` 保持零有原始代码依据；它不应指向预留的 4 KiB GPU fence 资源。

新增 `mt_fw_image.h` 使用 Linux firmware API 加载固定 8 MiB loader-stage 镜像：

- 路径 `/lib/firmware/mt-vgpu-guest/gen1-guest-loader.bin`，已安装。
- SHA-256 必须为 `35d40f75099aaf7ff74040736ee7ee34e09e26f3556b7ea9040fa143bad9cee9`。
- 检查本机 v2/Host PB 信息和预期 PB VA/大小，再覆盖已验证的 Guest 初始状态与空队列。
- 内核导出的最终 8 MiB 与 `guest-state-stage.bin` 逐字节一致，SHA-256 为
  `d27f408f9048236a10095c849af150eb133834890045328e37c610a85a0cfe15`。

`test_firmware_upload=1` 是显式、默认关闭的传输验证。只在 Guest0/FW1 且本轮
资源准备和镜像校验成功时运行。备份实际 8 MiB，写入镜像、完整读回比较，
再恢复原数据并完整读回比较。原数据备份留在 root-only sysfs，直到卸载。
本机一次实测两项比较都通过，记录为 `reports/firmware-upload-status.txt` /
`firmware-upload-validation.json`。原数据已导出到
`build/firmware/firmware-before-upload-8m.bin`，SHA-256 为
`066d0689a0e2e3f44a98d1f6b747a3e91f99412be3a7a5ac7c1468612f3f2ba1`。

这次实际改写了固件分配区，包括队列数据，随后恢复；没有写 BAR1+30/+38、
Guest 状态或 doorbell，因此没有向宿主提交新镜像、没有提交命令，页表也未上传。
测试脚本最初在模块绑定期间调用独立 `mt-status`，被其并发访问保护拒绝；
传输与恢复均已完成，finally 卸载模块后重新读取状态成功，无需重复写入测试。
绑定期间应读取模块自己的 `connection` sysfs，独立工具仅用于未绑定设备。

卸载后仍为 Guest0/FW1，SDDM/SPICE/Tailscale active，无新增内核异常。

## 下一步依赖

1. 核实宿主固件映射的提交/撤销协议，将已验证的设备内存分配器用于完整初始化。
2. 把已验证的状态和队列构造接入实际资源分配/映射，初始化必要的资源内容，再执行可撤销的连接试验。
3. 连接成功之后仍需处理中断、命令完成、DRM 与配套用户态；当前 llvmpipe 状态未改变。
