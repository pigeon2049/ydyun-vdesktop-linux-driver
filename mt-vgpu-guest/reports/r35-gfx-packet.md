# r35：TA/3D 寄存器与完整 Windows 图形包的 C 版重建

2026-09-30。已将 family 2 的 TA/3D 寄存器及受限单批次图形包移植为 C，
完整 18,112 字节输出在 288 组输入下与 Windows 原始指令逐字节一致。
此轮完成的是 CPU 侧构造和验证；没有发送 3D GPU 任务，尚不能运行 OpenGL/Vulkan。

本轮只读复核的启动 ID 仍为 `fe18deeb-d431-4c03-ac88-34235e9849c5`。
Guest2/FW2/started1/event_result0，completed=262、pending=0。
已加载模块身份与 r28/r31/r32/r34 保存的二进制相符，1080p 填充/复制前端仍在
`renderD130`。没有替换运行模块、操作宿主、请求复位或添加自动加载配置。

## 实现与原始指令依据

`kernel/mt_gfx_registers.h` 生成 176 字节 TA 寄存器和 352 字节 3D 寄存器。
输入仍保留参考驱动的 CPU 状态布局；尚未查明生产者语义的字段没有随意命名为
着色器地址、纹理或资源句柄。它不接受用户 ioctl，也不验证原始值对应合法 GPU 资源。

`kernel/mt_gfx_packet.h` 在此基础上生成完整单批次包，包括公共任务字段、RT 元数据、
寄存器、region 表和初始 CSW。范围限定为 family 2、primary version 2、单核、单批次，
无附加 fence/间接列表/性能采集。对不支持的计数、RT 数量、输出容量、DMA 地址和
缓冲重叠在写入前拒绝。源状态中的其它 GPU 地址仍需后续资源层验证；当前编码成功
不是硬件提交许可。

Windows 参考为 `/opt/MTT-driver-only/mtdxum64.dll`，SHA-256：
`3f2c843d3404d37b2beea9f97ebcc1e6207d11948c5da2c4581ccd4d8bc5ba0c`。
复用已有反编译库，通过 `scripts/gfx_reference.py` 执行以下原始指令：

| 地址 | 作用 |
| --- | --- |
| `180157d80` | 按硬件 family 设置布局常量；原反编译库未识别为独立函数，按反汇编限定至 `180157eee` |
| `180181ec0` | 计算整个提交包的大小、region 与 CSW 偏移 |
| `18015afa0` | 写外层包头、region 表、初始 CSW，调用任务序列化 |
| `18015b880` | 写 TA/3D 公共任务记录及寄存器 |
| `180158cc0` | 从 RT 与 batch 状态生成片段阶段元数据 |
| `180158f90` / `180159520` | TA / 3D 寄存器编码 |

原始指令运行在 Unicorn RAM 中。显式模型仅提供 CPU 状态 getter、memcpy/memset、
固定 PID、已映射栈探测和安全 cookie 校验；布局对象由原始 shape 填入，未执行
Windows 资源分配器。PE 文件及地址按散列固定，不运行 DLL 入口、Windows OS 代码、
驱动 ioctl 或硬件。修改 `ReferencePE/ReferenceOracle` 以接受显式固定散列，默认
KMD 路径和原有散列检查保留；不允许未列出的函数或 syscall。

## 完整包布局

| 内容 | 偏移 | 大小 |
| --- | --- | --- |
| Windows 包头 | `0x0` | `0x48` |
| 两个 region 描述，type 8 / type 5 | `0x48` | `0x30` |
| GFX region 头 | `0x78` | `0x18` |
| TA/3D 任务固定部分 | `0x90` | `0x3370` |
| TA 寄存器 | `0x3400` | `0xb0` |
| 3D 寄存器 | `0x34b0` | `0x160` |
| 对齐空隙 | `0x3610` | `0x10` |
| 初始 CSW 与 TA 扩展空间 | `0x3620` | `0x10a0` |
| 合计 | | `0x46c0` = 18,112 字节 |

