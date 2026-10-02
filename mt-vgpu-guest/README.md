# MT vGPU Guest：本机适配与实验

> **当前状态（2026-10-03）：以下横幅是按时间堆叠的历史记录，最新条在最上，
> 但顶部也已滞后。当前权威快照是仓库根目录 `PROGRESS-SNAPSHOT.md`
> （逐轮过程记录在 `MEMORY.md`）。两者冲突时以快照为准。**
>
> 一句话现状：厂商 MASA UMD 在自研内核桥上走完 8 个符号（connect → device →
> devmemctx → render → syncprim → kicksync → compute → kicksubmit
> accept-and-inspect），全部返回 0；S4-3 交接第一步（DMA + VM plan +
> kick T1+T2 只读观察）已落桥；首次 RGX 真实执行 + 像素读回 + 20 帧批量
> 已完成（r66/r70/r71）。真 kick 提交入口仍拒绝，那是 S4 边界。
> 运行中会话（`mt_guest_probe` 绑定 `00:0e.0`，Guest/FW 2/2 pinned；
> `mt_pvr_bridge` 在载；`/dev/dri` 有 `card1`/`renderD128`）**不要卸载模块、
> 解绑设备或提交额外工作**。下文“本机状态”表格与各“最新”横幅只代表各自
> 历史阶段。

**最新冷重启段结构自适应突破与 3D 渲染执行硬件机理深度查明（2026-09-30，r41b）**：查明宿主机冷重启后前 80 MiB 显存碎片化为 16 MiB + 64 MiB（OSID 7）引发 `-95` 拒绝的根本原因。依据 Windows 原厂驱动（ReferenceOracle `140026390`）放宽首段阈值至 `size < 0x1000000`（16 MiB），99 项单元测试与真实硬件 probe 100% 通过，硬件会话完整建立（`guest=2, fw=2`）。统一全功能 DRM 驱动 `mtvgpu 0.3.0` 成功上线，向用户态提供 `/dev/dri/card1` 和 `/dev/dri/renderD128`。深入查明 3D Universal 任务在物理 GPU 上的执行机理与 MMU 边界（Render Target Extent 与显存尺寸必须严格对齐、模板远端地址必须安全清理），为用户态无缝图形渲染铺平了道路！

**最新 3D Render Target 显存帧缓冲动态绑定与绘制读回验证（2026-09-30，r41）**：成功实现硬件 3D Universal 命令流中 Render Target 0 目标显存表面的动态编解码与绑定。通过独立 `slot_lock` 彻底剥离 GEM 槽位分配与全局 `submit_lock`，杜绝进程异常退出死锁。在 `space_3d` 空间中将渲染目标显存切片（Slot 0 & Slot 1）分别映射至 GPU VA `0x60000000ULL` 与 `0x61000000ULL`，总 mapping range 严格控制在 23 ranges（物理上限 24）。扩展 `struct drm_mt_submit_3d` 引入 `target_handle` 并保持 32 字节 ABI 兼容；升级 [userspace/mt-3d-check.c](userspace/mt-3d-check.c) 完成 GEM 创建、0x5a 特征填充、3D 渲染执行、Fence 等待与 64 KiB VRAM 完整读回核验全闭环！详见 [r41 渲染目标报告](reports/r41-3d-render-target.md)。

**最新用户态 DRM 3D 渲染执行接口打通（2026-09-30，r40）**：成功注册统一全功能 DRM 驱动 `mtvgpu 0.3.0`（`/dev/dri/card1`、`/dev/dri/renderD128`），宣告 `0x7` 全能力（2D Copy + Native Fill + 3D Universal）。引入 2D 与 3D 独立双 VM Space 隔离架构突破单个 MMU 空间 24-range 限制。纯用户态程序 [userspace/mt-3d-check.c](userspace/mt-3d-check.c) 通过标准 `DRM_IOCTL_MT_SUBMIT_3D` 直接发起 3D 渲染执行，每一帧成功绑定并核验 Linux 原生 syncobj 与 sync_file 异步栅栏，执行延迟仅 52 微秒，硬件队列同步推进，实现用户态到 GPU 硬件的完整闭环！详见 [r40 用户态 3D 报告](reports/r40-userspace-drm-3d.md)。

**真机 3D 渲染多帧批量压测与环形回绕突破（2026-09-30，r39）**：升级 [kernel/recovery/mt_live_3d.c](kernel/recovery/mt_live_3d.c) 支持多帧连续提交、动态参数注入与纳秒级性能采样。物理真机累计执行 **106 帧** 真实 3D Universal 任务，成功率 **100%**。零延迟极限压测下 50 帧总耗时仅 4.568 毫秒，平均单帧耗时仅 **60 微秒**（最低 48 微秒），等效吞吐超过 16,000 FPS。成功验证了 **硬件 Ring 队列跨越 64-slot 边界的自动回绕**（游标 61 -> 17 -> 42）。详见 [r39 压测报告](reports/r39-3d-batch-stress.md)。


**最新真机与渲染上下文进展（2026-09-30，r36）**：完成冷启动后硬件会话恢复（OSID 4、`mt_cold_disconnect` 置 Guest OFF、`fresh-trial.py` 成功重连），并复核通过 30 个真实 GPU 硬件任务（含 23 次 1080p 颜色矩形原生填充与 2 次 4 MiB 显存复制，读回并导出 6MB PPM）。在此基础上闭合原厂 Linux QY1 3D 渲染上下文（`RGXCreateRenderContextCCB`）：定义 11 个专用 BO 显存规范（总需求仅 ~84.3 KiB，完全能在现有普通堆余量中满足）、12 个保存/恢复任务阶段及 248 字节 CSW 模板，经 C 实现逐字节核验与内核 6.12 头文件下 `W=1` 零警告编译通过。详见 [r36 渲染上下文记录](reports/r36-gfx-context.md) 和 [验证报告](reports/r36-gfx-context-validation.json)。

**图形适配进展（2026-09-30，r35）**：已重建真正 TA/3D 的 528 字节寄存器及单批次 18,112 字节 Windows 图形包。288 例整包与 Windows 原始指令逐字节一致，96 例 Linux/Windows/C 寄存器交叉验证及内核 W=1 编译通过。本轮为离线编码验证，尚缺着色器、VDM 命令流和真实渲染上下文；没有新增 GPU 任务，仍 completed=262、pending=0。OpenGL/Vulkan 与桌面加速尚未实现。见 [图形包重建记录](reports/r35-gfx-packet.md) 和 [整包验证](reports/r35-gfx-packet-validation.json)。

**最新真机结果（2026-09-30，r34）**：1920×1080 原生 GPU 填充与单次 4 MiB 显存复制已通过，8 MiB GEM 表面逐字节读回核验。新增 31 个任务全部完成，累计 completed=262、pending=0；四套根页表保持不变，固件正常。当前大表面前端为 `renderD130`。尚未实现 OpenGL/Vulkan、MTT 扫描输出或桌面加速。见 [1080p 真机记录](reports/r34-large-surface.md)、[验证结果](reports/r34-large-surface-validation.json) 与 [实际 GPU 读回图](build/r34-live/gpu-native-1080p.png)。

**用户态适配进展（r33）**：执行 Linux legacy UMD 原始指令，确认 DRM 主版本控制两套提交协议；实现受限单 QY1 TDM 包转换。80 例完整 Guest 描述符与 Windows 指令结果一致，4544 个异常包被拒绝。此转换尚未接入真机 ioctl，不代表原厂 OpenGL/Vulkan 已可运行。见 [Linux 提交格式记录](reports/r33-linux-tdm.md)。

**最新真机结果（2026-09-30，r32）**：原生 GPU 矩形填充已跑通，新增 DRM `mtvgpu 0.2.0`（`card2`/`renderD129`）支持 GEM 颜色填充、显存复制及真实 syncobj fence。62 次填充与 11 次新旧前端复制通过；累计 completed=231、pending=0，固件正常。已从实际 GPU 绘制结果导出测试图。尚未实现 OpenGL/Vulkan、Mesa、扫描输出或桌面加速。详见 [原生填充记录](reports/r32-native-fill.md)、[数据核验](reports/r32-native-fill-validation.json) 和 [用户态用法](userspace/README.md)。

**最新真机结果（2026-09-30，r31）**：已注册实验 DRM `mtvgpu`（`card1`/`renderD128`），GEM 显存对象之间的直接 GPU 复制、原生 syncobj 等待和 sync_file 导出实测通过；单次 64 KiB、跨页、非对齐及两个 GPU 上下文切换均成功。新前端 9 个任务完成，驱动累计 completed=158、pending=0，固件正常。仍未实现 OpenGL/Vulkan 或桌面绘制。详见 [DRM/GEM 真机记录](reports/r31-drm-gem-syncobj.md) 与 [页表和执行核验](reports/r31-drm-validation.json)。

**前一阶段真机结果（2026-09-30，r30）**：用户态 GPU 复制接口已跑通，`/dev/mt-vgpu-copy` 可由 root 程序使用。4 个进程共 32 次复制、14 项非法/越权请求检查及完成后 copyout 错误检查通过；65,659 字节文件经 17 个 GPU 任务复制，输入输出散列一致。新增接口合计完成 50 个任务，驱动累计 completed=132、pending=0，固件正常。尚未实现 DRM/render 或桌面加速。详见 [用户态实测](reports/r30-userspace-copy.md) 和 [使用说明](userspace/README.md)。

**前一阶段真机结果（2026-09-30，r29）**：在 r28 工作会话中完成 80 次连续 GPU TQX 复制，15 种长度/偏移组合全部通过，覆盖 1～4096 字节、非对齐、页尾及环形队列回绕。完整源页、目标保护区和固定页表均核验通过；固件正常，pending=0。尚无 MTT DRM/render 节点或桌面加速。详见 [连续复制记录](reports/r29-repeated-hardware-copy.md) 与 [机器可读验证](reports/r29-copy-validation.json)。

**前一阶段真机结果（2026-09-30，r28）**：关机重启后进入 OSID 4 新会话，r24 页表权限修正版首次完成 256 字节 GPU TQX 复制，目标内容、剩余保护区和源页均核验通过，fence 正常完成，固件保持 ACTIVE。当前工作会话保留，先前阻塞已解除；尚无 MTT DRM/render 节点或桌面加速。详见 [真机复制成功记录](reports/r28-hardware-copy-success.md) 和 [数据验证](reports/r28-copy-validation.json)。以下为历史阶段状态。

**历史状态（2026-09-30，r27，已由 r28 解除）：当时受阻，未完成硬件加速。** 连续三轮确认同一故障会话，Guest 重启后仍 FW0；当前无 MTT 驱动绑定或 DRM/render 节点。r24 权限修正候选完整保留，缺少可验证的 Guest 专用恢复/旧地址撤销协议或新设备会话，暂不能继续真机提交。没有执行 MPC HWR，也不要求操作宿主。见 [完成条件与受阻审计](reports/r27-blocked-audit.md)。

**映射复核（2026-09-30，r26）**：已遍历 r23 真机根的全部 3335 页、14 个绑定，只有五个普通 BO 的 6 页误为只读，3329 个共享页权限正确；r24 修正没有遗漏同类共享映射错误。Windows ResetEngine 的适用路径仍会进入已知 HWR 请求，尚未找到独立 Guest 恢复入口。当前仍 Guest0/FW0，候选未重试，无硬件加速。见 [全量映射与恢复边界](reports/r26-mapping-and-recovery-boundary.md)。

**最新实机核查（2026-09-30，r25）**：Guest 已重启，但当前仍为 Guest0/FW0。只读采集确认 8 MiB 固件区与 r23 故障后快照逐字节相同，未完成队列和 `0x101` 事件保留。r24 权限候选尚不能重试；121 项 Windows 故障处理原始指令案例通过。旧自动加载驱动及诊断 helper 已正常卸载，当前 PCI 无绑定，无硬件加速。详见 [重启后的现场与故障分支](reports/r25-after-reboot.md)。以下条目为历史记录，其“当前/下一轮”描述不代表 r25 运行态。

**最新修正（2026-09-30，r24）**：从 r23 真机页表定位到普通 BO 错用了标志 `3`，实际生成“只读+coherent”页。Windows 与原厂 Linux 指令交叉验证确认；候选改用可写默认映射，并在 TQX 上传前拒绝只读输出/工作区。99 项 Python、15 项集成、280 项 GEM 与 330 项 fence 内核 RAM 检查通过。候选尚未真机重测，当前故障会话及资源仍保留，下一轮需要新的 Guest 启动。详见 [权限修正与验证](reports/r24-tqx-map-permissions.md)。

**最新真机进展（2026-09-30，r23）**：Guest 固件连接成功，DM1 空命令收到匹配完成事件并触发真实 Linux fence。随后单次 256 字节 TQX 复制返回 `0x101` 故障，尚未完成复制或启用图形加速。实际上传命令流/DMA 与 Windows 原始指令结果逐字节一致；现场页表解析到本次分配地址，源和目标均保持初始内容。当前 `Guest=0 / FW=0 / started=1`、`pending=1`，主模块及实验上下文保留全部资源；不要卸载或覆盖现场。99 项 Python 检查和 15 项集成检查通过。详见 [连接、fence 与首个复制故障现场](reports/r23-runtime-marker-tqx.md)。以下条目为历史阶段记录。

**前一阶段真机进展（2026-09-29，r22b）**：当前 V2 / OSID 6 已完成 Guest 信息页协商、显存堆重建、原厂 LMA 两页读写恢复、MMU 根上下文和 FW 内核虚拟堆创建/销毁，以及两页 GPU-local PMR 的三级页表映射/撤销。真实 PC、PD、PTE 的 Host 地址均核对通过；96 项 Python 检查通过。固件区与 PB 区保持原内容，测试模块已卸载，仍未启动 GPU 加速。下一步是 Guest 固件连接及任务提交，详见 [最新真机记录](reports/guest-mmu-live.md) 和 [机器可读结果](reports/guest-mmu-live-validation.json)。以下旧记录中的 OSID 4/7、未实测描述和旧候选状态仅代表各自历史阶段；请以最新记录为准。

**Legacy Linux UMD bridge ABI 复核（2026-09-29）**：5.2.0 `libsrv_um_MUSA.so.1.0.0` 有 205 个唯一 bridge 调用。2.3 Guest KMD 头文件覆盖 179 个，26 个缺失；本地 2.7.1 Native 头文件覆盖 198 个，7 个缺失；官方 5.2 DKMS Host 头文件覆盖 205 个。三套结构探针显示 5.2 Sync 分配 ABI 与 UMD 一致；2.3 只用 32 位地址写入、UMD 按 64 位读取，但零初始化会补零高位。`RGXKICKTA3D3` 多出的 8 字节在 2.3 handler 中未被读取，暂判高概率可忽略。缺失命令和部分 HWPerf/PFM 字段仍需追踪；5.2 DKMS 是 Host 配置，不能直接装作 Guest KMD。完整数据、字段判断和复现步骤见 [bridge ABI 审计](reports/legacy-umd-pvr-bridge-abi.md) 与 [脚本](scripts/audit-legacy-umd-pvr-bridges.py)。本轮没有安装或加载驱动。