原始 serializer 设置 CSW flags=1，并在其 `+0x1008/+0x100c/+0x1010` 写入
`0x10a0/1/0x1020`。末尾 `0x80` 字节由该函数清零；不能据此推断零上下文即可渲染。
type 8 的值来自外层源状态 `+0x1d8`，目前仅保留原字段值，资源语义仍需追踪。

Linux 参考仍为 legacy 5.2.0 `libsrv_um_MUSA.so.1.0.0`，SHA-256：
`b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0`。
`linux_gfx_reference.py` 执行 QY1 原始 `0017d890/0017d4b0` 编码寄存器，
并执行 `0017dab0` 及 Submission 系列函数生成完整 Linux 包。没有执行完整
`RGXKickGfx` bridge 或原厂应用初始化流程。

Windows 的固定任务部分 `0x3370`，Linux 为 `0x33b0`，相差 64 字节。
完整 Linux 包为 `0x46f0`，包头、CSW、region 顺序及大小也不同。96 组跨系统测试
显式匹配双方共享的三项元数据，并禁用 DDK debug 扩展；528 字节寄存器一致。
这不证明任意 Linux 包都可转换，本轮没有实现或开放通用 GFX 包转换器。

## 验证

- 180 组随机 Windows TA/3D 寄存器与 C 输出相同，覆盖 0–8 个 RT 项。
- 96 组 Linux / Windows / C 公共状态寄存器一致。
- 8 个原始完整 Linux 包及 24 个原始 Windows 任务记录中的寄存器尾部一致。
- 288 个完整 Windows 包与 C 编码相同，包含非零 PID、frame、job reference、
  type 8 值、RT/batch 字段和两种元数据条件分支；全包及末尾保护区均核验。
- 寄存器编码 6 项、整包编码 23 项异常输入拒绝，失败时输出不变。
- 当前 6.12.107 内核头文件下 W=1 编译无警告；生成的是无 init/exit 的编译验证模块，
  没有加载它。
- 共享原始指令执行器修改后，原 TQX 220 例填充及完整 DMA 对照、15 项拒绝检查通过。
  Linux TDM 的 80 例整包转换、4544 项拒绝及 76 例协议选择回归通过。

```sh
cd /opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest
python3 scripts/verify-gfx-registers.py
python3 scripts/verify-gfx-packet.py
python3 scripts/verify-gfx-kernel-build.py
python3 scripts/verify-tqx-fill.py
python3 scripts/verify-linux-tdm.py
sudo python3 scripts/check-graphics-session.py   # 只读，不提交 GPU 任务
```

结果位于 `reports/r35-gfx-register-validation.json`、`r35-gfx-packet-validation.json`、
`r35-gfx-kernel-build.log`、`r35-session-check.json`。原始输入/包、共享库、源码快照
及散列清单位于 `build/r35-gfx/`。

## 后续适配入口

完整包编码已具备，但仍缺真实图形资源、着色器、VDM 命令流、上下文保存/恢复任务和
标准用户态 bridge。不能用随机测试夹具向硬件提交。本机 r34 之后普通堆只剩约 2 MiB，
后续需要先计算图形上下文资源需求，并明确已有槽的所有权与复用方式。

已定位 Linux `RGXCreateRenderContextCCB` (`0017b6c0`)：CPU 上下文 `0x330` 字节，
CSW 源从 `+0x218` 开始。该函数依次调用 `001836b0` 分配 DCE 上下文资源，
`00183c40` 生成 PT 保存/恢复任务，`00183d30` 生成 SR 任务，`00182f40` 分配 IPP
资源；`00184f80` 准备 reset framework。协议 1 使用 `00136c40` bridge，协议 2
使用 `00137b20`；这些资源创建函数本轮只读反编译分析，尚未执行或移植。
PM render state 另有 `00171e40`，其分配大小受 feature 配置影响，不能照搬固定值。

下一步应闭合这些上下文资源的大小、专用堆和程序字节，再准备最小有效 3D 工作负载。
当前应用可用能力仍是已验证的 GPU 填充/复制，完整 Linux 图形驱动目标尚未完成。