**本轮适配核查（2026-09-29）**：已从当前 6.12 Guest 候选的核心二进制闭合固件选择链：默认只读参数 `mtgpu_load_windows_firmware=true` 经 Guest selector 进入 PVR 固件加载函数，选择 `.vz.win`；对应的 S3000 `1.0.0.0` 镜像已在候选 staging 中，SHA-256 为 `bd9b569dc8bee47ffcb606081aaf8a1942456510797b6ccb95397f5da56fa17e`。新增 [固件选择核验脚本](scripts/verify-guest-fw-selector.py)，可用 `python3 scripts/verify-guest-fw-selector.py` 重验。此项只排除了固件文件名/默认分支不匹配，不能证明固件启动或 OSID 7 堆已映射；候选未安装、未加载。

**Linux 信息页 ABI 核验**：配套 Linux 2.3.0 Host 的 BAR1 信息页响应明确写 version 1，并将 firmware heap base/size 放在 `0x848/0x850`；Guest 结构偏移与其一致。Windows 驱动探测的 version 2 页面使用另一种紧凑段表布局，不能假定 Linux Guest 一定收到这版 ABI；双版本候选仅为两种格式都提供了明确解析路径。已确认 Guest 本地 `mtgpu_platform_data.vz_data.fw_heap_card_base` 总偏移 `0x50` 并接入静态回归。当前仍缺 OSID 7 原始信息页字节和 `FW_PREMAP` 到 Host BAR2 的映射证据。见 [Host 固件堆与 Guest ABI 分析](reports/windows-fw-heap-ring-investigation.md)。

**双版本信息页适配候选（离线、未实测）**：后续核对发现保存的 Windows v2 页面在 Linux v1 的 VVPU 大小和 FW heap 字段位置读为 0，因此 v1-only 基址补丁不适用于该布局。新候选按页版本解析：v1 从 `+0x848/+0x850` 取 FW_MAIN；v2 从 `+0xc50` 条数、`+0x28` 起每条 24 字节的段表中找唯一 `flags&4` 段。VPU 大小读取改用 v1/v2 公共头的 `vm_bar2_actual_mem_size +0x20`；非 VPU 分支的三个 `fw_heap_size +0x850` 读取改用匹配 Host 固定的 8 MiB（`RGX_FW_HEAP_SHIFT=23`）。Linux Host v1 会把同一 BAR2 大小同时写入 `+0x20` 与 VVPU 段大小，并将 8 MiB 固定堆大小写入 `+0x850`。实现及边界见 [双版本信息页适配记录](reports/official-linux-guest-port.md#2026-09-29-双版本信息页离线适配候选)。候选仍未安装或加载，Windows v2 样本也不证明当前 Linux Guest 收到同一页。

**V1/V2 地址、共享内存与 Guest heap-count 候选（离线、未实测）**：audit13 在 audit12 的 V2 BAR2 地址/段解析、V1 回退和 Guest-only heap-count 修正上，新增 V2 VPU ring 映射修正。V1 保留旧 BAR2 起点和 32 KiB 行为；有效 V2 从已验证的 `vgpu_share_mem_dev_addr` 映射 ring 的 32 KiB 窗口，Host 仍收到完整 2 MiB 段长度。映射时会重新解析当前信息页，要求唯一 `flags&0x20` 段、已发布地址与该段对应且发布长度恰为 2 MiB。信息页缓冲区至少 4 KiB。当前候选为 [audit13-r10 mtgpu.ko](/opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/build/official-vgpu-2.3.0-guest-vgpu-info-v2-addr-sharedmem-vpumap-audit13-r10-20260929/mtgpu.ko)，SHA-256 `5c2b68a02f7a772ccacb729d6e99a2b872e1606863bf93d6995768cd6ef53d92`，build-id `59b8554cbed7738e56ea7552fd0ac7d847901589`；编译期布局断言确认预编译 ring mapper 的指针槽 `0x1bd88` 对应 `mt_chip.vm.mem`。本机记录的 `mem_mode=1` Guest `[644]` 计数错误已与核心比较分支和 `0x284` 错误码对应；构建时新增检查确认候选仅将 Guest 期望 heap 数从 5 调为 4。定向 vGPU helper 20 项、全套 Python 85 项、Windows V2 GDPA oracle 142 个地址对照、Guest PCI 顺序和 ftrace 检查通过。候选只离线构建，未安装或加载；OSID 7 实际页、Host backing、PVZ、固件启动和 GPU 执行仍未验证。另有 GDPA Guest 虚拟窗口与 PCI aperture 区分的复核，见 [V2 VPU ring 审计与适配记录](reports/official-linux-guest-port.md) 和 [机器可读审计](reports/vpu-v2-shared-ring-audit.json)。

**最新离线重建（audit13-r10）**：干净目录完整重建通过。构建脚本现把 Guest heap-count 校验放在全部后链接补丁之后，机器可读校验的 SHA-256 与最终模块一致；模块与离线 stage 副本散列相同，vermagic 为 6.12.107+deb13-amd64 SMP preempt mod_unload modversions。85 项 Python 单测、Guest V1/V2 地址兼容、Guest PCI 初始化顺序及 ftrace 元数据完整性检查通过。校验仍是静态 ELF/控制流结果，候选没有安装或加载；本机 [644] 修正尚无运行态验证。详细结果见适配记录与 VPU/V2 机器审计。

**候选与运行态身份核验**：只读诊断工具现会读取 `/sys/module/mtgpu/notes/.note.gnu.build-id`、模块 `initstate/refcnt/taint`，并可用 `--candidate <mtgpu.ko>` 比较候选 build-id 和 SHA-256。上次只读快照记录的 `mtgpu` build-id 与 `build/discarded-ftrace-oops-diagnostic-experiment/mtgpu.ko` 相同，与 audit13-r10 不同；采集时模块处于 `coming`，PCI 设备未绑定。该历史快照不会加载、卸载或访问 BAR/MMIO，见 [模块身份只读核验](reports/mtgpu-module-identity-readonly.json)。

**Linux v1 信息页快照解码器**：新增 `scripts/decode-linux-vgpu-info.py`，只解析用户提供的离线文件，严格拒绝 Windows v2，并输出 OSID、flags、VGPU/VVPU 段表、FW/MMU heap 原始基址和 SHA-256；不推断 BAR2 偏移或地址转换。ABI 偏移以随包头文件的 x86-64 布局为准，单测用 C 对齐模型核验 `0x848/0x858` 等关键偏移。当前保存的 `reports/device-info.bin` 与其它原始快照是 Windows v2/OSID 4，不能用于确认 Linux Guest OSID 7 的 fw_heap_base。工具用法和局限见下方适配报告。

**较新官方 Linux 栈核对**：已把官方 Linux Server 5.1.0/5.2.0 DKMS 与固件包下载到 `downloads/` 并只做静态分析。虽然源码认出 S3000 `0x0222` 为 QUYUAN1，也含 Guest/VZ 代码，但两份 DKMS 包的 `MUSA_NUM_OS_SUPPORTED=1`，编译时关闭多 OS Guest/VZ 路径，不能直接替代 vGPU Guest KMD。PVR 文档指出 Guest PVZ provider 需由具体 Hypervisor 提供；两版仍保留 Linux v1 信息页旧数据窗口，可作 ABI 旁证。5.2.0 源码在本机 6.12 内核仍遇到 PCI IRQ、PFN 跟踪、BAR resize 和 platform remove API 差异；没有安装或加载。详细来源、哈希和限制见 [Guest-only 移植记录](reports/official-linux-guest-port.md)。

**Linux v1-only FW_MAIN 基址候选（离线历史产物）**：此前的候选在 `mtgpu_platform_data_vz_init+0x8d` 用 Guest 信息页指针读取 Linux ABI 的 `fw_heap_base`（`0x848`），再发布到 PVR 的 `fw_heap_card_base`；`mtgpu_device_memory_fixup+0x447` 中用于 VPU 大小与范围计算的 VVPU `size` 读取保持原样。模块位于 `build/official-vgpu-2.3.0-guest-heapcount-fwbasepubfix-audit-20260929/mtgpu.ko`，SHA-256 为 `052695c9b38a73b36dce884ca633d647e79f68f2a5f183bbb5382c236d96ca42`。双版本候选见上文。更早的 `...heapcount-fwbasefix...` 候选把 `0x438` 的 VVPU 大小读取错误换成 `0x848` 的物理基址，可能破坏内存范围计算，已撤回并标记为不可用。OSID 7 原始信息页、运行时 heap backing 和 GPU 加速仍未验证。

该候选的后链接补丁现会同时核验 Guest-only 分支和 `vgpu_info` 指针发布指令，避免目标函数周边数据流变化后仍误打补丁。65 项 Python 单测、Host 固件槽（含 FW_CONFIG 子堆 ABI）、Guest FW premap（含 OSID 7 blueprint/heap ID）、PVZ/rawheap 全直接调用点扫描、Guest probe 的 PCI BusMaster 调用顺序、ftrace 元数据完整性、Windows heap-profile 静态核验和本地发行 ZIP Guest/Host 分界审计通过。双版本构建的逐字节后链接结果见 [vGPU 信息页 ftrace 核验](reports/vgpu-info-compat-ftrace-validation.json)。PVZ wrapper 在直接 ELF 重定位及随包官方源码中没有调用引用，但间接或外部调用尚不能排除；这一静态结论也不证明 BAR2 backing。Windows 显示驱动的 `0x0200` 配置 helper 填充的是节点/资源表，不能视作 Guest 物理堆映射证据；详见 [Windows RM heap 与 PCI 资源映射审计](reports/windows-resource-mapping-audit.md)、[PVZ 映射调用路径核查](reports/static-guest-fw-premap.md)、[Guest probe PCI 核验](reports/guest-probe-pci-master-validation.json) 和 [后链接 ftrace 核验](reports/postlink-ftrace-integrity.json)。

**2026-09-29 离线重建**：从参考包干净源码再次构建 Guest-only 候选，并应用已核对的 Local Guest 四堆修正；HOST 内存模式 Guest 的静态路径也支持该计数修正，Hybrid 则取决于 `osid_count`，不能视作通用修复。产物在 `build/official-vgpu-2.3.0-guest-heapcountfix-20260929/mtgpu.ko`，SHA-256 为 `8bf4a81b4219c5c89c0bd2b0382d3469859f716036c8e85b6981b116a18e8a7f`。vermagic、S3000 PCI alias、修正指令和 ELF 检查通过；18 个单元测试、TQX/VM RAM 测试和实验模块 `W=1` 构建通过。候选未安装、未加载。模式计数依据见 [Guest PhysHeap 控制流审计](reports/guest-physheap-control-flow.md)。

**运行态只读快照**：`/proc/modules` 仍显示 `mtgpu` 为 `Loading`、引用数 1；S3000 PCI 设备未绑定，PCI COMMAND 为 `I/O+Memory`、BusMaster 关闭。当前没有安全的运行时验证条件；本轮只做源码分析和离线构建。

**Windows 反编译新增旁证**：[RM heap 与 PCI 资源映射审计](reports/windows-resource-mapping-audit.md) 受限执行 mtkm64.sys 的 GPU 资源对象构造函数，selector 0x0200 选择 4 heaps，与 S3000 的 Linux QUYUAN1 family ID 相符但 ABI 字段关系尚未证实；mtdispkm 的同 selector 只初始化显示/引擎节点表，不证明物理 heap 数。另还原了 mtkm64 StartAdapter 的 PCI capability 检查和 Memory/BusMaster 使能路径，同时区分本地 BAR 映射和缺失的跨 VM PVZ 回调。此证据支持四堆候选，不代表 Guest 核心已验证。

**Host 固件堆槽与 Guest premap 反编译进展**：[BAR2 固件堆路径分析](reports/windows-fw-heap-ring-investigation.md) 还原出 Host 按 MPC/OSID 固定分配 8 MiB 固件堆槽；Host 私有 vGPU 记录表保存同一槽，Host 初始化也按 PCI BAR2 起始地址加槽偏移映射。Windows v2 参考页 OSID 4 的槽与 `0x3f000000` 固件段起点、保存的 8 MiB 分配记录相符。Linux 侧保存的 Guest OSID 是 7，不能把两份记录当作同一运行态。候选 Host 基址 `0x3e800000` 是由 OSID 4 地址反推，尚非 Host 字段实测。Linux PVR 核心按 `0xe1c0000000 + OSID×8 MiB` 构造 `FW_PREMAPn` 设备地址；本机 OSID 7 走 raw heap 数组索引 7，蓝图地址为 `0xe1c3800000`。Host 公式推得的 OSID 7 BAR2 offset `0x40800000` 与 GPU/card PAddr `0x7737ef000` 相对 OSID 4 的步进都为 `0x1800000`，但这些地址域之间的转换和 backing 未证实。Guest 初始化把 `vz_data.fw_heap_card_base` 送入首个 `PHYS_HEAP_CONFIG` 的 `sCardBase`，该描述符标记为 FW_MAIN、大小 8 MiB；其本地表达式中 `fw_heap_size` 抵消，条件分支可取自信息页 `segment_info[DEVICE_TYPE_VVPU].size`，但 OSID 7 原始页记录不含判定该分支所需的 flag/段值。这仍不能证明该堆、PVR premap 地址与 Host BAR2 槽之间存在对应。Guest 静态固件初始化会走 premap 分支并绕过该函数里的 Host raw-heap map；直接 ELF 重定位与随包官方 C/H/S/Makefile 扫描未发现 PVZ client wrapper 调用，但不透明间接调用和外部 provider 仍无法排除。可用 `python3 scripts/verify-host-vgpu-fw-slot.py`、`python3 scripts/verify-static-guest-fw-premap.py` 和 `python3 scripts/verify-pvz-call-path.py` 重验对应静态证据。

**本轮离线回归通过**：对齐 GEM、TQX 上传/提交和启动 BO 测试夹具与当前接口后，`python3 scripts/verify-runtime-integration.py` 全部通过：15 个 C/RAM 检查、`W=1` 模块构建及保留 `mt_guest` ABI 对照；27 个 Python 单测通过。测试只使用 RAM/OS 模型，没有加载模块、写 PCI/GPU 或开启工作提交。另有 Host 固件槽和 Guest FW premap 静态核验；详细限制及本轮修订见 [Guest-only 移植记录](reports/official-linux-guest-port.md) 和 [集成验证报告](reports/runtime-integration-build.json)。

2026-09-29 实测：**尚未启用摩尔线程硬件加速**。本目录包含可复现的诊断工具、
Windows 驱动分析、Linux 内核编译实验和失败证据，不是可安装的 Guest 驱动发行版。
路径为 `/opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest`。

**Linux 2.3.0 Guest-only 适配进展**：本地包实际提供的是 Linux Host 源码，当前分支将其裁成 Guest-only 并针对 `6.12.107+deb13-amd64` 干净构建，能匹配本机 S3000 `1ed5:0222`。内核识别到有效 Guest 信息页（当前 OSID 7、1 GiB），但 PVR 在 `PhysHeapsInit` 因 heap 描述符期望数 5、实际生成数 4 而退出，没有 MTT DRM 节点或硬件加速。默认关闭的后链接 heap-count 候选已重新构建并通过 ELF、vermagic、PCI alias 和固件 staging 核验，尚未运行时验证；新的反汇编证据显示 Guest 固件初始化可走静态 premap 分支，因此 PVZ stub 暂不视为必经阻塞，但 BAR2 物理 backing 未验证。诊断、候选与后续条件见 [Guest-only 移植记录](reports/official-linux-guest-port.md)，源码兼容补丁见 [official-vgpu-2.3.0-linux-6.12-guest-only.patch](patches/official-vgpu-2.3.0-linux-6.12-guest-only.patch)。参考包只含 `.vz.win` 固件。

**2026-09-29 离线适配核验**：新增脚本核对 S3000 的 QUYUAN1 驱动映射和 VZ 固件家族；源码声明的 1.0.0.0 镜像对应当前路径，1.1/1.2 的 QY2/PH 标记不足以证明可替换。只读 PCI 检查显示两个 64 KiB BAR、一个 16 GiB BAR2，设备未绑定且 BusMaster 关闭；本 Guest 没有 VMBus。详见 [固件家族核验](reports/vz-firmware-family-audit.md) 和 [Guest-only 移植记录](reports/official-linux-guest-port.md)。PVZ guest map/unmap 的 Host ABI 仍未找到，尚无 GPU 加速。

**最新池内接入**：[上下文切片与保留区TQX上传](reports/context-pool-slices.md)
私有GEM入口已把五个普通BO接到shader/PDS状态/纹理池切片的自动分配和TQX准备流程；
切片按上下文缓存，并新增封存VM中的空闲切片回收/新上下文租用及上下文销毁入口。W=1编译通过；既有编码、
内核RAM及fence检查不覆盖本轮新销毁逻辑。尚未连接实际GPU执行，也没有公开ioctl/DRM节点。

**最新根页表追踪**：[TQX 根页表发布入口](reports/root-page-table-publication.md)
普通硬件节点的`SetRootPageTable`更新Guest Context/Process和设备侧root槽；`+0x250`二级回调仅适用于替代上下文，
不需要为普通TQX工作包臆造PCI/RPC发布步骤。工作包会携带root，但Linux扩容后的根替换及最终撤销仍未完成，
所以实际工作提交门保持关闭。本轮只读状态为`driver_state=2`/`firmware_state=1`、PCI未绑定，未执行查询或恢复。

**最新动态池接入**：[三个独立池的BO/VM生命周期](reports/reserved-pools-integration.md)
已接入生产内部准备与九项共享原子映射，完整TQX测试持有19个BO。
38,920次原始页面调用对照、257项内核RAM及321项fence检查通过；尚无GPU执行。

**最新静态程序接入**：[PDS/USC 初始化与动态池核查](reports/static-programs-path.md)
已将176字节PDS和20,096字节USC加入启动CPU暂存；原始指令全缓冲区对照、15组集成检查、
W=1/ABI通过。另确认三个独立动态池的真实分配地址；尚未接入其映射或上传新镜像。

**最新系统RAM接入**：[Paging Command真实后备与逐页地址转换](reports/system-memory-path.md)
已替换此前临时BAR后备，接入非连续系统页、BO生命周期及VM页表。
3,132页原始对照、14组集成检查、235项内核RAM和321项fence检查通过，仍无GPU执行。

**最新映射纠正**：[4 MiB Paging Command创建与映射](reports/paging-command-correction.md)
已替换此前误用的8 KiB Paging Context，新增独立分配并保持共享ABI。
16组原始路径、13组集成回归和227项真实内核RAM检查通过；仍未验证GPU执行。

**最新启动资源桥接**：[现有启动块到 BO/VM 引用](reports/boot-bo-lifetime.md)
已接入主模块内部接口，多个VM复用同一视图，最终引用释放前保留原分配。
13组回归和222项真实内核RAM检查通过；内容初始化/上传与实机执行仍待完成。

**最新进程映射接入**：[固定共享资源与原子批量绑定](reports/process-shared-resources.md)
已加入 VM 内部接口；TQX任务可同时持有九个任务对象、六个共享对象及页表。
166项内核GEM/RAM检查与321项fence检查通过。后续已接启动BO视图，硬件加速未启用。

**最新初始化追踪**：[状态分配与分页初始化](reports/tqx-state-init-path.md) 已执行到
原始 `InitContextResource`：它只登记 CPU 映射，未写状态模板。8组完整路径、
3组分配失败及3组无效通知通过；下一步推进上下文发布。仍无GPU执行验证。

**最新任务接入**：[TQX 上传、资源持有与 fence](reports/tqx-work-lifetime.md)
已贯通内部准备和异步引用：上传后持有九个对象及页表，队列满可重试，匹配完成后释放。
60种准备故障检查及321项真实内核RAM fence检查通过。尚无GPU执行；以下为各阶段记录。

**最新完整资源上传**：[九对象核查与十一页上传](reports/tqx-submission-upload.md)
已加入GEM，DMA写入与读回接通，独立引擎状态保留。55个I/O故障和15对新增别名
检查通过，内核RAM检查增至138项。尚未衔接实际任务持有/发布，硬件加速未启用。

**最新核心数接入**：[Guest 信息页到 TQX 布局](reports/tqx-topology-path.md) 的原始传递
路径已验证，主模块可自动取得核心数，GEM拒绝冲突参数和静默拓扑变化。
本机保存的信息页为1核心；126项内核RAM检查通过。生产主模块未加载，硬件加速未启用。

**最新提交编码**：[TQX DMA 与独立引擎状态](reports/tqx-dma-path.md) 已接入 GEM
CPU 编码，144 组完整原始路径对照通过；独立状态区的分配参数与地址也已核实。
119 项内核 RAM 检查通过。状态初始化、真实拓扑、新 BO 的映射/上传和实际提交
仍待完成；未做恢复或 Host 操作。下面记录保留各阶段当时的限制。

**最新 BO 上传接入**：[TQX 九页写入与读回](reports/tqx-upload-path.md) 已加入 GEM 内部接口，
覆盖五类资源、非零 BO 偏移、填充与失败回收。144 组页面对照、45 个 I/O/map
故障注入及 114 项内核 RAM 检查通过；生产 BAR 路径尚未实测，未获得 GPU 加速。

**最新进展**：[TQX 专用堆与 GEM/VM 接入](reports/tqx-heap-path.md) 已将五类资源
实际 VA 换算为编码地址，并核对七对象映射与物理别名。144 组完整原始路径及
100 项真实内核 RAM 检查通过；程序/状态上传、异步资源持有和提交转换仍待完成。
主模块未加载，没有恢复或 Host 操作。下文保留此前各阶段的证据与当时状态。

**本轮关键修正**：已确认本机 `1ed5:0222` 在参考驱动中使用 TQX 传输模块，
CE 版本为 0。主模块、GEM、上下文和提交路径已增加平台检查，拒绝本机 CE 请求；
此前 CE3 构造保留为其他平台的 RAM 参考实现，不能用于本机复制。
见 [平台选择证据](reports/device-profile-path.md)。
本机适配已转向 [TQX 复制构造](reports/tqx-copy-path.md)：拆块、几何和操作选择
接入 GEM 内部暂存接口，432 组原始指令对照通过；尚需完整 job 发射和命令流封装。
主实验模块未加载，未开放 GPU 工作提交。

进一步补齐 [TQX 程序库与 job 状态](reports/tqx-program-path.md)：提取并验证
131 项程序模板的 20,272 字节库，GEM 已提供 CPU 暂存接口；复制常量重排和两段
PDS 状态在 192 组原始 job 构造中一致。54 项内核 GEM/RAM 检查通过。
专用堆地址转换、目标描述符和完整命令流仍待接入，尚未上传程序或获得硬件加速。

最新补齐 [TQX 源纹理描述符与 sampler](reports/tqx-texture-path.md)，接入每块复制计划。
490 组源编码、432 组拆块和增强后的 192 组 job 原始指令对照通过；内核 GEM/RAM
检查增至 57 项。源 GPU VA 和描述符专用堆索引已区分，尚未实现真实堆映射及 GPU 执行。

最新补齐 [TQX 目标命令与组合 job](reports/tqx-destination-path.md)：490 组目标编码
和 192 组源/程序/目标组合对照通过；ASan/UBSan、W=1 构建及 63 项内核 RAM
检查通过。仍需专用堆映射、初始与收尾状态和实际提交，尚未获得硬件加速。

进一步补齐 [TQX 单页命令初始化与收尾](reports/tqx-stream-path.md)：从真实对象
构造执行到最终记录，190 组原始指令对照通过，内核 RAM 检查增至 69 项。
已确认常量 type4 分配和初始记录序号；专用堆映射、多拆块和真实提交仍待完成。

已推进至 [TQX 完整单区域复制编码](reports/tqx-copy-stream-path.md)：1～4 拆块、
状态复用、命令收尾和根地址导出通过 432 组完整原始路径对照；内核 RAM 检查
增至 76 项。仍需专用堆映射、资源生命周期和提交转换，尚无 GPU 执行。

**当前继续驱动适配，恢复问题暂不处理**：已接入可选的持久连接与运行上下文模式，
内核构建、资源持有/退出测试及原始指令对照通过，新模块未加载。
见 [运行上下文接入](reports/runtime-context-integration.md)。硬件加速尚未完成。
进一步补齐了 [显存对象与引用层](reports/buffer-object-layer.md)：普通显存后备、
清零、CPU/GPU 使用引用和错误回滚已实现。
现已接入 [BO 到 GPU 虚拟地址的映射层](reports/gpu-vm-mapping.md)，提供三级页表、
事务式绑定/解除绑定、对象引用持有和页表上传读回接口，供后续 GEM 与任务上下文使用。
这一层已通过离线测试，尚未在设备上发布新地址空间。
进一步加入 [Linux GEM 内部桥接](reports/gem-object-bridge.md)：创建/句柄/VM 绑定接口、
BO 容器最终销毁已接入；真实 Linux GEM 核心的 RAM 后备测试已通过。
尚未注册 DRM 节点或提供用户 ioctl。最新只读现场为主实验模块未加载、Guest2/FW1，
见 [本轮现场记录](reports/gem-stage-preflight.json)；下面的通信恢复模式描述属于历史状态。
现已补齐 [事件分派与空提交 fence 路径](reports/event-fence-path.md)，普通完成匹配、
队列回压和 dma_fence 回调通过原始指令对照及真实内核 RAM 测试。提交入口仍未开放，
主模块未加载，没有向显卡发送空提交或渲染命令。
进一步实现 [真实提交包构造与 VM 范围检查](reports/work-command-path.md)，确认根页表、
进程编号和命令 VA 字段的区别，并还原逻辑节点到 DM 的路由。120 个完整包及 20 个
节点路由对照通过，GEM 内部接口可生成经映射检查的 CPU 暂存包；尚未开放工作负载提交。

进一步接入 [工作任务资源生命周期](reports/work-resource-lifetime.md)：任务持有页表和全部
映射对象，失败回滚、发布后保留、匹配完成后释放已连接到 dma_fence 队列。
204 项真实内核 RAM 队列检查通过；实际工作提交仍关闭。

进一步接入 [进程和任务上下文](reports/execution-context-path.md)：上下文统一提供
进程 token、PID、VM 根和 DM 路由，pending 任务阻止上下文提前销毁。
6 种参考 Context、12 个根关联及 4 个 token 案例对照通过；216 项内核 RAM 检查通过。

新增 [复制引擎线性载荷](reports/ce-copy-path.md)：40 字节复制编码在 192 个案例中与
原始指令一致，GEM 内部接口已增加源/目标映射、越界和物理别名重叠检查。
24 项内核 GEM/RAM 检查通过；完整命令流起止与同步仍需还原，尚不能执行此载荷。

进一步接入 [CE3 命令段初始化、同步和收尾](reports/ce-stream-path.md)：128 组原始
序列的 176 字节命令段与 256 字节库记录一致，GEM 三对象暂存检查和 29 项内核 RAM
检查通过。库记录到 DMA 提交载荷的转换仍需还原，未开启真实提交。

本轮进一步接入 [CE 记录导出与分页转换](reports/ce-paging-path.md)：192 组记录转换、
128 组完整构造序列与原始指令一致，新增五个地址区间与后备别名检查，38 项内核
GEM/RAM 检查通过。已还原一条 Windows 分页封装路径；后续已确认本机 CE 版本为 0，
该路径不适用于本机。尚未向 GPU 发布任务。

**此前现场核查**：本机通信正常，但固件始终未消费五条命令；Guest 单端
恢复路径尚未确认。宿主操作已排除，不等待相关授权。最新补充了
[共享页显示中断标志核查](reports/display-shared-flags.md)和
[初始化失败清理路径验证](reports/failed-init-cleanup.md)；
[前次受阻核查](reports/guest-only-impasse.md)及下文保留各阶段历史记录。
此前重新继续的三轮核查已排除失败清理和显示共享标志这两条线索，尚无有据可执行的
Guest 启动步骤；见 [重新继续后的受阻审计](reports/guest-only-resumed-audit.json)。

Windows 驱动的完整批量分析见 [反编译库使用说明](DECOMPILATION.md)。
`decompiled/` 保存伪 C、完整汇编、函数索引、调用关系和失败记录，
`ghidra-projects/` 保存可复用的 Ghidra 工程。查询结果不必再次分析二进制。

## 后续实机适配进展

已进一步还原平台内存窗口刷新：新增 C 构造函数，65 个原始指令对照及 15 个
拒绝案例通过，分区起点与当前上传地址一致。它供连接后的上下文集成使用，尚未
发布到设备；固件仍未启动。见 [窗口构造核查](reports/guest-windows-audit.md)。

最新发现：Windows Guest adapter 初始化明确设置 PCI COMMAND 的 MEMORY|MASTER。
24 个原始指令案例通过，Linux 干净连接路径已补齐并编译，未替换当前通信模块。
本机单开 MASTER、MASTER 加既有队列通知两次试验均未推动固件；配置已恢复，
临时模块已卸载。没有操作宿主或触发重置。见 [PCI 步骤及实测](reports/pci-master-audit.md)。

最新补齐：连接/断开状态机增加参考驱动的异常健康值门控，36 个原始指令案例通过，
新模块编译通过，尚未替换在用的通信恢复模块。当前健康值为 1，固件仍未启动。
另发现内核故障恢复通知会进入旧 Host 的 MPC 级恢复路径，未当作本 VM 复位触发。
见 [本轮代码与实机核查](reports/guest-health-gate.md)。

**最新状态（2026-09-28，本机恢复试验后）**：按“宿主无法操作”的限制，已在 Guest
内撤销重启遗留的四页通信注册，并加载新版主模块恢复 IRQ/RPC 服务；协议协商通过，
宿主查询可持续回复。当前是 `recover_channels=1` 的通信恢复模式，没有上传新固件。
只读快照确认旧固件前 1 MiB 与重启前失败现场完全一致，仍为 Guest1/FW1、started=0、
命令队列 5/0。参考驱动的 OSID 刷新步骤也已实测，未改变设备信息或固件状态。
之后新增实时队列/地址回读，并对已有队列做了一次限定重通知；400 次观察后仍未推进，
没有增加命令。见 [重通知实测](reports/retained-kick-audit.md)。
进一步只读核对六段共 9,478,144 字节全部一致，软件页表遍历通过；Guest 更新元数据
返回当前参考包无可用更新，未下载或安装。见 [完整内存与元数据核查](reports/retained-memory-and-metadata.md)。
**仍未获得硬件加速**。后面的自持引用旧模块和辅助模块描述均为历史状态。
见 [本机恢复记录](reports/local-recovery.md) 与 [当前进度](reports/progress-2026-09-28.md)。

**历史状态（重启前，首次持续通信恢复后）**：首次实际固件连接失败，`connect_result=-110`，
断开未获确认（`disconnect_result=-108`）。模块仍绑定并自持引用，四页 RPC 通道和上传内存
仍保留；尚未恢复本轮上传前内容。固件命令环 head=5、tail=0，started=0，没有硬件加速。
试验触发共享 IRQ10 未处理中断；单纯重新启用曾失败。随后已加载
[持续通信辅助模块](kernel/recovery/README.md)，补上共享页中断确认及宿主统计查询回复，
实测 IRQ10 持续启用、消息环继续往返，SDDM、SPICE、Tailscale 服务均 active。
补交 BAR2 基地址、BAR4 缺席值和三条共享区通知后，固件状态仍未推进。
主模块源码现已接入连接前的持续 IRQ/RPC 服务，且连接等待期间同步回复查询；
新版已编译，尚未替换当前自持引用的旧模块。现场辅助模块已复用同一 transport，
消息环模型测试及更新后 25 秒实测通过，硬件连接尚待解决。
详见 [当前进度快照](reports/progress-2026-09-28.md) 和
[持续通信实测](reports/live-service-validation.json)。下文各次已卸载/已恢复描述仅指历史测试。

已将原始固件加载入口、连接重试和断开路径串起来执行：完整 8 MiB 固件、24 KiB
页表和 4 KiB 辅助区一致；模拟固件停滞时，排除五条命令的进程 ID 后，最终完整镜像
也与失败现场一致。`python3 scripts/verify-fw-load-entry.py` 使用保存的本机配置和产物，
`--live` 额外通过 `sudo -n cat` 只读比较当前绑定模块导出数据，不写设备。
该验证需要本目录已有的 device-info、Guest layout/state、bootstrap 和失败现场产物。
详见 [完整加载路径对照](reports/firmware-load-entry-live-validation.json)；
它验证 CPU 侧构造与提交，不代表宿主固件已经运行。

现已实现 [最小 Guest 协议探测模块](kernel/README.md)，在本机完成加载、绑定与卸载。
设备信息查询和四页共享通道收发均已成功，返回协议版本 2、OSID 4、1 GiB 显存字段。
宿主接受参考 2.7.5 协议的版本协商；完整寄存器、消息环和内存布局见
[协议记录](PROTOCOL-NOTES.md)。固件为 READY，Guest 尚未 ACTIVE，也没有硬件渲染节点。

以下 Native/Guest 编译实验是早期证据；新的 `kernel/mt_guest_probe.c` 是独立编写的
协议探测模块，未使用或取消旧实验中预编译核心的入口保护。当前模块仍加载，未安装到启动流程。

进一步实现了三级 GPU 页表编码，1980 组原始指令对照全部通过；提取并解析五份嵌入固件，
读取了本机固件和共享分段基线。详见 [固件与 MMU 适配记录](FIRMWARE-NOTES.md)。
页表和固件现已在首次连接试验中上传并发布固件地址，但尚未建立连接。

进一步追踪并执行参考指令，确认本机 PCI ID 的参考路径选择固件编号 0、三级 MMU。
新增 Guest 布局构造及固件加载阶段镜像构造：4096 组特征组合、20 组完整布局、
10 组各 8 MiB 的加载镜像均通过原始指令对照。结果保存在
`reports/firmware-layout-validation.json`，硬件连接尚未完成。

已继续实现本机 host-PB 路径的初始运行状态和 GPU 堆布局，22 个堆、13 项资源与
参考分配器输出一致，33 组完整镜像对照通过。私有分配池已从本机响应和原始代码确认，
候选页表地址修正为 `0x605800000`。详见 `reports/firmware-state-validation.json`。

已接入 Linux `gen_pool` 显存管理并实机验证：保留 BAR2，分配/映射 8 MiB 固件区、
6 页页表区和 1 页辅助区，验证空间不足、回收复用与卸载清理。
当时对辅助页完成了 4 KiB 图案写入、读回和原数据恢复校验，未写固件/页表内容；
GPU 虚拟地址映射和固件连接仍未执行；模块已卸载。
证据见 `reports/vram-reservation-validation.json`、`reports/vram-write-validation.json`。

已移植固件命令环、连接/断开命令构造及排空判断，新增 Linux I/O 映射适配和每单元生产者锁。
768 组入队、12 组满环、36 组排空、66 组命令构造通过原始指令对照。
实机读取六个单元的队列计数均为空；本轮未初始化设备队列、未发送命令，也未连接固件。
结果见 `reports/firmware-queue-validation.json` 和 `reports/firmware-queue-hardware-validation.json`。

2026-09-28：新增按需构造稀疏页表和三页默认映射，完整原始分配/映射路径对照通过。
YUV 系数与 DM Kill 程序已还原。内核 `prepare_resources=1` 在本机保留默认映射、
PDS/USC、YUV、DM Kill、fence、paging context 和 PB 的实际显存，生成约 1 MiB 的
CPU 准备数据；导出后逐字节匹配原始指令输出。该轮未上传或连接固件，随后释放映射。
详见 `reports/bootstrap-hardware-validation.json`。

连接/断开状态机已通过 27 组原始指令路径对照。内核从
`/lib/firmware/mt-vgpu-guest/gen1-guest-loader.bin` 读取固定散列的镜像，再构造 Guest 状态，
完整 8 MiB 与此前原始指令输出一致。随后实机完成整块固件 **备份→写入→读回→恢复→读回**，
两次比较均通过。这次确实写过固件分配区，但未发布地址、未敲 doorbell、未连接固件。
完整原数据备份保存在 `build/firmware/firmware-before-upload-8m.bin`；模块已卸载，
Guest0/FW1，服务正常。证据见 `reports/firmware-upload-validation.json`。

## 本机状态

| 项目 | 实测 |
| --- | --- |
| 系统 | Debian 13.7 amd64，Linux 6.12.107+deb13-amd64 |
| 摩尔线程 PCI | 0000:00:0e.0，1ed5:0222，subsystem 1ed5:1101 |
| Windows INF 对应型号 | Moore Threads S3000 MTvGPU-1101 |
| 内核驱动 / MTT render 节点 | 最新核查主实验模块未加载、PCI 未绑定 / 不存在 |
| OpenGL、Vulkan、KWin | 均为 llvmpipe 软件渲染 |
| 当前显示 | QXL；SDDM、SPICE、Tailscale 正常运行 |

PCI BAR2 的 16 GiB 是地址窗口，不能当作分配给此 vGPU 的显存。
用户已属于 `video`、`render` 组；当前问题不是这两个组的权限。

## 本轮取得的新证据

1. `/opt/MTT-driver-only` 是 Windows PE/INF 驱动，两个 INF 都匹配本机设备。
   `MT-VGPU-ENCODE.inf` 声明 `27.18.594.2562`，另一 INF 声明 `30.0.2505.1`；
   实际 `mtkm64.sys` 文件版本是 `30.0.2505.1`。目录含不同版本 INF，不能仅看其中一个。
2. 三个 `.sys` 中的 PDB 路径都带 `M-vdi-guest275`。这提示应向平台核查 **2.7.5 分支**，
   不是宿主驱动版本的实测值。Windows 内核驱动依赖 NT/WDDM，不能直接加载到 Linux。
3. 从[官方 S2000 产品页](https://www.mthreads.com/product/S2000)取得的
   `S2000_MT_vGPU_2.3.0.zip` 已下载、通过 ZIP CRC 检查并计算 SHA-256。
   包中 Guest 是 Windows `.sys/.dll`；Linux DKMS 是 **Host** 构建，配置为
   `RGX_NUM_OS_SUPPORTED=15`、`PVRSRV_APPHINT_DRIVERMODE=0`，没有 Linux Guest 用户态栈。
   包含的共享协议头文件可用于研究，未把 Host 驱动装入虚拟机。
4. 对社区 2.7.1/6.12 源码进行三组离线构建，结果见下表。
5. 更深的二进制检查发现：现有 Native 核心的 `PvzConnectionInit()` 在日志输出后
   固定返回 `0x14`，对应 `PVRSRV_ERROR_NOT_SUPPORTED`。
   所以仅删除参数检查、改变宏或者修好链接，都不能补上这个核心的虚拟化初始化。

## 实际编译实验

源：[dixyes/mtgpu-drv 的 2.7.1-6.12 分支](https://github.com/dixyes/mtgpu-drv/tree/2.7.1-6.12)，
固定提交 `099f7ea5afced34f9424618a9e2ec7aa37dba024`。
该树链接预编译 `mtgpu_core.o_binary`，不是完整可重编译的 GPU 驱动源码。

针对本机 Debian 内核，将 `pci_resize_resource()` 调用适配为四参数，第四参数为 0。
所有实验副本都额外在模块初始化入口直接返回 `-EPERM`，避免实验产物被误加载后探测设备。
原始源码、Windows 驱动保持不变。

| 实验 | 结果 |
| --- | --- |
| `native`：原配置 + 内核接口补丁 | 编译通过，vermagic 匹配；5138 条 objtool 警告；不是 Guest 驱动 |
| `guest-macro`：OS 数改为 8、Guest 宏改为 1 | 编译失败，缺少 `rgxdebug.h` |
| `guest-header`：再补入官方 2.3.0 的调试头 | modpost 失败：`_RGXDumpRGXMMUFaultStatus` exported without definition |

实验值 8 仅用于触发 `>1` 的条件分支，不代表本机宿主实际 OSID 数量。
Native blob 中上述调试函数只有本地符号 `t`，旧 Host blob 中是全局符号 `T`。
混用旧头文件不能改变预编译核心。

用对象文件的调试信息进一步比较 `_PVRSRV_DEVICE_CONFIG_`：
`ui32IRQ` 的偏移从 Native 的 **56** 变成 Guest 配置的 **88** 字节，
`hDevData` 从 **64** 变成 **96** 字节。使用原 blob 搭配改宏后的头文件存在 ABI 不一致。
这些证据说明现有材料不能靠几个配置项变成可靠 Guest 驱动；不等于完整逆向在理论上不可能。

## 使用工具

普通桌面用户执行（不需要 sudo）：

```sh
cd /opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest
python3 scripts/guest-doctor.py --output reports/machine-current.json
python3 scripts/audit-windows.py /opt/MTT-driver-only --output reports/windows-reference.json
python3 -m unittest discover -s tests -v
```

`guest-doctor.py` 只读取 sysfs 和运行查询命令，不读取 MMIO、不绑定 PCI、不加载模块。
需要核对一个离线候选时，可追加 `--candidate build/<候选目录>/mtgpu.ko`；脚本只读取候选文件的 SHA-256/build-id，并与内核导出的模块 build-id 比较。
缺少设备/驱动/render 节点时退出码是 2；节点出现也只表示可以进入功能测试，不宣告加速成功。
在没有桌面会话的 SSH 中，OpenGL/KWin 检查可能无法完成，JSON 会保留具体失败信息。
`audit-windows.py` 只做静态分析，哈希用于复现，不等同于验证发布者签名。

源码已下载在本机的 `src/`；重取社区参考源码可使用：

```sh
git clone --branch 2.7.1-6.12 https://github.com/dixyes/mtgpu-drv.git src/mtgpu-2.7.1-6.12
git -C src/mtgpu-2.7.1-6.12 checkout 099f7ea5afced34f9424618a9e2ec7aa37dba024
```

构建脚本要求固定提交且源码无改动，并限定本次验证的内核：

```sh
python3 scripts/build-experiment.py native
python3 scripts/build-experiment.py guest-macro
python3 scripts/build-experiment.py guest-header
```

最后一项需要官方 ZIP 内 `97499b97a_mtgpu-1.0.0.amd64.deb` 解包到
`src/official-vgpu-2.3.0/`，本机已准备好。脚本只在 `build/` 中复制、修改和编译，
保存补丁、日志和 JSON；不会安装、执行包维护脚本或加载模块。
实验 `.ko` 不可用于安装，不应移除入口保护后直接试载。

## 继续启用硬件所需材料

需要与云平台 Host 匹配的 **MT_vGPU_LINUX_GUEST amd64** 完整包，包含相配套的
内核核心、固件及 EGL/OpenGL/Vulkan/编解码用户态组件，并确认 Host 版本。
官方[安装指南](https://docs.mthreads.com/vgpu/version-2.9.2/vgpu-doc-online/install_guide/)
区分 Host、Windows Guest、Linux Guest，驱动获取入口为 `developers@mthreads.com`。
已准备 [平台材料请求文本](HOST-REQUEST.md)，没有代发消息。

不要因为版本更新就选择 2.9.2：官方[发布说明](https://docs.mthreads.com/vgpu/version-2.9.2/vgpu-doc-online/releasenote/)
明确它不兼容 2.5.x/2.6.x/2.7.x。当前仅有 `guest275` 线索，应先由平台确认。

取得真实 Guest 包后，顺序是：解包核对 → 针对 6.12 编译 → 确认核心/用户态/Host 版本 →
临时绑定并验证本卡 render 节点和 GMI → 应用 EGL/GL/Vulkan → KWin 与 QXL 互操作 →
官方云电脑画面与编解码验收。硬件加速成功须有实际运行证据，不能以模块能编译代替。

最初三组 Linux 构建实验未安装系统包、未加载实验驱动、未改 Mesa/QXL/GRUB/SDDM、未重启。
诊断工具的 5 项测试通过；收尾复查仍为 QXL + llvmpipe，三个服务 active，kernel taint 为 0。
诊断和实验文件在此目录内；`downloads/`、`src/`、`build/`、原始参考文件及完整构建日志
已加入本目录 Git 忽略规则，不把厂商二进制夹带入项目提交。

随后按用户要求安装了 Java 21、ripgrep 和本地 Ghidra 12.1.4，用于完整批量反编译
Windows 驱动。此次工具安装不加载显卡驱动，不改变显示配置。
